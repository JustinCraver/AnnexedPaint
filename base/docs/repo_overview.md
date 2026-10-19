# Architecture and workflow map

The checkout contains the ArmorPaint application and the Iron engine/build tools.
It bundles many dependencies and build-tool binaries, but still requires platform
compiler, SDK and system libraries; see the [development guide](readme.md).
The application manifest is in `paint/sources/globals.h`: title `ArmorPaint`,
version `1.1alpha`, project format `19`, config format `1` at the 2026-10-18 review.

Paths below are relative to the repository root.

## Source ownership

| Area | Responsibility |
| --- | --- |
| `paint/project.c`, `base/project.c` | Application/engine sources, defines, platform features, shader and asset packaging |
| `paint/sources/main.c` | Application initialization and inclusion of most application `.c` modules |
| `paint/sources/{types,functions,globals,enums}.h` | Shared data structures, declarations, state and enums |
| `paint/sources/startup.c` | Embedded startup scene data |
| `paint/sources/base.c`, `context.c`, `config.c` | Frame/input integration, active editing state, preferences |
| `paint/sources/ui/` | Immediate-mode editor UI, dialogs, panels, timeline |
| `paint/sources/slots/`, `history.c`, `util/util_layer.c` | Layer/material/brush resources, layer operations and undo/redo |
| `paint/sources/render/`, `parser_material.c`, `node_shader.c` | Painting, render paths, shader generation and material compilation |
| `paint/sources/nodes_*` | Brush, material, mesh and neural node implementations |
| `paint/sources/io/`, `project.c`, `project_save.c`, `util/util_encode.c` | Import/export, project lifecycle, deferred save completion and `.arm` encoding |
| `paint/plugins/` | Native format readers (including FBX, glTF, SVG, TIFF and EXR) |
| `paint/sources/minic_{api,impl}.c`, `minic_api_list.h`, `agent.c`, `args.c` | Script API, editor automation, CLI and local MCP bridge |
| `paint/assets/`, `paint/shaders/` | Shipped presets, locale text, scripts, meshes and application shaders |
| `base/sources/engine.c`, `iron_*.c`, `backends/` | Scene/assets, memory utilities, UI/input/files, GPU and platform backends |
| `base/sources/iron_file_write.c` | Checked byte writes and native project replacement through a unique sibling file |
| `base/sources/iron_image.c` | Checked image writes and dynamically sized encoded-image buffers |
| `base/sources/libs/`, `kong/` | Bundled libraries, minic interpreter and shader compiler |
| `base/tools/amake/`, `base/tools/bin/` | Build-tool source and checked-in platform binaries |
| `base/tools/tcc/`, `iris/`, `pad/` | Optional compiler/tool sources; not the normal Windows app toolchain |

`paint/project.c` directly compiles `main.c`, `minic_api.c`, and `startup.c` plus
the engine and native plug-ins. Most application modules are included by `main.c`;
adding them separately to the project would duplicate definitions. State is shared
through `g_project`, `g_context`, `g_config` and related globals. This is an
established design, not a recommendation to split it into new abstraction layers.
`project.c` includes `project_save.c`; do not add the latter as a standalone app
build input. Engine `iron_file_write.c` is compiled by the existing base wildcard.

## Main workflows

1. **Startup and new project:** `main.c` initializes shared state, config and
   graphics; `base.c` initializes the editor. `ui/box_new_project.c` selects a
   template and `project.c:project_new()` resets objects, materials, layers,
   history, timeline and view state. Several preview/layer operations run on a
   later frame through `sys_notify_on_next_frame`.
2. **Import:** file dialogs and drops reach `io/import_asset.c`; scripts reach the
   same path through `minic_impl.c:script_import_asset()`. Mesh import dispatches
   OBJ/Blender internally and native plug-in formats through a registry.
   `import_mesh_finish_import()` completes hierarchy, bounds and layer setup.
   Texture import caches GPU images and registers project assets. Recognition and
   registry lookup must agree on extension case; retain the caller's actual path.
   `import_arm.c` includes `import_arm_validate.c`: incoming projects are decoded
   by field name into staged allocations and checked before `project_new()` or
   append/import callbacks can modify the active project. Required arrays, mesh
   layouts/references, matrices, hierarchy and exact LZ4 layer sizes are checked;
   rejected staging allocations are released. Legacy defaults remain explicit.
   Registry mesh imports collect and validate the complete stream before scene
   replacement/addition. glTF checks buffer loading and geometry first; multiple
   primitives, sparse accessors and non-triangles are rejected. OBJ/Blender have
   separate parser paths. Rejected project/plug-in mesh imports stop CLI exports.
3. **Paint and undo:** tools/UI update context; brush/material nodes feed generated
   shaders in `render/make_paint.c` and related files. `render_path_paint.c` writes
   GPU layer targets; `slots/slot_layer.c` and `util/util_layer.c` manage layer
   resources. `history.c` records operation-specific state and texture snapshots.
   A change may affect both ordinary painting and fill/mask layers.
4. **Save and reopen:** `project_save.c:project_save()` captures the destination
   and quit intent, then defers to `export_arm_run_project()`.
   `export_arm_encode_project()` collects nodes, assets, layer pixels and timeline
   data; `util_encode.c`/armpack serialize it. `import_arm.c` and `import_legacy.c`
   reconstruct state and compatibility data. Native project writes use
   `iron_file_save_bytes_atomic()`: exclusively create a sibling, check the full
   write/flush/sync/close, then replace the destination. Failure retains the dirty
   title, previous Save As filepath and recent files, and keeps the editor open.
   Only success commits the new filepath, clears the title, reports saved and
   honors save-and-quit. Save As asset references use the selected destination;
   Windows serialization normalizes separators for relative references. Browser
   disk/cloud writes retain their existing routing but report unconfirmed dispatch
   and keep unsaved state/open because that bridge lacks a completion receipt.
   See review P1-01 for verification and remaining platform/legacy limits.
5. **Export:** `ui/box_export.c` and `args.c` select settings;
   `io/export_texture.c` flattens selected layers and applies channel/color-space
   mappings from `paint/assets/export_presets/`. PNG/JPG and EXR bit depths take
   different write paths. Mesh, material and player export have separate owners.
   Texture export returns an aggregate boolean only after encoding and checked
   writes finish; failures suppress the success message. The CLI validates its
   exact preset/type/directory, waits for script frame callbacks when combined,
   reapplies CLI settings, and stops background execution after export completion.
   Engine process status carries CLI failures through the native entry point.
6. **Timeline/capture:** `ui/tab_timeline.c` owns keyframes, playback, stages and
   save integration. `viewport.c` captures the viewport; `base/sources/iron_image.c`
   implements MP4 encoding. Current capture writes `output.mp4` next to the saved
   project. Presence of these paths does not qualify timing or visual output.
7. **Scripts and automation:** minic evaluates C-like scripts through explicit
   registered API functions. The desktop `--mcp` stdio bridge communicates with a
   running editor using files in its application-data `agent` directory. It is
   not a stateless headless editor. Preserve imported script trust prompts and
   session/restore behavior; concurrent-client handling is not qualified here.

## Platform and feature boundaries

Windows uses Direct3D12 by default; other backends include Vulkan, Metal and
WebGPU. `paint/project.c` gates features by platform. Neural nodes, account/cloud
operations and web player export involve external assets or services; a local
build does not verify those dependencies. Player export explicitly rejects Debug
builds, and its macOS branch reports `Not implemented`.

The [review](PROJECT_REVIEW.md) separates observed behavior, source-confirmed
limitations, untested areas and proposed improvements. No accepted Annexed-specific
requirements or roadmap were found; preserve upstream intent until that direction
is explicitly decided.
