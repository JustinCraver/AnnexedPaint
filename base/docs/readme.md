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

For batch texture export, supply an existing output directory and an exact preset
name from `data/export_presets/`, without `.json`. Spaces and dots in directory
names are supported. Types are `png`, `jpg`, `exr16` and `exr32`:

```powershell
.\ArmorPaint.exe --background --export-textures png base_color 'C:\Textures\output.v1'
.\ArmorPaint.exe --background --script 'prepare.c' --export-textures exr16 generic 'C:\Textures\output.v1'
```

When combined, the script runs on the opened project first. The CLI waits for its
finite next-frame/update callbacks, then applies the requested export settings and
completes the writes before background exit. Invalid arguments/type/preset/directory,
script load/compile/runtime errors, a nonzero script `main` result, and texture
write failures return **1**; successful completion returns **0**. Explicitly quitting
a script before its requested CLI texture export also fails. Use one export flag;
`--api` cannot be combined with scripts or export. Rejected project and plug-in
mesh imports also return **1** and stop the script/export sequence (review P1-03).
A failed multichannel export can leave channels
already written; it reports failure rather than claiming a complete export.

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
.\paint\tests\export-windows.ps1
.\paint\tests\export-windows.ps1 -SkipNative  # image writer and encoder checks
.\paint\tests\import-windows.ps1
.\paint\tests\import-windows.ps1 -SkipNative  # decoder, mesh stream and glTF checks without a GPU
.\paint\tests\editor-windows.ps1
.\paint\tests\editor-windows.ps1 -SkipNative  # standalone interpreter checks without a GPU
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

The export suite checks the production PNG/JPEG writer with injected short-write,
flush and close failures. Guarded allocations detect encoder overruns for small
and incompressible PNG/JPEG images; decoded output checks dimensions and color.
`-SkipNative` runs these without a GPU. The full suite checks direct and script-then-export commands, exact presets,
decoded 2048x2048 PNG/JPEG/half-float EXR/float EXR channel values, invalid arguments,
missing/failed scripts, deferred runtime errors and real locked texture writes.
It checks that partial multichannel failure returns nonzero and suppresses success.
Twelve packed-save processes exercise automatic shutdown by default; change that
count with `-ShutdownRepeats`. The save suite also uses automatic CLI completion
after its assertions. Every native process retains the original 30-second limit;
there are no crash retries. Results, logs and hashes stay under
`paint/build/tests/export-<id>/`. These checks do not qualify interactive painting,
other platforms, audio playback, browser/cloud completion or every texture preset.

The import suite tests the production project staging decoder with every
truncated prefix against an unreadable guard page, every staging allocation
failure, wrong field types/counts, invalid mesh/material/parent references,
overflowing dimensions and corrupt LZ4 data. It checks generated format-version
0-19 fixtures (including the old `fill_layer` reference), two bundled mesh scenes,
and complete mesh-stream rejection when a later record is null or invalid.
The stream harness supplies controlled importer/scene callbacks; its staging
owner and the canvas encoder are extracted unchanged from production source,
with source/hash receipts. `-SkipNative` also checks missing/truncated glTF buffers,
invalid accessors/nodes/cycles and explicit rejection of multiple primitives.

The full import suite uses a populated disposable native project and checks
geometry/layer/material identity, asset references, timeline, dirty title,
filepath and recent files after rejected imports. A saved three-object project
reopens with its geometry and material mapping; generated versions 4, 9, 18 and
19 also reopen. These are generated compatibility fixtures, not an archive of
historical project files. Invalid CLI project/glTF imports must exit **1** before
exporting anything. Receipts stay under `paint/build/tests/import-<id>/`, with
the same 30-second native limits and no crash retries.

Project decoding validates fields by name before resetting the active project;
the wire encoding, legacy defaults and imported-script trust gates are retained.
glTF geometry import currently accepts one triangle primitive per mesh node.
Multiple primitives are rejected as a whole file, preserving active work;
material splitting/merging and glTF material-definition import are not added.
Sparse accessors and non-triangle primitives are also explicitly unsupported.
The checks do not qualify all malformed inputs/codecs, every historical project,
OBJ/Blender/FBX parser internals, interactive dialogs/painting, other platforms,
or resource exhaustion and GPU allocation failures during reconstruction.

The editor/interpreter command compiles and runs all 39 existing minic checks,
including expected compile/runtime errors and a native-to-script callback value.
The standalone host uses production interpreter/math code with controlled scene
and formatting bindings; it does not qualify the editor's full API registry.
An injected failing assertion must return **1**. `-SkipNative` stops after these
checks and does not claim editor/GPU coverage. To verify the command's own failure
status, run `./paint/tests/editor-windows.ps1 -SkipNative -InjectFailure`; this
deliberately fails and retains its results.

The full command runs the existing `paint/assets/plugins/dev/test.c` exercise in
a fresh hidden runtime with a supplied output root containing spaces and a dot.
It requires 43 state/output assertions and six more after a fresh-process reopen,
plus nonzero status for a deliberately failed script assertion. Separate PNG
decoding checks every pixel for brush/world strokes, erasing, fill, exact undo/redo,
save/reopen and repaint/resave. Five exported channels must round-trip exactly;
an isolated known-color layer checks base RGB, normal, occlusion, roughness and
metallic values and grayscale/RGB layouts. Missing outputs and unequal images
must fail. Every process has a 30-second limit, without crash retries. Logs,
generated scripts, source/output/executable hashes and `results.json` remain in
`paint/build/tests/editor-<id>/`.

Like the smoke runner, `-Executable` can select another binary while runtime data
comes from this checkout's `paint/build/out/data`. If the staged executable is in
use, an already successful build at `paint/build/x64/Release/ArmorPaint.exe` can
be tested directly without closing the running editor. This command verifies
selected automated Windows/Direct3D12 workflows, not manual painting quality,
dialogs, stylus/device input, physics, every export preset/bit depth, other
platforms or hosted CI.

Other existing checks have narrower or different purposes:

- `base/tests/triangle`, `cube`, and `fall` are graphical sample applications.
  Run `..\..\make.bat` from the selected sample directory to generate its project.
  They are not an automated editor regression suite.
- `base/sources/libs/minic_tests.c` returns an aggregate failure count to
  `paint/tests/minic-runner.c`. Compiling it into the app does not execute it;
  use the editor/interpreter command above.
- `paint/assets/plugins/dev/test.c` replaces project state and is not included by
  the normal `assets/plugins/*` packaging rule. The runner appends its `main()`
  calling `test_begin(output_root, mode)` and checks both script results and
  decoded outputs. Run it through the disposable command above.
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
