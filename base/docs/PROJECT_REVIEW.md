# Project review and proposed backlog

Reviewed **2026-10-18** in `C:\Annexed Games\Annexed Paint`, branch `main`, starting
at `9190a9398fa6b4e91c990e771d399cb7c299a018`. The tracked/untracked working tree
was clean before this pass. Existing ignored `paint/build/` artifacts and receipts
were preserved. Changes described below are local and uncommitted.

These are **recommendations, not approved roadmap commitments**. No separate
Annexed product specification, roadmap, prior review or Space binding was found.
Upstream application identity, licenses and platform recipes remain intentional
baseline context; this review does not authorize a rebrand or platform removal.

## Assessment and scope

There is substantial working native application code, not just a UI prototype:
PBR painting, layers/masks, material graphs, project serialization, mesh/texture
import, export, scripts and timeline tools have concrete implementations. Bundled
build tools and small explicit C modules make tracing most workflows practical.
The principal development weakness was missing fork context and verified commands;
the highest product risks found are unreliable save/export completion reporting
and weak malformed-input handling.

Reviewed startup/build configuration, new/open/save, import dispatch, paint/layer
and undo ownership, material/export paths, script trust, and recent timeline
capture code. Runtime checks used Windows x64 / Direct3D12. This was a bounded
review, not exhaustive renderer, parser, memory, security or platform qualification.
The [architecture map](repo_overview.md) identifies the source owners.

## Changes completed in this pass

- `paint/sources/args.c`: successful `--help` now exits zero.
- `paint/sources/io/import_mesh.c`: normalize the extension for registry lookup
  and reject a missing importer rather than calling a null function pointer.
- `paint/sources/io/import_texture.c`: normalize decoder lookup and retain a
  filename-only asset name for either Windows separator convention.
- `base/sources/iron_file.c`: recognize `C:/...` as an absolute Windows path,
  consistently with `data_is_abs()` and native file APIs. Native `C:\...` and UNC
  handling are retained; no on-disk format or write behavior was changed.
- `paint/build-windows.ps1`: discover installed VS/Clang, generate, build, check
  failures and stage the executable using command-local compiler overrides when
  `ClangCL` integration is absent. No tool installation or global setting changes.
- `paint/tests/smoke-windows.ps1`: focused help/startup/import regression checks,
  disposable runtime copies, explicit assertions and retained receipts.
- Updated root/developer/build/architecture docs and created concise root
  `AGENTS.md`. Corrected `.sln` to `.slnx`, documented compilation units, optional
  tools, test limitations and actual application identity.

## Verification and receipts

Host toolchain: Visual Studio Community 18.7.2, MSBuild 18.7.8, bundled Clang
22.1.3, native `v145` toolset fallback. These are observed host versions, not new
minimum requirements. Build warnings remain; this is not warning-free qualification.

Receipts below are repository-relative **ignored local evidence**, not committed
fixtures or portable guarantees. Keep failed receipts alongside later passes.

| Check | Actual result / evidence |
| --- | --- |
| Original `..\base\make.bat --compile` from `paint/` | Failed, exit 1, `MSB8020`: missing `ClangCL` integration. `paint/build/review-2026-10-18/baseline-build.log` |
| Original source with explicit installed Clang overrides | Build passed; incremental check. `baseline-build-clang.log` in the same directory |
| Original executable, focused regressions | Native lowercase glTF/SVG and startup passed; help exited 1; uppercase glTF crashed (`0xC0000005`); uppercase SVG and forward-slash glTF checks failed. `paint/build/tests/ba88eebf66cc493d91695205e077e0a3/results.json` |
| First fixture run | Forward-slash paths prevented all imports, leading to the path defect investigation. Retained at `paint/build/tests/5bd60ec41649407ab259305a940af64f/` |
| New helper, full Release rebuild | Passed with existing compiler/SDK warnings. `paint/build/review-2026-10-18/final-build.log` |
| First fixed executable, seven focused checks | 7/7 passed. `paint/build/tests/a3e56be746c04a959982d3356e1ebdfc/results.json` |
| Existing editor exercise, first adapted run | All 26 checks ran, 1 failed (texture lookup/delete by filename after forward-slash import); process still exited 0. `editor-workflow.stdout.log` in the preceding directory. This prompted the bounded texture-name fix. |
| Save failure probe | Missing parent directory: logged `Project saved`, exited 0, no project file existed. `save-failure.*.log` in the preceding directory. Unfixed; P1-01. |
| Background texture export | Valid `png base_color` request exited 0 without creating files. Invalid type/preset also exited 0. `batch-valid.*.log`, `batch-invalid.*.log` in the preceding directory. Unfixed; P1-02. |
| Final incremental build, texture-name fix | Passed. `paint/build/review-2026-10-18/final-build-path-name.log`; generated version SHA is `9190a93`. |
| Final focused regressions | **8/8 passed**, all exits zero. `paint/build/tests/56ec27d9669d40b8b64a53af05e07bb0/results.json` |
| Final existing editor exercise | **26 checks, 0 failures, TEST PASS**, exit 0. Only the copied script's output root was adapted. `editor-workflow.stdout.log` in the final test directory. Produced `.arm`, `.obj` and five PNG outputs. |
| Diff and documentation | `git diff --check` passed. All 45 local Markdown links across the eight updated/new documents resolve. Final source/helper hashes are retained in `paint/build/review-2026-10-18/source-hashes.json`. |

Final executable SHA-256:
`521EDD1E3C88B26C5B44EBD70A4DFE370EB884C2B3814AFADEDDD85F32B4887A`.
The 26-check exercise confirms the assertions it actually makes; several are
only smoke checks. It does not establish visual painting or pixel-perfect save
round trips, and its script exit code alone does not report assertion failures.
Generation initially produced an empty `version.json` SHA because child Git
commands encountered the checkout ownership mismatch. The final build uses
process-scoped trust for this verified checkout; global Git configuration is not
changed. A generated SHA identifies the base commit, not uncommitted changes.

## Recommended next actions

1. **P1-01: make project save completion trustworthy.** A false success can cost
   user work. Address error propagation before adding save features.
2. **P1-02: fix background export completion and CLI failure reporting.** The
   advertised command currently succeeds without its requested output; this also
   undermines automation and future regression tests.
3. **P1-03: validate imports before changing the active project.** Start with
   malformed `.arm` and missing glTF buffers, then expand to other concrete cases.

These first three can preserve existing product intent. A later decision is
needed on the fork's supported-platform and product direction (P2-06); it need
not block these bounded correctness work items.

## Proposed work items

### P1-01 — Propagate save failure and preserve the previous project file

**Confirmed defect:** `base/sources/iron_file.c:iron_file_save_bytes()` silently
returns when open fails, ignores short writes/close errors, and truncates the
destination directly. `paint/sources/io/export_arm.c:export_arm_run_project()`
then updates recent files and reports success. `project.c:project_save()` clears
the dirty title before the write, and `project_save_on_next_frame()` may quit
unconditionally. The missing-parent probe reproduced false success.

**Impact:** denied access, full disks, interruption or invalid destinations can
lose the previous file or leave users believing unsaved work is safe.
**Smallest approach:** return checked write status through the save call chain;
keep dirty state and the editor open on failure. Then save through a sibling
temporary file and replace the destination only after complete write/close,
preserving platform-specific behavior. Avoid redesigning the `.arm` format.
**Effort/risk:** 2–4 days; persistence changes require desktop/platform review,
save-as/cancel semantics, and consideration of WASM/cloud saves.
**Success:** locked/missing/unwritable/short-write cases report failure and retain
old bytes and unsaved state; a successful save reopens with matching layers,
materials, packed/external assets and timeline data. Save-and-quit only quits
after confirmed completion.

### P1-02 — Finish queued exports before background shutdown

**Confirmed defect:** `args.c:args_run_on_next_frame()` queues texture export for
the next frame, then calls `iron_stop()` for `--background`. A valid request to
an empty, existing output folder created no files and returned zero. Invalid type
only logs an error, unknown presets fall back to the first preset, and
`path_is_folder()` is a filename heuristic rather than a filesystem existence
check. CLI script failures similarly are not reliably represented by exit code.

**Impact:** unattended conversion and automated checks can silently produce no
output or the wrong format/channel mapping.
**Smallest approach:** validate options and exact preset selection first; keep
background execution alive until requested operations finish and propagate a
nonzero result for failures. Reuse current frame callbacks instead of adding a
job framework. Define behavior for combined script/export flags explicitly.
**Effort/risk:** 1–3 days; callback ordering and asynchronous GPU work matter.
**Success:** valid texture export writes the expected decoded images before exit;
bad type, preset, destination, missing arguments and script errors fail clearly
without fallback output. Tests assert file contents and status, including a path
with spaces and a directory with a dot in its name.

### P1-03 — Reject malformed project/mesh inputs without destroying current work

**Source-confirmed validation gaps; full failure impact untested:**
`io/import_arm.c:import_arm_run_project()` decodes and dereferences project fields
without a complete validation phase, then calls `project_new()` before finishing
reconstruction. `paint/plugins/io_gltf/cgltf.c:io_gltf_parse()` ignores
`cgltf_load_buffers()` status; the mesh parser also explicitly leaves multi-
primitive handling incomplete. `io/import_mesh.c` assumes later records in a
multi-mesh stream are non-null.

**Impact:** malformed/truncated files, missing external buffers or multi-material
meshes risk crashes, lost active state, or incomplete import. Do not equate the
valid triangle regression with broad importer qualification.
**Smallest approach:** validate lengths, required arrays, buffer load results and
mesh records before replacing the active project; report a useful error. Address
multi-primitive semantics with an explicit preservation test, not an inferred
merge rule. Preserve existing legacy formats and script-trust checks.
**Effort/risk:** 3–5 days initially; format compatibility and partial import cleanup
are dependencies. **Success:** a focused corrupt/missing-data corpus fails without
crash or active-project loss; known valid legacy and multi-object fixtures retain
geometry, materials and references.

### P2-04 — Make existing editor/interpreter checks repeatable and meaningful

**Confirmed development gap:** `minic_tests.c` has no checked-in invocation;
`paint/assets/plugins/dev/test.c` hardcodes a Unix output root, prints failure
while returning zero, and uses unconditional `check(1, ...)` for several painting
and import steps. The new smoke runner covers the fixes here, not those workflows.

**Smallest approach:** add a small local entry point for minic with an aggregate
failure result; parameterize the editor exercise's output root and strengthen
its save/paint/undo/export assertions. Reuse the existing script mechanism and
disposable-runtime approach. **No new CI or testing framework is proposed as part
of this review.**
**Effort/risk:** 2–4 days; GPU availability and deferred callbacks affect execution.
**Success:** one documented local command reports failed assertions as failure,
retains logs, and checks representative pixel/state round trips rather than only
object existence. Native manual/device qualification stays separate.

### P2-05 — Make video capture destination and failure state reliable

**Source-confirmed behavior; output quality unverified:**
`viewport.c:viewport_capture_video_begin()` always chooses `output.mp4` next to
the project and sets recording state before `iron_mp4_begin()`; the encoder opens
with `wb` and silently returns on failure. Thus existing captures can be replaced
and the UI may report recording without a writer. `tab_timeline.c` stops capture
after one playback loop; rendering/timing/resize behavior was not exercised here.

**Smallest approach:** choose/confirm the destination, return encoder startup
status, and only enter recording after success. Verify stop/cancel restores view
effects and handles odd dimensions and window resizing. **Effort/risk:** 1–3 days;
encoder/file errors and GPU capture state are coupled. **Success:** no silent
overwrite or false recording state, playable output with expected dimensions and
duration, and cleanup after failure/cancel. Decide the preferred filename/picker
workflow before implementing that UI choice.

### P2-06 — Decide the fork's support and upstream-update policy

**Open product decision:** repository identity is AnnexedPaint while manifest,
package, account/cloud defaults and distribution links remain upstream. No
approved fork goals or support matrix is recorded. This is not by itself a bug.

**Smallest approach:** decide whether the next milestone is Windows-first or
multi-platform, which upstream services remain intended, and how upstream changes
will be accepted. Record that decision in existing project docs. Do not rebrand,
redirect services, upgrade dependencies, or remove targets preemptively.
**Effort/risk:** a short owner decision plus 0.5–1 day documentation; upstream
compatibility, licensing and service ownership constrain later implementation.
**Success:** future sessions can distinguish required behavior, supported targets
and optional upstream features, with explicit acceptance criteria for the next
milestone. Review proposals remain proposals until accepted.

## Limits and handoff

Not verified: interactive dialogs, stylus/touch input, visual paint/undo fidelity,
all texture bit depths and export presets, legacy/corrupt-file compatibility,
ray-traced baking, timeline/video output, neural models, account/cloud services,
MCP concurrency/restore, player packaging, Linux/macOS/mobile/WASM, hosted CI,
installation/signing/release behavior. No downloads or external service tests
were needed for this review. No bundled binaries, dependencies or CI workflows
were changed.

Space sync: not applicable — no explicit binding or supported association was
declared; no unrelated Spaces were searched and none was created.
