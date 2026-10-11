# Plan: volver a atacar el TAA después de revertir C/D

Estado: C/D (estimación de movimiento) **desactivado**. SSAO, SSR eliminado y la
cadena TAA quedan como en el build que funcionaba.

---

## 1. Qué se revirtió y por qué

Las fases C/D metieron tres regresiones encadenadas sobre un build que ya estaba
bien: bloques de colores, pantalla negra, y crash al entrar a la pantalla de
carga de una partida.

Cambios aplicados:

| Punto | Antes de C/D | Con C/D | Ahora |
|---|---|---|---|
| `RunFaroMotionEstimation` | no existía | 7 pases + pirámide | `return` inmediato |
| Captura de profundidad | sólo si `faro_postfx_enabled` | también si AA == TAA | sólo si `faro_postfx_enabled` |
| Target de AO | asignado inline | asignación diferida | **inline** otra vez |
| Vista de depuración | 5 opciones | 6 (con "Motion field") | 5 |

El código de la pirámide sigue en el archivo, inerte, para no arriesgar al SSAO
con una cirugía de ~170 referencias antes de que confirmes que el build está
bien. Se borra limpio en cuanto lo confirmes.

**Sobre el target de AO:** hoy lo cambié a asignación diferida con el argumento
de que su `WaitDeviceIdle()` era peligroso. Nunca comprobé que ese peligro se
disparara; era una teoría. El SSAO se verificó funcionando con la versión inline,
así que vuelve a inline. No se vuelve a tocar sin una reproducción que lo
implique de verdad.

---

## 2. Lo que el log del crash sí dejó claro

Dos defectos reales, **independientes de C/D**, que estaban ahí antes y siguen:

### 2.1 El caché de descriptor sets puede devolver una entrada con handles muertos

`GetFaroEffectDescriptorSet` indexa por el **valor numérico de los handles**:

```cpp
uint64 hash = (uint64)colorView;
hash = std::rotl<uint64>(hash, 13) ^ (uint64)depthView;
...
```

Vulkan **reutiliza valores de handle** después de destruirlos. Cuando el snapshot
de profundidad (o cualquier target) se destruye y se recrea, la nueva
`VkImageView` puede recibir exactamente el mismo valor numérico. El caché acierta
sobre la entrada vieja, cuyo descriptor sigue apuntando al objeto liberado.

Esto encaja con la firma del crash: `0xC0000005 READ at 0xF8` dentro del ICD —
un campo en offset `0xF8` de un objeto nulo/liberado, no un desbordamiento.

Es un fallo latente que puede morder al SSAO también, en cuanto el snapshot
cambie de tamaño. **Prioridad 1.**

### 2.2 El snapshot copia un buffer distinto del que post-FX selecciona

Las dos últimas líneas antes del crash:

```
19:18:43.866  depth capture: using depth 1280x720 for post FX (output 1920x1080, 4 drawcalls / 12 vertices)
19:18:43.897  depth snapshot: active, 1920x1080 format 126
```

`CaptureSceneDepthSnapshot` se llama desde la ruta de bind y copia **cualquier**
profundidad que se enlace, mientras `FindSceneDepthViewForSize` elige otra. Así
que el snapshot se recrea en bucle entre tamaños y los efectos leen una copia que
no corresponde al buffer elegido.

Y nótese el otro dato: **12 vértices**. El selector aceptó un buffer
prácticamente vacío, muy por debajo del umbral de 300. En la pantalla de carga no
hay geometría de escena y el heurístico se queda sin candidato válido.

---

## 3. Plan, en orden

### Fase 0 — Verificar el revert *(hecho, pendiente tu confirmación)*
Entrar a la partida guardada. Esperado: sin crash, SSAO como antes, TAA como
antes de C/D.

### Fase 1 — Invalidación del caché de descriptor sets
El caché no puede indexarse por handles crudos. Opciones, de menos a más
invasiva:

1. **Un contador de generación.** Cada destrucción de target incrementa
   `m_faroResourceGeneration`; se mezcla en el hash. Las entradas viejas quedan
   inalcanzables. Barato y suficiente.
2. Vaciar el caché al destruir cualquier target (`vkFreeDescriptorSets` o reset
   del pool) desde el límite de frame.

Recomiendo (1) más (2) como limpieza, para que el caché no crezca sin tope.

### Fase 2 — El snapshot sigue a la profundidad *seleccionada*
El snapshot debe copiar el buffer que `FindSceneDepthViewForSize` eligió, no el
que esté enlazado en ese instante. Una sola fuente de verdad.

### Fase 3 — Guarda de "no hay escena"
Si el candidato elegido no supera los umbrales (300 vértices / 16 drawcalls), los
efectos que dependen de profundidad deben **desactivarse ese frame**, no usar
basura. Es justo el caso de la pantalla de carga.

### Fase 4 — Recién entonces, el TAA

Aquí hay que ser honesto sobre el techo del enfoque:

- Lo que querías es que el TAA quite los bordes de sierra **sin emborronar**.
- Un TAA sin vectores de movimiento sólo puede acumular con jitter + recorte de
  varianza. Sobre geometría quieta funciona; sobre geometría en movimiento
  rechaza el historial y el antialiasing desaparece justo donde más se nota.
- Cemu no tiene los vectores de movimiento del juego, y las matrices de
  vista/proyección viven en uniform buffers del juego con layout desconocido. Por
  eso los add-ons tipo ReShade recurren a block matching sobre luma/profundidad.

O sea: **el block matching no era la idea equivocada, la implementación estaba
mal**. Pero no se vuelve a intentar sin estas tres condiciones:

1. **Interruptor en la GUI** para la estimación de movimiento, independiente del
   TAA. C/D entró como todo-o-nada y por eso cuando rompió no había forma de
   hacer A/B. Esto no se repite.
2. **La vista de depuración primero.** Es la lección que ya está escrita en la
   sección 14 del documento de investigación y que volví a ignorar: los
   contadores de CPU decían "GTAO corrió 600/600" y era verdad e irrelevante.
   Mirar el buffer intermedio contesta en segundos lo que las métricas no
   contestan nunca.
3. **Un pase por vez, compilando y verificando entre cada uno.** C/D metió siete
   pases nuevos de golpe.

---

## 4. Qué no vuelve a pasar

- **Cambiar código que funciona basándome en una teoría sin reproducción.** El
  target de AO de hoy fue exactamente eso.
- **Leer un stack de crash a medias.** Tenía `ReShade64.dll` delante en el log y
  lo pasé por alto; atribuí el fallo a nuestro uso de Vulkan sin verlo. Fue tu
  observación la que lo situó bien.
- **Confiar en los símbolos del crashlog sin comprobar el PDB.** `Cemu_release.pdb`
  es del 26-09 y el exe de hoy: todos los nombres de función de ese stack son
  ruido. Si hace falta leer un stack, primero regenerar el PDB.
