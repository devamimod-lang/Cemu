# Investigación: puntos reales de mejora de GPU en el renderer Vulkan de Cemu

Investigación de solo-lectura sobre el renderer Vulkan del core (no toca FSR1/FXAA/SMAA).
Cubre sincronización CPU-GPU, descriptor sets, pipeline cache, submits, memoria,
texturas, render passes, barriers y multi-threading.

## Veredicto rápido

La mayoría de los puntos "clásicos" que uno esperaría optimizar en un emulador Vulkan
**ya están bien resueltos** en Cemu: descriptor sets cacheados, memoria de
staging/buffers pooleada (sin `vkAllocateMemory` por frame), render passes/framebuffers
cacheados, submits ya agrupados (no uno por draw call), y barriers ya son selectivos
(no hay un barrier global "a lo bruto" en el camino en vivo). Esto confirma que el
equipo de Cemu ya hizo bien la tarea básica de un renderer Vulkan - no hay fruta
fácil ahí.

Encontré **un punto real y concreto** que sí es una regresión de rendimiento
silenciosa, y **dos puntos estructurales** que son mejoras genuinas pero de mayor
esfuerzo/riesgo.

## 1. Bug real: `GX2DrawDone` ignora la opción de configuración en Vulkan

**El más concreto y con mayor probabilidad de impacto medible.**

`GX2DrawDone()` (`Cafe/OS/libs/gx2/GX2_Event.cpp:217-235`) tiene `forceFullSync`
**hardcodeado a `true` para el backend Vulkan, sin importar el valor de
`gx2drawdone_sync`** (línea 221-223). O sea: la opción "Full sync at GX2DrawDone"
que aparece en el log (`GX2DrawdoneSync: true` en tu `settings.xml`) **no hace nada
en Vulkan** - siempre se comporta como si estuviera en `true`, sin que el usuario
pueda desactivarla.

Esto importa porque cada `GX2DrawDone()` dispara
`LatteTextureReadback_UpdateFinishedTransfers(true)` +
`LatteQuery_UpdateFinishedQueriesForceFinishAll()`
(`LatteCommandProcessor.cpp:1412-1417`). Si en ese momento hay un readback de
textura o una occlusion query todavía en vuelo, esto se convierte en un
**bloqueo real** del hilo de render esperando que la GPU termine
(`TextureReadbackVk.cpp:78-81` → `vkWaitForFences(..., UINT64_MAX)` en
`VulkanRenderer.cpp:2184`). Juegos que llaman `GX2DrawDone` seguido (varios lo
hacen por frame) mientras usan occlusion queries o leen texturas de vuelta a CPU
van a comer este stall cada vez, sin que el usuario pueda mitigarlo desde
Ajustes.

**Qué se puede hacer**: respetar `gx2drawdone_sync` también en el path de Vulkan
(actualmente solo se respeta para OpenGL, a juzgar por el hardcodeo). Esto es un
cambio acotado y de bajo riesgo - no toca lógica de renderizado, solo el flag que
decide si forzar el sync completo.

## 2. El pipeline cache en memoria crece sin límite

`m_pipeline_info_cache` (`VulkanRenderer.h:278`) es un mapa que va acumulando una
entrada por cada combinación (shader de vértices, estado de pipeline) vista durante
toda la sesión, **sin ninguna estrategia de expulsión** (confirmado en
`VulkanRendererCore.cpp:231-233`). El `VkPipelineCache` en disco (persistente entre
sesiones, bien) es una cosa distinta y está bien manejado
(`VulkanRenderer.cpp:2313-2428`); el problema es el mapa EN MEMORIA que vive
mientras el proceso corre.

En sesiones largas con muchos cambios de escena/efectos (más pipelines únicos
generados), esto es crecimiento de memoria sin techo - no es un stall de GPU per se,
pero sí es uso de RAM que crece indefinidamente y en teoría podría degradar cache
hits de CPU con el tiempo en sesiones muy largas.

**Qué se puede hacer**: una política LRU simple con un límite razonable (algunos
miles de entradas) sería suficiente. Menor prioridad que el punto 1 porque el
impacto es acumulativo/lento, no un stall inmediato.

## 3. Grabación de comandos en un solo hilo (estructural, no es un bug)

Todo el PM4/replay de comandos GX2 hacia llamadas Vulkan corre en un solo hilo
(`LatteThread.cpp`) - no se encontró ningún uso de
`VK_COMMAND_BUFFER_LEVEL_SECONDARY` ni grabación en hilos worker en ningún lugar
bajo `Renderer/Vulkan/`. La infraestructura de hilos existente (compilación de
pipelines, compilación de shaders, guardado de cache en disco, hilo de vsync) no
descarga la grabación de comandos por-draw en sí.

Esto es una limitación estructural del diseño (reproducir un stream de comandos con
estado mutable secuencial es difícil de paralelizar de forma correcta), no algo que
se arregle con un cambio quirúrgico. Lo menciono porque es el techo real de cuánto
puede escalar el uso de CPU en sistemas con muchos núcleos, pero atacarlo es un
proyecto grande (command buffers secundarios grabados en paralelo con
sincronización correcta de estado), con riesgo real de introducir bugs de
sincronización sutiles. No lo recomendaría como próximo paso.

## Recomendación de orden de trabajo

1. **Arreglar el hardcodeo de `GX2DrawDone` en Vulkan** (punto 1) - bajo riesgo,
   impacto real y medible en juegos que usan occlusion queries/readback seguido.
   Este es el candidato correcto para implementar primero.
2. **Límite LRU en el pipeline cache en memoria** (punto 2) - bajo riesgo, impacto
   más marginal pero fácil de hacer bien.
3. **Paralelizar grabación de comandos** (punto 3) - NO lo recomendaría ahora:
   alto esfuerzo, alto riesgo de bugs de sincronización, y el resto del pipeline
   (descriptor sets, memoria, render passes, barriers) ya está bien optimizado, así
   que el retorno de esta inversión es incierto comparado con el riesgo.

Todo lo demás investigado (descriptor sets, memoria de staging, cache de texturas,
render pass/framebuffer, barriers, patrón de submits) está ya bien implementado -
no encontré puntos de mejora reales ahí.
