# Project review and proposed backlog

Reviewed **2026-10-18** in `C:\Annexed Games\Annexed Paint`, branch `main`, starting
at `9190a9398fa6b4e91c990e771d399cb7c299a018`. The tracked/untracked working tree
was clean before this pass. Existing ignored `paint/build/` artifacts and receipts
were preserved. The initial review changes are recorded in `43a52c886` and P1-01
in `e0bb782450b8a808d565df2c17443b1e2755c3e9`. P1-02 started from that clean
`main` checkout; its implementation, tests and documentation remain uncommitted.
P1-03 began at the same base with that uncommitted P1-02 work present and
preserved; the P1-03 implementation and evidence below are also uncommitted.

These began as **recommendations, not approved roadmap commitments**. P1-01,
P1-02, P1-03 and P2-04 were subsequently explicitly requested for implementation;
the other items remain proposals. No separate
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
| Save failure probe | Missing parent directory: logged `Project saved`, exited 0, no project file existed. `save-failure.*.log` in the preceding directory. Baseline failure retained; addressed by the P1-01 follow-up below. |
| Background texture export | Valid `png base_color` request exited 0 without creating files. Invalid type/preset also exited 0. `batch-valid.*.log`, `batch-invalid.*.log` in the preceding directory. Original review failure; addressed by P1-02 below. |
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

## P1-01 implementation and verification (2026-10-18)

**Implemented:** native project save failures now propagate to the deferred
completion callback. A failed write leaves the previous destination bytes, dirty
title, active Save As filepath and recent files intact, and does not honor
save-and-quit. Only a completed project write commits the new filepath/title,
records the recent file, reports `Project saved`, and optionally quits.

- `base/sources/iron_file_write.c` owns the checked boolean byte writer and
  native project replacement. It exclusively creates a unique sibling, checks
  full write, flush, filesystem sync and close, then replaces the destination
  using `MoveFileExW` on Windows or `rename` on POSIX. Failure never falls back to
  truncating the destination. Completed failures clean up their own sibling;
  process interruption can leave a partial sibling for recovery.
- `paint/sources/project.c` includes the separately testable `project_save.c` in
  the existing application translation unit. Each deferred request owns its
  destination and quit intent; a new project invalidates an old queued request.
  Save As/cancel preserves the active filepath until success. Player export/run
  also stops its continuation when its project write fails.
- Save As asset references now use the selected destination. The round-trip
  fixture exposed mixed Windows separators producing an unusable relative asset
  reference; serialization now normalizes both paths, and scripted project reopen
  normalizes its Windows path. The `.arm` fields/encoding and script-trust gates
  are unchanged.
- Browser disk/cloud routing is retained, but the existing bridge acknowledges
  dispatch rather than completion. Such requests report **unconfirmed**, keep
  unsaved state and stay open. Cloud acknowledgements, successful browser
  save-and-quit and project thumbnails after confirmed cloud completion still
  require a backend completion contract; no cloud success is claimed here.

The repeatable command is `./paint/tests/save-windows.ps1`; `-SkipNative` runs
the writer/lifecycle/interruption checks without a GPU. See the
[development guide](readme.md) for scope and retained receipts.

| Check | Actual result / ignored local evidence |
| --- | --- |
| Final Windows x64 / Direct3D12 Release build | Passed with existing warnings. `paint/build/review-2026-10-18/p1-01-final-6486e420dde04141a89e0bf492c4cfd3/build.log` |
| Production byte-writer harness | **23 assertions, 0 failures**: exact bytes/lengths, UTF-8/spaces, real lock/read-only failures, injected open/short-write/flush/sync/close/replace failures, exclusive sibling collision and cleanup. `paint/build/tests/save-cac8c977aa6e436099dd12e0a0d712c5/file-save/test.log` |
| Production deferred lifecycle harness | **11 assertions, 0 failures**: dirty/title/recent state, retry, Save As failure/success/cancel, first-save cancel, captured quit intent and superseded request. `project-save/test.log` in the same run directory. Dialogs/writer outcomes are controlled test doubles. |
| Interrupted sibling write | Expected child exit **73**, original bytes retained, exactly one five-byte partial sibling retained. `interrupted/` and `results.json` in the same directory. This is process termination, not physical power-loss qualification. |
| Full focused save suite | **16/16 checks passed**, no timeouts; native missing-parent/locked/read-only errors, successful new/replacement saves, save-and-quit, reopen with two layers/a named material node/input scale/timeline transform, external/packed SVG textures, and cleanup. `results.json` in the same directory. Packed reopen succeeds with the source texture moved offline. |
| Save/reopen pixel evidence | All **five PNG channel files match SHA-256** before/after fresh-process reopen, for both external and packed fixtures; hashes are in `pixels-0`/`pixels-1` results. Paths include spaces. This covers the selected fixture/preset, not every layer or texture format. |
| Existing native import/startup smoke suite | **8/8 passed**, exits zero. `paint/build/tests/fd547ee3d824436cb7f8ab22ec96ae25/results.json` |
| Browser source syntax | Both new modules passed `wasm32` syntax checks using bundled browser headers. `wasm-syntax.json` in the final build receipt directory. This is not a WASM link, browser run or cloud-service check. |

Final P1-01 executable SHA-256:
`B4F1AEBC504834764457BFBD49F1D085D8803F9ACB1995048E8462C1C3614947`.
Generated base SHA is `43a52c8`; `source-hashes.json` in the final build receipt
directory identifies the uncommitted implementation, tests and documentation.

**Retained failures and qualification boundary:** the first native fixture was
rejected by the interpreter for an incorrect layer field name
(`save-3bb74c45236545aeb11c1af7ffef9b6d`). The next run exposed the real external
asset/reopen separator defects (`save-4fca80093498495eb915967bc1f53ffd`), which are
fixed. Early callback-harness compilation/link attempts remain in the
`save-flow-*` directories; the final harness directly includes the save lifecycle
without the engine entry point. An intermediate RGB-output fixture failed its
color assertion (`save-7534e424bfe143fabeee1b56ddcf23b9`); that scripting/UI path
is not qualified by the final named noise-node/input-scale fixture.

Runs relying on **later automatic background-script shutdown** intermittently
exited `0xC0000005` after save/content assertions passed, including
`save-1febfa3bc4854fbb98d05a245b75a624` and
`save-d3c98ac9dd5146609a157041bc473ee6`. Four isolated packed replacements passed
(`packed-exit-fabe8644f616493781c8f0edd48cdec3`); that does not erase the failed
aggregates. Debug-wrapper attempts timed out without establishing a crash cause
(`save-d77566dd66444e5c969d68f3d40c0915`,
`save-5c25ae0dd56c467da4a3a451dab45f84`). That P1-01 fixture used
`script_quit()` after its assertions as a temporary bypass. P1-02 below identifies
and fixes an encoded-image heap overrun and removes that bypass. The separate
watchdog still verifies native save-and-quit. Native process exit zero, the
original 30-second limits and all failed receipts are retained.

Not qualified: interactive file pickers/device painting, real full disks or
power loss, special filesystem/link/ACL metadata, legacy compatibility, native
POSIX/mobile runs, a full WASM build, or browser/cloud completion. Controlled
write failures and selected native round trips are the acceptance evidence here.

## P1-02 implementation and verification (2026-10-18)

**Implemented:** background texture export completes before shutdown. Type,
existing destination directory and exact preset selection are validated before
running the import/script/export sequence. Invalid or missing options, conflicting
exports/API flags, missing scripts, interpreter errors (including later frame
callbacks), nonzero script results and texture write failures return **1**.
Successful completion returns **0** through the native entry point without a
modal error dialog. Unknown presets produce no fallback output.

- `args.c` uses the existing frame callbacks. With `--script --export-textures`,
  the script runs first on the opened project; its finite next-frame/update
  callbacks finish before CLI export settings are applied and the export runs.
  An explicit script quit before the requested texture export completes fails.
  The [development guide](readme.md) records the combined-flag contract and command.
- PNG/JPEG writers check encoding, each write, flush and close. EXR uses the
  existing checked byte writer. Texture export aggregates failures, returns a
  boolean and suppresses `Textures exported` on failure. Script writer/export
  declarations expose the boolean results. A partial multichannel failure can
  leave earlier channel files; it returns failure and identifies the failed file.
- CLI EXR conversion now tolerates undo textures that have not been allocated.
  The first focused native run exposed this separate null-history dereference.
- **Automatic-exit cause:** `iron_image.c:_encode_image()` allocated `w*h*4`
  bytes, then copied variable-sized codec chunks without a capacity check. The
  packed 4x4 SVG fixture produces a **78-byte PNG in a 64-byte allocation**; a
  1x1 PNG needs **70 bytes in a 4-byte allocation**. These files can decode and
  pass content assertions while the heap is already corrupted. The encoder now
  owns a local buffer, checks/grows capacity before every chunk and initializes
  its length/capacity. Encoding/size/allocation failures return no encoded buffer.
  The `.arm` encoding and script permission boundaries are unchanged.

The original executable reproduced **3 access violations in 12** packed-save
runs. A debug-heap-disabled diagnostic run captured a post-assertion access
violation during `combase`/CRT process teardown. COM cleanup alone did not resolve
it: a later aggregate still crashed. That exploratory cleanup was removed from
the final patch after the guarded encoder test established the overwrite. No
renderer or COM lifecycle fix is claimed. Retained receipts are ignored local
evidence; later passing runs do not erase them.

| Check | Actual result / ignored local evidence |
| --- | --- |
| Final Windows x64 / Direct3D12 Release build | Passed with existing warnings. `paint/build/review-2026-10-18/p1-02-final-db2b92fdde3c48c582f20d7f61473db4/build-final.log` |
| Original packed-save automatic exit | **3/12 access violations**, no timeouts. `paint/build/tests/p1-02-crash-699852de36b142488ebc99f75bce3a43/results.json`; teardown stack in `heap-debug-7.stdout.log` in that directory. Normal debug-heap runs masked the failure. |
| Original encoder with guarded allocator | **13 assertions, 7 failures; 6 overwritten guards**, expected exit 1. All six PNG/JPEG files still decoded. `paint/build/tests/encode-original-0f25c0b1489d42568f464780679e1817/test.log`. This directly includes the original encoder extracted from base commit `e0bb782`, with guarded heap storage. |
| Fixed production image writer and encoder | **10 writer assertions + 13 encoder assertions, 0 failures**. Short-write/flush/close errors, missing directory, small and incompressible PNG/JPEG, decoded dimensions/color and intact allocation guards. `paint/build/tests/export-af42569ade274ebaa6f73a4d204c3771/` (and `export-b0761e0bdb7444afb5d49c82383f542c/`). No GPU is required. |
| Full focused export and shutdown suite | **74/74 checks passed**, no timeouts (`-ShutdownRepeats 24`): unit writer/encoder, `--help`, script writer status, direct/scripted 2048x2048 PNG/JPEG/EXR16/EXR32 exports with independent decode/pixel checks, exact `generic` five-channel output, 14 invalid CLI/combination cases, 6 script/callback/early-quit failure cases with zero fallback files, 4 locked-file preservations, partial multichannel failure evidence, and **24/24 automatic packed-save exits**. `paint/build/tests/export-165a405941644e6c9abff0973ccd8c8c/results.json` (and `export-aaff145c306540dfaafca6738093a7ee/results.json`). |
| Full save suite with automatic CLI shutdown | **16/16 checks passed**, no timeouts, with `script_quit()` removed from `save-workflow.c.in` so all native save/reopen/pixel checks exit via normal CLI completion. `paint/build/tests/save-cbea75e76f684de19a889a40e839972a/results.json` (and `save-573975206cb0456faf1d0e99c0b35fe1/results.json`). |
| Existing native import/startup smoke suite | **8/8 passed**, exits zero. `paint/build/tests/8308bf47e2e54a6a97e98b85f752c2a2/results.json` (and `202db4dd36c8440a9679b7285436ffb3/results.json`). |
| Browser source syntax | All four touched C modules (`iron_image.c`, `args.c`, `export_texture.c`, `util_layer.c`) passed `wasm32` syntax checks with bundled `miniclib` headers after guarding `ferror`/`fflush` in `_write_image` with `#ifndef IRON_WASM`. `wasm-syntax.json` in the final build receipt directory. This is not a WASM link or browser run. |

Final P1-02 executable SHA-256:
`6DB4912F16F1CFE2EDD88FD134BCCFD235347567BD309F7F5EEE86B6750209F5`.
Generated base SHA is `e0bb782`; `source-hashes.json` in the final build receipt
directory identifies the uncommitted implementation, tests and documentation.

Additional retained failures: `export-791c307abb234c629081afd8cf64563b` failed
to compile `export-inspect.c` before renaming its `u32` helper and switching to
`stbi_load_from_memory`. `export-db4c01e4ea594416b3843da7eeb1c2ee` exposed
the CLI EXR null-history defect and two invalid runtime-error fixtures (division
by zero evaluates to zero in this interpreter; array bounds errors now exercise
the real error path). `export-58fe9a7fce7849ea87d7f65dcb5c12fa` retained another
post-assertion automatic-exit crash before the encoder fix and a new test fixture
that incorrectly accessed a private layer field. Its corrected fixture uses the
existing buffer API. Initial allocator instrumentation exhausted its tracking
slots (`export-f0823188c9144b938a54f23e6d594b72`,
`encode-baseline-7240a2bbff6b4ec8a328da07d49ff337`); the bounded test allocator was
corrected before the complete original/fixed comparison above. The first
baseline setup also lacked an SVG copy; the complete 12-run crash cohort supplied
the fixture explicitly. Initial `wasm32` syntax probes failed until `-nostdlibinc`
and the `#ifndef IRON_WASM` guard in `iron_image.c` were applied. Running the
PowerShell harness under Windows PowerShell 5.1 exposed `/Fo:` trailing-backslash
quoting (`export-57e9726d007e42249771495bfb9f38ac`,
`save-aa8b5b26252847d58803e971f78de1c9`) and uncached `Start-Process` exit codes
(`export-31c72bf92d5345abb8f9a269b859f429`) before `export-windows.ps1` passed an
explicit `.obj` path and cached `$process.Handle`. These setup failures are not
product passes.

Not qualified: interactive painting/dialogs/stylus input, audio playback, every
texture preset/resolution, physical disk/power-loss behavior, malformed project
imports (P1-03), other native platforms or browser/cloud completion. The focused
Windows checks cover the selected CLI workflows and the demonstrated heap overrun.

## P1-03 implementation and verification (2026-10-18)

**Implemented:** project import stages checked data before changing the active
project. Rejected files report an error containing the path and retain the
project, mesh/layer/material resources, dirty title, filepath and recent files.
Registry mesh import validates every record before replacing or adding objects;
a null or invalid later record rejects the stream. Invalid project/plug-in mesh
CLI inputs return **1** before running the requested script/export sequence.

- `io/import_arm_validate.c`, included by `import_arm.c` in the existing app
  translation unit, decodes by field name into owned staging allocations. It
  checks file bounds, counts/types, duplicate fields, required arrays, mesh
  layouts/indices, transforms, material/object references, hierarchy cycles,
  layer dimensions/bit depths and exact LZ4 sizes before `project_new()`.
  Failed decoder staging allocations are released. Open, append, material,
  brush and swatch entry points use the checked decoder. No standalone build
  input was added and `.arm` encoding was not changed.
- Format versions **0-19**, the historical trailing zero byte and old
  `fill_layer` reference remain supported. Compatibility defaults follow
  `import_legacy.c`; generated fixtures test them. Two-component brush UV
  vectors remain valid. Imported-script trust checks and `auto_run_scripts`
  routing remain in their existing owners after validation.
- `iron_lz4.c` bounds literal/match lengths, offsets and output size before
  copying. The same checked reader supports allocation-free preflight and the
  subsequent decoder. Mask/paint/sculpt and timeline payloads are checked
  against their expected byte counts.
- glTF checks buffer-load status, accessor bounds/counts, triangle indices and
  node cycles before emitting geometry; failure resets reader iteration.
  **Multiple primitives are explicitly rejected for the whole file.** The
  native preservation fixture covers that rejection. The existing raw-mesh
  contract has no primitive/material mapping; no merge rule, material splitting
  or glTF material-definition import was inferred. Sparse accessors and
  non-triangle primitives are also rejected.

The repeatable command is `./paint/tests/import-windows.ps1`; `-SkipNative`
runs the decoder, compression, staged-stream and glTF checks without a GPU.
See the [development guide](readme.md) for coverage and unsupported inputs.

| Check | Actual result / ignored local evidence |
| --- | --- |
| Windows x64 / Direct3D12 Release build | Passed with compiler warnings. `paint/build/review-2026-10-18/p1-03-e3d4f933f3ef4b479de849e543f92006/build-final.log` |
| Production decoder, stream and compression harnesses | **51 decoder + 7 stream + 11 LZ4 assertions, 0 failures** in the final fixture invocation. Every truncated prefix ends at an unreadable guard page; every decoder allocation failure must release staging. Includes reordered/duplicate/mistyped fields, oversized counts and overflow dimensions, invalid references/cycles, short transforms, bad indices and corrupt LZ4. The stream owner/canvas encoder are extracted unchanged from production, with hashes; importer and scene commits are controlled callbacks. `paint/build/tests/import-f9c0d41f715f417990ab1d61555e9422/` |
| Focused import suite | **65/65 checks passed**, no timeouts. Includes two bundled mesh scenes; seven corrupt/unsupported glTF cases followed by valid two-object reads; generated versions 0-19 in the decoder; native rejection preserving populated state; missing-buffer append; and two CLI failures with **zero export files**. `results.json` in the same run directory |
| Valid native project and compatibility fixtures | Three objects, both named glTF triangles, paint layers, a named material and packed texture reopen. Independent decoding verifies object/material mapping; native reads verify both live triangle meshes. Generated versions **4, 9, 18, 19** also reopen; versions 0-6 use `fill_layer`. These are generated compatibility fixtures, not historical archived project files. `saved.arm`, `valid-version-*.arm`, `project-import-test.log` and per-read logs in the same directory |
| Existing save suite | **16/16 passed**, all native timeout fields false; all five PNG channel hashes match before/after reopen for both external and packed texture fixtures. `paint/build/tests/save-e1ea849898654188b541bffce355dc2d/results.json` |
| Existing export/shutdown suite | **62/62 passed**, no timeouts, including the default **12/12 automatic packed-save exits**. `paint/build/tests/export-d5b9ac99d2bf4141a5810473d07da831/results.json` |
| Existing startup/import smoke suite | **8/8 passed**, exits zero, no timeouts. `paint/build/tests/1578591b6c3a479aa621e45f15220990/results.json` |
| Browser source probes | `import_arm_validate.c`, `import_arm.c`, `import_mesh.c`, `args.c` and `iron_lz4.c` passed `wasm32` syntax with bundled headers. The native glTF module and its original HEAD version both fail that probe on existing missing `strrchr`/`atoi` declarations in the minimal headers. Initial/final probe logs and `wasm-syntax.json` are in the build receipt directory. No full WASM link or browser run is claimed. |

Final P1-03 executable SHA-256:
`93EF97EBFE6E878CF826EAEDF583EE82F4E05FE417E33411DCAE9905BF8BC3BB`.
Generated base SHA is `e0bb782`; `source-hashes.json` in the build receipt
directory identifies the full uncommitted implementation, tests and documentation,
including the preserved P1-02 work. `starting.diff` records the starting changes.

**Retained failures:** the first build placed the append error message in the
wrong callback (`build-1.log`); subsequent builds corrected it. Initial standalone
harness compilation/link failures remain in `import-b8574553a1134be0903e487e78381c28`,
`import-ff34a73d31fe4ddf86106710e480e433`,
`import-41461e3033694623a1cace388f75e380` and
`import-a5ee9e0421be48e8afa5bd68de6759c6`. Early native fixtures accessed script
fields that are not exposed (`import-78c89abdeae84baeaaf6c4a2f1083cf9`,
`import-c205008afc294ca18267261ddec92f6e`); raw default material/brush canvases
were also incorrectly treated as project files in the first harness. The first
populated native fixture exposed an overly strict three-component vector check
on valid brush UV data (`import-94ac6a19a7d7418da42f0f9f2dc32624`), now corrected.
The later `import-d23622348c58444897ac3b686277585e` passed all native preservation
and reopen cases but its two CLI tests used the wrong argument order; the final
run uses the documented order and requires the import failure message. These
receipts are retained and are not product passes.

Not qualified: every malformed format/codec, archived legacy projects, OBJ/Blender/
FBX parser internals, GPU/resource exhaustion during reconstruction, interactive
dialogs/painting/stylus input, other native platforms, browser/cloud operation,
or broader glTF primitive/material semantics. The initial P1-03 safety boundary
is implemented with the focused cases above; multi-primitive import remains
explicitly unsupported rather than silently incomplete.

## P2-04 implementation and verification (2026-10-19)

**Implemented locally:** one command, `./paint/tests/editor-windows.ps1`, runs the
existing interpreter and editor exercises with aggregate failure status and
retained evidence. Work started from clean `main` at
`27ffe244744a25aa0b8463824f3edacf2845f342`; these implementation, test and
documentation changes remain uncommitted.

- `minic_tests()` now returns its failure count. All 39 cases check result and
  error-count behavior, including expected parser/runtime rejections. The native
  callback case asserts the received argument instead of only printing a message.
  The small standalone host uses production interpreter/math code with controlled
  scene and formatting bindings; this is distinct from editor API qualification.
- The existing editor script takes its output root/mode through
  `test_begin(output_root, mode)`. The runner supplies a fresh directory with
  spaces and a dot, generated entry points, matching runtime data and no copied
  user config. Failed script assertions use `script_assert()` and the existing
  CLI error path, including deferred callbacks. Success uses automatic CLI
  completion. History calls preserve the draw context; layer texture readback
  is exposed through its existing borrowed texture.
- The exercise checks 43 state/output assertions, then six more in a fresh
  process: layer name/count/opacity, named geometry and material node values,
  completed saves and replacement saves, texture delete/reimport and exported
  OBJ/material reimports. Independent PNG decoding checks every pixel for brush,
  eraser, world stroke, fill, undo/redo, save/reopen and repaint/resave behavior.
  Five texture channels round-trip exactly. An isolated reference layer checks
  known RGB/grayscale values; the neutral normal expectation follows the existing
  8-bit clear and whiteout blend. No renderer/encoding behavior was changed.

| Check | Actual result / ignored local evidence |
| --- | --- |
| Windows x64 / Direct3D12 Release compilation/link | Passed with existing warnings. `paint/build/review-2026-10-19/p2-04-final-5e0324a860a243b1932c13fc9b1dc415/build.log`; source and executable hashes are beside it. |
| Complete editor/interpreter command | **60/60 runner checks passed**, no timeouts. `paint/build/tests/editor-5156661914484de48fb25fd8f823ece0/results.json`; includes **39 interpreter checks**, **43 + 6 editor assertions**, decoded pixel comparisons and failure controls. |
| Observable painting and persistence | Brush changed **84,736** pixels, erasing reduced alpha on **18,819**, world stroke changed **5,539**, fill changed **2,727,681**, and repaint erased **4,160**. Undo, redo, saved-layer reopen, fresh-process replacement reopen and both five-channel export comparisons match exactly. Per-check decoder logs and output hashes are in the same run. |
| Deliberate failures | The minic control and deferred editor assertion return **1**; missing output, unequal images and wrong known values are rejected. The command itself with `-SkipNative -InjectFailure` returned **1**, retained at `paint/build/tests/editor-81a6f0023f154a2d823866260fcd8bdd/`. |
| Existing startup/import smoke suite | **8/8 passed**, exits zero. `paint/build/tests/9f4f7368723d4da7aa02483fb54424a1/results.json`. |
| Final diff/documentation/source check | `git diff --check` passed; all **27** checked local Markdown links resolve. All **14** recorded source hashes still match the final successful runner receipt. |

Tested executable SHA-256:
`E28A03E2CB57A6773012E69C5CBC64C3662810ADF805F453D299A6F431811CD7`.
The normal build helper compiled successfully but its final staging copy failed
because `paint/build/out/ArmorPaint.exe` was in use
(`p2-04-4320a6c866d54275983c0bae99bf8434/build.log` in the review directory).
The running editor was left alone. The final direct MSBuild check passed, and
native commands used `-Executable ./paint/build/x64/Release/ArmorPaint.exe`,
copied into disposable runtimes. The staged executable was not replaced.

Early standalone host/link, generic-container compiler-flag and memory-only PNG
decoder harness failures remain in `editor-8426d1e8285b46388489f2bf3080fd18`,
`editor-bbcfc5268ac14263b31be0297f2881d9` and
`editor-28f37df0db5e4592b74d1c8fe46a2c09` under `paint/build/tests/`.
`editor-2f7e8350e53d40c6a977a3c947dfc8d6` exposed a fixture assumption that removing
one RGB node removed the default RGB node too; the final check verifies node ID,
node count and link count. Expanded reference fixtures initially assumed RGB
storage for grayscale outputs and unquantized neutral normals
(`editor-c1c733a958864193acc769c14ab26b37`,
`editor-1d7c3e040ec7477387bed3bb08d944a1`). Those expectations were reconciled
with the preset, clear and shader source; failures and their original outputs
remain. The 30-second process limits and failure assertions were retained.

No CI/framework was added. See the [development guide](readme.md) for the command,
`-SkipNative`, explicit failure injection and binary selection. These selected
automated Windows/Direct3D12 fixtures do not qualify manual paint quality,
dialogs/stylus/device input, physics, every preset/bit depth, other platforms,
browser/cloud services or hosted CI. Earlier save/export/import qualification
limits remain in force.

## Recommended next actions

1. **P2-05: decide capture destination/confirmation behavior before its UI work.**
   The proposed bounded capture work remains as described below; no filename or
   picker policy has been selected by the completed P2-04 test work.

P1-01, P1-02, the bounded P1-03 changes and P2-04 are implemented with the evidence and
limits above. A later decision is
needed on the fork's supported-platform and product direction (P2-06); it need
not block these bounded correctness work items.

## Proposed work items

### P1-01 — Propagate save failure and preserve the previous project file

**Status:** native implementation complete; focused Windows checks passed as
recorded above. Browser acknowledgement and remaining platform/runtime
qualification are explicitly limited above.

**Original confirmed defect:** `base/sources/iron_file.c:iron_file_save_bytes()` silently
returns when open fails, ignores short writes/close errors, and truncates the
destination directly. `paint/sources/io/export_arm.c:export_arm_run_project()`
then updates recent files and reports success. `project.c:project_save()` clears
the dirty title before the write, and `project_save_on_next_frame()` may quit
unconditionally. The missing-parent probe reproduced false success.

**Impact:** denied access, full disks, interruption or invalid destinations can
lose the previous file or leave users believing unsaved work is safe.
**Implemented approach:** return checked write status through the save call chain;
keep dirty state and the editor open on failure. Native saves use a sibling
temporary file and replace the destination only after complete write/flush/sync/
close, without redesigning the `.arm` format. Platform limits are recorded above.
**Effort/risk:** 2–4 days; persistence changes require desktop/platform review,
save-as/cancel semantics, and consideration of WASM/cloud saves.
**Success:** locked/missing/unwritable/short-write cases report failure and retain
old bytes and unsaved state; a successful save reopens with matching layers,
materials, packed/external assets and timeline data. Save-and-quit only quits
after confirmed completion.

### P1-02 — Finish queued exports before background shutdown

**Status:** implemented; Windows results, retained failures and qualification
limits are recorded in the P1-02 section above. The temporary explicit-quit test
bypass has been removed.

**Original confirmed defect:** `args.c:args_run_on_next_frame()` queues texture export for
the next frame, then calls `iron_stop()` for `--background`. A valid request to
an empty, existing output folder created no files and returned zero. Invalid type
only logs an error, unknown presets fall back to the first preset, and
`path_is_folder()` is a filename heuristic rather than a filesystem existence
check. CLI script failures similarly are not reliably represented by exit code.

**Impact:** unattended conversion and automated checks can silently produce no
output or the wrong format/channel mapping.
**Implemented approach:** validate options and exact preset selection first;
chain existing script/export frame callbacks, aggregate checked writer results,
and return nonzero for failures. Grow encoded-image buffers before copying codec
chunks to prevent packed-asset heap corruption. Combined script/export behavior
and the dedicated regression command are documented in the development guide.
**Effort/risk:** 1–3 days; callback ordering and asynchronous GPU work matter.
**Success:** valid texture export writes the expected decoded images before exit;
bad type, preset, destination, missing arguments and script errors fail clearly
without fallback output. Tests assert file contents and status, including a path
with spaces and a directory with a dot in its name.

### P1-03 — Reject malformed project/mesh inputs without destroying current work

**Status:** staged validation implemented; focused Windows results and remaining
input/platform limits are recorded above. Multiple glTF primitives are explicitly
rejected until a preservation contract is defined.

**Original source-confirmed validation gaps; full failure impact untested at review:**
`io/import_arm.c:import_arm_run_project()` decodes and dereferences project fields
without a complete validation phase, then calls `project_new()` before finishing
reconstruction. `paint/plugins/io_gltf/cgltf.c:io_gltf_parse()` ignores
`cgltf_load_buffers()` status; the mesh parser also explicitly leaves multi-
primitive handling incomplete. `io/import_mesh.c` assumes later records in a
multi-mesh stream are non-null.

**Impact:** malformed/truncated files, missing external buffers or multi-material
meshes risk crashes, lost active state, or incomplete import. Do not equate the
valid triangle regression with broad importer qualification.
**Implemented approach:** stage checked project data and complete mesh streams,
validate lengths/arrays/references and buffer-load results before changing active
work, and propagate failures to callers. Multi-primitive glTF rejection has an
explicit active-state preservation test. Existing legacy defaults and script-trust
checks are retained; broader primitive/material semantics remain unsupported.
**Effort/risk:** 3–5 days initially; format compatibility and partial import cleanup
are dependencies. **Success:** a focused corrupt/missing-data corpus fails without
crash or active-project loss; known valid legacy and multi-object fixtures retain
geometry, materials and references.

### P2-04 — Make existing editor/interpreter checks repeatable and meaningful

**Status:** implemented locally; the repeatable Windows command, failure controls,
pixel/state evidence and remaining qualification limits are recorded above.

**Original confirmed development gap:** `minic_tests.c` has no checked-in invocation;
`paint/assets/plugins/dev/test.c` hardcodes a Unix output root, prints failure
while returning zero, and uses unconditional `check(1, ...)` for several painting
and import steps. The new smoke runner covers the fixes here, not those workflows.

**Implemented approach:** a small local minic entry point returns aggregate
failure; the editor exercise takes an output root and uses the existing script
and disposable-runtime mechanisms. Save/paint/undo/export checks now assert
decoded pixels and saved state, and deliberate failures verify command status.
No CI or testing framework was added.
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

Not verified: interactive dialogs, stylus/touch input, manual visual paint/undo fidelity,
all texture bit depths and export presets, legacy/corrupt-file compatibility,
ray-traced baking, timeline/video output, neural models, account/cloud services,
MCP concurrency/restore, player packaging, Linux/macOS/mobile/WASM, hosted CI,
installation/signing/release behavior. No downloads or external service tests
were needed for this review. No bundled binaries, dependencies or CI workflows
were changed.

Space sync: not applicable — no explicit binding or supported association was
declared; no unrelated Spaces were searched and none was created.
