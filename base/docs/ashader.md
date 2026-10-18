## ashader

A wrapper around the bundled [Kong compiler](../sources/kong/). It converts
`.shader` sources to graphics API formats: HLSL/Direct3D bytecode, Metal shader
source, SPIR-V for Vulkan, and WGSL for WebGPU. The build path is implemented in
[ashader.c](../tools/amake/ashader.c); [amake](amake.md) invokes it during asset
generation. See `base/shaders/raytrace/src/` for the separate ray-tracing shaders
and backend scripts.
