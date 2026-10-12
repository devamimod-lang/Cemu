# Medición de rendimiento: dónde está el cuello de botella

Primera medición real del proyecto. `ROADMAP_PERFORMANCE.md` listaba "perfilar
juego de referencia CPU/GPU" como paso 1 y nunca se había hecho.

Juego: Breath of the Wild. Equipo: Ryzen 7 8700F (16 hilos), RTX 5060.

---

## 1. Resultado: el límite es la CPU, y la GPU está esperando

### Hilos del proceso (% de una core, media sobre 6 s)

| Hilo | % | Qué es |
|---|---|---|
| **`OSSched[core=1]`** | **98.4** | **core 1 del PowerPC emulado — saturado** |
| `OSSched[core=2]` | 82.6 | core 2 del PowerPC |
| `LatteThread` | 71.6 | procesado de comandos de GPU |
| `OSSched[core=0]` | 66.4 | core 0 del PowerPC |
| `SDL_events` | 2.9 | — |
| otros 61 hilos | ~0 | — |

Total del proceso: 320 % de una core = 20 % de 16 hilos. El sistema tiene CPU
libre de sobra; lo que no tiene es **un core más rápido**, que es lo que
necesita un hilo saturado.

### Desglose del hilo de render (12 muestras de 600 frames)

| Métrica | Valor |
|---|---|
| `frame` | ~13 ms |
| `idle` (esperando comandos de la CPU) | 1–12 % (pico de 45 % sólo al arrancar) |
| `async` (GX2DrawDone / readback) | **0.0 %** |
| `fence` (esperando a la GPU) | **0.0 %** |
| `shader compile` | 0.0 % |

**`fence 0.0 %` es el dato que lo cierra:** el hilo de render nunca espera a la
GPU. La GPU termina su trabajo antes de que la CPU pueda darle más.

### Conclusión

Mover trabajo a la GPU **no puede subir los FPS aquí**. El hilo saturado ejecuta
código PowerPC del juego a través del JIT: serial, lleno de saltos y sensible a
latencia, que es justo lo que una GPU hace peor. Y no se reparte entre más
hilos, porque el reparto entre los 3 cores del Wii U lo decidió el juego.

Optimizar `LatteThread` (lo único que "tiraría más de la gráfica") tampoco
subiría FPS: está al 71.6 %, con holgura, y no es el límite.

---

## 2. Lo que resultó ser falso

### El hardcode de `GX2DrawDone` no cuesta nada

`GX2_Event.cpp` fuerza `forceFullSync = true` en Vulkan sin mirar el ajuste
`gx2drawdone_sync`, así que "Full sync at GX2DrawDone" es **inerte en Vulkan**.
Eso es cierto y está verificado en el código. Pero `async` mide **0.0 %** en las
12 muestras: el stall no existe en este juego y escena.

Era el sospechoso principal de un documento previo del repo y de mi propia
lectura. Medir antes de "arreglarlo" evitó cambiar código correcto por una
teoría, que es el error que más caro salió en la sesión anterior.

### Dos contadores están muertos

- `gpuTime_dcStage*` (texturas, vértices, shaders, índices, MRT, API de
  drawcall): declarados y con `frameFinished()`, pero **cero** llamadas a
  `beginMeasuring`/`endMeasuring`. Restos de un profiler de la era OpenGL.
- `recompilerLeaveCount`: se acumula y hasta se convierte en una tasa por
  segundo (`rlps` en `LattePerformanceMonitor.cpp:124`), pero **nada lo
  incrementa** en todo el código. Exponerlo reportaría 0 y engañaría.

Eso importa: medir el camino del recompilador exige instrumentarlo primero, no
sólo mostrar lo que ya hay.

---

## 3. Buena parte del cuello es de configuración, no de código

Graphic packs activos, y qué le cuestan a la CPU:

| Pack / preset | Efecto en el core saturado |
|---|---|
| **`Mods/FPS++`, límite 120 FPS** | **El más grave.** La lógica del juego corre una vez por frame; a 120 FPS el core 1 hace ~4× el trabajo que a 30. |
| **`Graphics`: "Extreme (Higher Draw Distance)"** | Más actores vivos → más física, más lógica y más draw calls, todo en el lado del juego. |
| `Mods/Draw Distance` | Un segundo multiplicador de distancia, encima del anterior. |
| `Graphics`: sombras "Ultra (300 %)" | Sobre todo GPU, que tiene holgura. |
| `Enhancements`: "Extreme (16x)" | Sobre todo GPU. |
| `Workarounds/Reshade Compatibility` | **Ya no sirve de nada**: ReShade está fuera. Overhead puro. |

La consecuencia práctica es que hay margen grande **sin tocar una línea de
código**, y hay que medirlo antes de invertir en el recompilador.

---

## 4. Plan, en orden de coste

### A. A/B de configuración *(minutos, cero código)*
Medir FPS en la misma escena cambiando una cosa a la vez:

1. **FPS++ a 60 FPS** en vez de 120. Hipótesis: el salto más grande.
2. **Draw distance a Normal**. Segunda hipótesis.
3. **Desactivar el pack de compatibilidad con ReShade.**

Repetir la medición de hilos tras cada cambio. Si `OSSched[core=1]` baja de
98 %, está confirmado.

### B. Build con PGO/LTO *(bajo riesgo, no toca lógica)*
Es la mejora de código más barata y apunta exactamente a lo que duele: código
JIT serial con muchos saltos. `ROADMAP_PERFORMANCE.md` §5 ya lo proponía.
No cambia comportamiento, sólo cómo se compila.

### C. Instrumentar el recompilador *(si A y B no bastan)*
Antes de optimizar nada ahí hace falta saber dónde se va el tiempo: tamaño de
bloque, frecuencia de caída al intérprete (hoy incontable, ver §2), spills de
registros. Es trabajo de medición, no de optimización.

### Lo que NO va a ayudar
Cualquier optimización del lado GPU: FSR, TAA, SSAO, más frames en vuelo,
descriptor sets, menos cambios de estado. La GPU ya está esperando.
