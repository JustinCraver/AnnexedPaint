Brute Pathtracer is the custom path-tracing engine in the shared Iron code. It
trades correctness for speed and uses hardware-accelerated ray tracing through
the Direct3D12, Vulkan and Metal backends.

In ArmorPaint, it is used for rendering the path-traced viewport mode and for ray-traced baking features.

The backend declarations currently allocate 64 unique geometry entries and 1024
instances (`GPU_RAYTRACE_MAX_OBJECTS` in the backend headers and the instance
arrays in their implementations). These are implementation limits, not tested
scene-size guarantees. It has two modes - fast, which has most of the features
disabled, and quality, which has features like translucency and emission enabled.

- [Ray-tracing shaders](../shaders/raytrace/src/)
- [Viewport path](../../paint/sources/render/render_path_raytrace.c)
- [Bake path](../../paint/sources/render/render_path_raytrace_bake.c)
- [GPU interface](../sources/iron_gpu.h)
- [Backend implementations](../sources/backends/)

The Windows startup/import checks in the [project review](PROJECT_REVIEW.md) do
not qualify ray-traced viewport quality or baking output.
