# Annexed Paint

This is the [AnnexedPaint repository](https://github.com/JustinCraver/AnnexedPaint),
a source fork of [ArmorPaint](https://github.com/armory3d/armorpaint), a native
application for 3D PBR texture painting. The application and generated binaries
currently retain the **ArmorPaint** name (`1.1alpha` in the source manifest).
This is a development checkout, not a qualified release.

The implementation includes mesh and texture import, painting with layers and
masks, node materials, undo/redo, `.arm` project files, texture and mesh export,
scripting, and timeline/video tools. Feature presence does not establish complete
workflow or platform coverage; see the [review and recommendations](base/docs/PROJECT_REVIEW.md).
No separate approved Annexed product roadmap is recorded in this checkout.

## Start here

- [Development commands and verification](base/docs/readme.md)
- [Architecture and workflow map](base/docs/repo_overview.md)
- [Prioritized project review](base/docs/PROJECT_REVIEW.md) — proposals, not approved commitments
- [Agent guidance](AGENTS.md)
- [Upstream user manual](https://armorpaint.org/manual) — usage reference; local source and test results establish this fork's current behavior

## Windows x64 quick start

Prerequisites: Git, Visual Studio C++ x64 build tools and Windows SDK, and the
Visual Studio **C++ Clang tools for Windows** component. No Node/npm setup is
required. The bundled `amake` generates projects and assets.

```powershell
git clone https://github.com/JustinCraver/AnnexedPaint.git
cd AnnexedPaint
.\paint\build-windows.ps1
.\paint\tests\smoke-windows.ps1
Push-Location .\paint\build\out
.\ArmorPaint.exe
Pop-Location
```

The helper uses installed tools only and supports `-Configuration Debug` and
`-Rebuild`. It handles installations that have `clang-cl.exe` but lack the
`ClangCL` MSBuild integration. The generated solution is
`paint/build/ArmorPaint.slnx`; the project is `ArmorPaint.vcxproj`. See the
[development guide](base/docs/readme.md) for the original build path and limits.

## Other target recipes

These existing target recipes are retained; they were **not verified** by the
2026-10-18 Windows review. Run them from `paint/` using the platform's toolchain.

| Target | Command | Prerequisites / output |
| --- | --- | --- |
| Linux x64 | `../base/make --run` | Clang, make and [system dependencies](base/docs/linux_deps.md) |
| macOS arm64 | `../base/make` | Xcode; `build/ArmorPaint.xcodeproj` |
| Android arm64 | `../base/make --target android` | Android Studio/SDK/NDK; `build/ArmorPaint` |
| iOS arm64 | `../base/make --target ios` | Xcode; `build/ArmorPaint.xcodeproj` |
| WASM | `../base/make --target wasm --compile --embed` | Clang with wasm target and C23 `#embed` support, make; WebGPU runtime |

`--embed` requests embedded assets and requires C23 `#embed` support (the
upstream build recipe specifies Clang 19 or newer). Non-embedded builds need the
generated `data/` directory beside the executable.

## Attribution

Preserve the [project license](license.md), third-party notices in
`base/assets/licenses/` and `paint/assets/licenses/`, and upstream attribution.
Upstream offers [paid binaries](https://armorpaint.org/download) to fund its
development; that distribution is separate from this fork's local builds.
