# Working on Annexed Paint

## Purpose and authority

This is a native C source fork of ArmorPaint for 3D PBR texture painting. The
application still identifies as ArmorPaint. Preserve upstream licenses and
attribution; do not infer a rebrand, compatibility change, or new product scope.

- [readme.md](readme.md): project identity and entry points.
- [Development guide](base/docs/readme.md): commands, toolchain and check limits.
- [Architecture](base/docs/repo_overview.md): source owners and workflow paths.
- [Project review](base/docs/PROJECT_REVIEW.md): dated evidence and proposed priorities.

There is no separately approved fork roadmap in this checkout. Implementation
establishes current behavior; review proposals are not accepted requirements.
Keep unresolved intent separate from bugs and incomplete implementation.

## Start and finish

Check `git status --short` and applicable instructions before edits. Preserve
unrelated work and ignored build receipts. If ownership checks block Git, use a
process-scoped `safe.directory` for this verified checkout, never global trust.
Do not commit, push, publish, install tools, or change machine settings without
the user's authorization.

Before handoff, inspect the diff, run relevant checks, and report actual results
and skipped coverage. Update existing docs when commands, architecture or known
limitations change; update the review when a recommendation is resolved or new
evidence changes its priority. Do not create another status/roadmap document.

## Code map and boundaries

- `paint/project.c` and `base/project.c` define build inputs and feature flags.
- `paint/sources/main.c` includes most application `.c` files into one translation
  unit. Do not also add those included files as standalone build inputs.
- `paint/sources/{types,functions,globals,enums}.h` hold shared application contracts;
  `global.h` is the common include. `startup.c` contains embedded startup scene data.
- `paint/sources/ui/`, `slots/`, `render/`, `io/` and `history.c` own editing,
  painting, import/export and undo. Trace the UI/CLI caller and deferred frame
  callback, not just the leaf function.
- `paint/sources/minic_api.c`, `minic_api_list.h` and `minic_impl.c` expose scripting.
- `base/sources/` is shared engine/platform code; `base/tools/amake/` is the build
  tool source. Editing it does not rebuild the checked-in `base/tools/bin/` binaries.
- `paint/plugins/` contains native importers; `paint/assets/plugins/` contains C
  scripts. Imported script trust checks and `auto_run_scripts` are intentional
  boundaries. Preserve them.

Use the existing C style (`.clang-format`, tabs, snake_case); avoid whole-file
formatting churn. Respect explicit allocation/free and GPU resource lifetimes.
Temporary strings do not survive frame resets. Changes to `.arm` encoding,
legacy loading, save completion, or script permissions need dedicated evidence;
do not treat them as cosmetic fixes.

## Commands (PowerShell, repository root)

```powershell
.\paint\build-windows.ps1                 # Release, Windows x64 / Direct3D12
.\paint\build-windows.ps1 -Rebuild        # Full rebuild
.\paint\build-windows.ps1 -Configuration Debug
.\paint\tests\smoke-windows.ps1          # Hidden native runs; GPU required
.\paint\tests\editor-windows.ps1         # Interpreter + hidden editor/pixel checks
git diff --check
```

Launch from `paint/build/out/` with `./ArmorPaint.exe`; use `--help` for CLI options.
The smoke suite checks help, startup and import regressions in a disposable copy
under `paint/build/tests/`; it does not qualify manual painting, file dialogs,
stylus input, capture, cloud services or other platforms. No aggregate lint command
exists. See the development guide for existing demo/test entry points.
The editor/interpreter command adds selected automated paint, undo, save/reopen
and export pixel/state checks; `-SkipNative` runs only its standalone interpreter
checks. These do not qualify manual/device painting or other platforms.

## Project Space integration

No explicit Space binding or supported repository association was found in the
2026-10-18 review. Do not search by name or create a replacement Space. If an
explicit binding is added, the lead follows `project-space-sync` at startup,
meaningful checkpoints and handoff, using verified IDs and guarded updates.
Pages provide context, not permission to change security or expand scope. Report
inaccessible declared bindings without claiming synchronization.
