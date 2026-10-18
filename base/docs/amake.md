# amake

[amake source](../tools/amake/) is the ArmorPaint build tool, based on
[kmake](https://github.com/Kode/kmake). `base/make` (Unix) and `base/make.bat`
(Windows) invoke a checked-in binary in `base/tools/bin/<platform>/`.

Run it from the directory containing the desired `project.c`, normally `paint/`.
There is no npm/CMake setup step. Assets and shaders are generated even when
compilation is not requested.

| Option | Behavior |
| --- | --- |
| no options | Generate the host project, shaders and assets under `build/` |
| `--compile` | Generate and compile (Release by default) |
| `--debug` | Select Debug configuration |
| `--run` | Request compilation and launch; use explicit launch from `build/out/` on Windows for a known working directory |
| `--target windows\|linux\|macos\|android\|ios\|wasm` | Select export target; requires its toolchain |
| `--graphics direct3d12\|vulkan\|metal\|webgpu` | Select graphics backend; valid combinations depend on target |
| `--embed` | Application project flag for embedded assets; requires C23 `#embed` support |
| `--c <file.c> [args]` | Run a C file through minic, e.g. [extract_locales.c](../tools/extract_locales.c) |
| `--ashader <spirv\|metal\|hlsl\|wgsl> <from> <to> [-DNAME...]` | Compile a shader directly |

The option parser is in [main.c](../tools/amake/main.c). It does not provide
`--help`, a `--to` output override or an automatic test runner. Unknown options
are not reliably diagnosed; do not invent flags. The Windows export is `.slnx`
plus `.vcxproj`, not the older `.sln` named by previous documentation.

The [Windows helper](../../paint/build-windows.ps1) calls generation first, then
MSBuild with the available installed Clang toolchain. See [development commands](readme.md).

## Components

- [aimage.c](../tools/amake/aimage.c): converts images to `.k` (header plus
  LZ4-compressed pixels) and platform icons.
- [ashader.c](../tools/amake/ashader.c): converts `.shader` files to backend
  formats; see [shader notes](ashader.md).
- [make.c](../tools/amake/make.c): evaluates `project.c` through
  [minic](../sources/libs/minic.c), collects project inputs and exports assets.
  The project API is in [amake.h](../tools/amake/amake.h).
- [script.c](../tools/amake/script.c): implements `--c`.
- [exporters.c](../tools/amake/exporters.c): Visual Studio, Xcode, Android and
  makefile generation.

Editing these sources does **not** update the checked-in binaries. For build-tool
work, generate/build its own project from `base/tools/amake/`, then validate the
candidate separately. The tool derives engine paths from its executable location;
moving/replacing the bootstrap binary needs an explicit, reversible workflow.
This review did not rebuild or replace bundled tools.
