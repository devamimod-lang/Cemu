# Cemu Performance Roadmap

## Diseño actual y razones
Cemu emula Wii U / Cafe con:
- CPU Espresso PowerPC 750 3-core a 1.24 GHz con JIT recompiler vía IML → x64/AArch64 y fallback interpreter.
- Scheduler PPC con quantum 45000 instrucciones y HLE coreinit.
- GPU Latte AMD GCN-derivado: comando Latte → transcompilación LatteTC ZpIR → backend Vulkan/OpenGL/Metal.
- MMU PowerPC con página 128 KB.
- HLE de coreinit/gx2/iosu y servicios del sistema.

Razones de diseño:
- PowerPC requiere JIT para velocidad jugable.
- GPU Latte cerrada → transcompilación a ZpIR evita interpretación instrucción a instrucción.
- HLE acelera compatibilidad evitando LLE completa de IOSU.
- Tiling AMD exacto en LatteAddrLib para corrección de texturas.

## Puntos de mejora

### 0. Mejor uso de GPU
**Qué mejorar**
- Reducir cambios de estado dinámico y batch por GraphicsPipelineKey.
- Usar descriptor sets update_after_bind y reutilizar descriptor pools.
- Mover decode de tiling y uploads a compute async.
- Aumentar número de frames en vuelo para saturar GPU.
- Upscaler integrado en el final pass para evitar copias extra.

**Archivos**
- src/Cafe/HW/Latte/Renderer/Vulkan/
- src/Cafe/HW/Latte/LatteAddrLib/

**Métricas**
- GPU utilization, tiempo de submit, draw calls/frame.

**Riesgo**
Medio

### 1. CPU Recompiler
**Qué mejorar**
- IML optimizaciones, mayor tamaño de bloque, mejor asignación de registros.
- Reducir recompilerLeaveCount en código dinámico.
- Ajustar quantum por perfil de juego, mejorar boost/deboost.

**Archivos**
- src/Cafe/HW/Espresso/Recompiler/PPCRecompiler.cpp
- src/Cafe/HW/Espresso/Recompiler/IML/
- src/Cafe/HW/Espresso/PPCScheduler.cpp

**Métricas**
- IPC, tiempo de compilación bloque, ciclos por instrucción.

**Riesgo**
Medio

### 2. Shader transcompilación y GPU backend
**Qué mejorar**
- Cache de shaders persistente por juego, warm-up.
- Optimizar ZpIR → SPIR-V lowering.
- Reducir cambios de estado dinámico en Vulkan/Metal.
- Usar Vulkan preferente sobre OpenGL.

**Archivos**
- src/Cafe/HW/Latte/Transcompiler/LatteTC.cpp
- src/Cafe/HW/Latte/Transcompiler/LatteTCGenIR.cpp
- src/Cafe/HW/Latte/Renderer/Vulkan/
- src/Cafe/HW/Latte/LatteShaderCache.cpp

**Métricas**
- Tiempo shader create, stutter primer frame.

**Riesgo**
Bajo-medio

### 2.1 FSR 1 mejorado
**Qué mejorar**
- Reemplazar filtro actual por kernel mejorado con edge detection.
- Añadir sharpening adaptativo y calidad configurable por juego.
- Integrar upscaler como shader fullscreen en el final pass Vulkan.

**Archivos**
- src/Cafe/HW/Latte/Renderer/Vulkan/

**Métricas**
- Calidad percibida, overhead GPU.

**Riesgo**
Bajo

### 2.2 FSR 2 temporal
**Qué mejorar**
- Integrar AMD FidelityFX Super Resolution 2 temporal open source MIT.
- Requiere depth buffer, color buffer y velocity buffer en resolución de render.
- FSR 2 reemplaza TAA interno; post-procesos que necesitan anti-aliasing van post-upscale.
- Implementar generación de velocity desde matrices previas y depth.

**Archivos**
- src/Cafe/HW/Latte/Renderer/Vulkan/
- src/Cafe/HW/Latte/LatteShaderCache.cpp

**Métricas**
- Calidad de imagen vs native, ghosting, overhead.

**Riesgo**
Alto

**Generación de velocity en Cemu**
- Hoy Cemu no genera velocity. Renderer Vulkan maneja color + depth sin motion vectors.
- Enfoque viable:
  1. Capturar matrices MVP por draw desde REG_CONST/uniform buffers en LatteCommandProcessor.
  2. Guardar historial de frame: prevDepth, prevColor, prevMatrices.
  3. Generar velocity via pass screen-space: reconstruir posición con depth y matrices prev/actual.
  4. Alternativa mesh-based: modificar LatteTCGenIR para output de motion vector.
- Puntos de inserción:
  src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRendererCore.cpp
  src/Cafe/HW/Latte/Transcompiler/LatteTCGenIR.cpp
  src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.cpp




### 3. Texturas y memoria
**Qué mejorar**
- Optimizar decode de tiling en LatteAddrLib con unroll y SIMD.
- Reducir overhead de texture readback y buffer cache.
- Mejorar TLB/MMU con caché de traducción.

**Archivos**
- src/Cafe/HW/Latte/LatteAddrLib/
- src/Cafe/HW/Latte/LatteTextureCache.cpp
- src/Cafe/HW/MMU/MMU.cpp

**Métricas**
- Tiempo de acceso textura, misses MMU.

**Riesgo**
Medio

### 4. Scheduler y Core Timing
**Qué mejorar**
- Reducir lock contention en coreinit scheduler.
- Batch eventos de timing y evitar micro-despertares.
- Perfilado de quantum por juego.

**Archivos**
- src/Cafe/OS/libs/coreinit/coreinit_Scheduler.cpp
- src/Cafe/HW/Espresso/PPCScheduler.cpp

**Métricas**
- Latencia de cambio de hilo, uso CPU scheduler.

**Riesgo**
Medio

### 5. Build y optimizaciones
**Qué mejorar**
- PGO/LTO, flags de compilación agresivos, usar JIT con optimizaciones unsafe donde sea seguro.

**Archivos**
- CMakeLists.txt
- BUILD.md

**Riesgo**
Bajo

## Próximos pasos
1. Perfilar juego de referencia CPU/GPU.
2. Implementar cache persistente de shaders.
3. Medir impacto de optimizaciones recompiler.
4. Prototipar FSR 1 mejorado como shader fullscreen.
5. Evaluar viabilidad de velocity buffer para FSR 2 temporal.
