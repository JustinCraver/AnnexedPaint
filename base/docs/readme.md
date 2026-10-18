# Development guide

Start with the [repository overview](repo_overview.md) and the dated
[project review](PROJECT_REVIEW.md). The [upstream manual](https://armorpaint.org/manual)
is a usage reference, not proof that a workflow works in this checkout.

## Windows x64 / Direct3D12

Use PowerShell at the repository root. Install prerequisites separately if they
are missing: Git, Visual Studio C++ x64 tools, a Windows SDK, and C++ Clang tools
for Windows. Builds do not require Node/npm or a package restore. Native startup,
including `--background`, still requires a working graphics device.

```powershell
.\paint\build-windows.ps1
.\paint\build-windows.ps1 -Configuration Debug   # alternative configuration
.\paint\build-windows.ps1 -Rebuild              # full Release rebuild
```

[build-windows.ps1](../../paint/build-windows.ps1) discovers the latest Visual
Studio installation with x64 C++ tools using `vswhere`, generates assets/projects,
and invokes MSBuild. It uses the bundled Visual Studio `clang-cl.exe`. When the
`ClangCL` integration is absent, it selects the installed native toolset with
command-local `CLToolExe`/`CLToolPath` overrides. It neither installs components
nor edits global configuration. It fails on generation/build failure and copies
the successful binary into `paint/build/out/`.

The original build route remains available from `paint/`:

```powershell
Push-Location .\paint
..\base\make.bat                 # generate only
..\base\make.bat --compile       # requires ClangCL MSBuild integration
Pop-Location
```

Outputs are `paint/build/ArmorPaint.slnx`, `ArmorPaint.vcxproj`, generated assets
under `build/out/data/`, and compilation products under `build/x64/Release/` or
`build/x64/Debug/`. Generation is relative to the current project directory, so
calling `base/make.bat` at the repository root is incorrect. Generated projects
are overwritten by generation; make lasting build changes in the project/build
sources, not the generated XML. Both configurations share `build/out/`.

```powershell
Push-Location .\paint\build\out
.\ArmorPaint.exe --help
.\ArmorPaint.exe --background
.\ArmorPaint.exe
Pop-Location
```

Keep the matching `data/` next to a non-embedded executable. Development settings
are normally written to `data/config.json`; protected installations use the
application data directory. Use disposable copies for destructive workflow tests.

### Troubleshooting

- `MSB8020` for `ClangCL`: having `clang-cl.exe` alone does not provide the MSBuild
  integration. Use the helper above with the installed compiler. The 2026-10-18
  check used Visual Studio 18.7.2, Clang 22.1.3 and the `v145` fallback; this is a
  tested local combination, not a new minimum version requirement.
- Git ownership warnings: inspect the checkout owner and use process-scoped Git
  trust for the verified path if appropriate. Do not change global trust. `amake`
  also invokes Git for `build/version.json`; inspect its `sha` because generation
  can succeed with an empty SHA when that child Git invocation fails.
- A compiler-success result does not verify GPU startup. Retain failing logs and
  distinguish toolchain/environment failures from application failures.
- The fallback currently emits existing unused-option/SDK/header warnings. Do not
  suppress warnings or weaken checks to make the build look clean.

## Verification

```powershell
.\paint\tests\smoke-windows.ps1
.\paint\tests\save-windows.ps1
.\paint\tests\save-windows.ps1 -SkipNative  # writer/lifecycle checks without a GPU
git diff --check
```

The smoke script creates a fresh runtime under `paint/build/tests/<id>/`, copies
generated data without the current `config.json`, and runs hidden native
processes. Its eight checks cover help's zero exit status, background startup,
lowercase/uppercase glTF and SVG imports, and forward-slash absolute Windows
paths for both formats. Import checks require actual named objects or the expected image dimensions,
not only exit code zero. Each process has a 30-second limit. Logs, executable hash
and `results.json` remain available. An alternate binary can be supplied with
`-Executable`; runtime data still comes from this checkout's `paint/build/out/data`.

The save suite compiles the production byte writer and deferred save lifecycle
with controlled I/O and dialog results, using the installed Clang tools. It
checks short writes, flush/sync/close/replace failures, real locked/read-only
files, unique sibling collisions, Save As/cancel/retry behavior and quit intent.
A separate process terminates during a partial write: the previous file must
remain intact, and its partial sibling is retained as evidence. No GPU is needed
for these checks (`-SkipNative`).

The full save command also uses a disposable hidden native runtime. It checks
missing-parent/locked/read-only failures, dirty titles and recent files, successful
replacement, fresh-process reopen with named layers/materials, external/packed
textures and a timeline keyframe, matching PNG exports before/after reopen, and
successful save-and-quit. Packed reopening removes the external fixture first.
Each native process has the same 30-second limit as the smoke suite. The runner
requires explicit assertion markers and output evidence; script exit zero alone
is insufficient. Per-check logs, hashes and `results.json` stay under
`paint/build/tests/save-<id>/`. Native file-picker interaction, physical full-disk
and power-loss behavior, other platforms and browser/cloud completion are not
qualified by these checks.

Other existing checks have narrower or different purposes:

- `base/tests/triangle`, `cube`, and `fall` are graphical sample applications.
  Run `..\..\make.bat` from the selected sample directory to generate its project.
  They are not an automated editor regression suite.
- `base/sources/libs/minic_tests.c` contains interpreter checks, but there is no
  checked-in command-line runner calling `minic_tests()`. It prints PASS/FAIL;
  compiling that file into the app does not execute the checks.
- `paint/assets/plugins/dev/test.c` is a scripted editor exercise with 26 expected
  checks. It uses `/tmp/armorpaint`, replaces project state, and is not included by
  the normal `assets/plugins/*` packaging rule. Run only in a disposable runtime
  with its output root adapted there. Some checks merely confirm a call completed;
  `TEST PASS` alone is not pixel-level or persistence qualification.
- There is no repository-wide test or lint command. Use targeted assertions,
  compiler diagnostics, `git diff --check`, and native workflow checks. Format only
  touched C sections with the existing `.clang-format` when useful.

For editing changes, manually exercise import, paint, undo/redo, save, reopen and
texture export on disposable files. Verify saved content and exported dimensions,
channels and color space. Input/device changes need the relevant physical device;
background tests do not verify dialogs, stylus pressure or focus behavior.

## Other platforms and build internals

Retained target recipes are in the [root readme](../../readme.md). See
[Linux dependencies](linux_deps.md), [amake](amake.md), [shaders](ashader.md), and
[path tracing](brute_pathtracer.md). Existing workflows in `.github/workflows/`
compile Windows, Linux and macOS; their presence is not evidence that they passed
for a local working tree. This review did not change CI or verify those hosted runs.
