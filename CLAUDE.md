# CLAUDE.md

Real-time voxel engine: C11 + Vulkan 1.3, GPU compute ray traversal over a brickmap. Design and data layouts:
`PLAN.md` (source of truth — update it in the same commit when a layout or decision changes). Current milestone
tasks: `docs/M1_TASKS.md`.

## Build & run

Prereqs: CMake ≥ 3.25, Ninja (recommended), a C11 compiler (GCC ≥ 11 / Clang ≥ 15 / MSVC VS 2022 / clang-cl),
Vulkan SDK (for `glslc` and validation layers; Vulkan headers come from `external/Vulkan-Headers`).

```sh
git clone --recursive <repo>            # or: git submodule update --init --recursive
cmake --list-presets
cmake --preset linux-clang-debug        # win: win-msvc-debug | win-clangcl-debug
cmake --build --preset linux-clang-debug
./build/linux-clang-debug/engine                     # Debug: validation ON by default
./build/linux-clang-debug/engine --validation=sync   # off | on | sync | gpuav
./build/linux-clang-debug/engine --gpu=1 --selftest
```
Shaders compile as part of the build (`glslc` → `<exe_dir>/shaders/*.spv`). Tracy: configure with `-DHD_TRACY=ON`.

## Code conventions

- **Language**: engine code is C11 (`C_EXTENSIONS OFF`). The only C++ we write is `src/gpu/vma_impl.cpp`.
  Third-party C++ (imgui via cimgui, Tracy client) lives in isolated targets and is used only via C headers.
- **Style**: concise, minimal comments (explain *why*, not *what*), no over-abstraction. Plain structs, flat arrays,
  explicit `*_init` / `*_shutdown` pairs, shutdown in reverse init order. No opaque handles or vtables unless
  there are ≥2 real implementations.
- **Naming**: functions `module_verb[_noun]` (`arena_push`, `gpu_buffer_create`), types `PascalCase`, GPU-shared
  structs `Gpu*`, macros/constants `UPPER_CASE`, file-local functions `static`. 4-space indent, `.clang-format`.
- **Memory**: arenas — `perm` (process lifetime), per-frame-slot `frame` arena (reset after fence wait), thread
  `scratch`. Fixed-capacity containers sized at init. **No `malloc`/`realloc` in per-frame paths** (debug alloc
  counter must stay 0 after warm-up). `stb_ds` only for init-time/tooling code or with pre-reserved capacity;
  hot-path maps (chunk map) are hand-written open addressing.
- **Vulkan**: every `VkResult` goes through `VK_CHECK(expr)` (aborts with expr, file, line, result name).
  Exceptions handled explicitly: `VK_SUBOPTIMAL_KHR` / `VK_ERROR_OUT_OF_DATE_KHR` on acquire/present,
  `VK_NOT_READY` on queries. Every object gets a debug name (`gpu_name`), every pass a debug label + timestamps.
  Sync2 barriers only, via `vk_sync.c` helpers. Buffers are accessed through BDA; descriptors only for images.
- **GPU structs**: defined once in `shaders/shared/gpu_types.h` (scalar layout), `_Static_assert` sizes in C.
  Bit layouts documented in `PLAN.md` §3 — change both together.
- **Coordinates**: world = `int32` voxels; shaders see render space only; subtract integers before converting
  to float. No epsilon bias in traversal (PLAN §4).
- **Asserts**: `ASSERT` in Debug + RelWithDebInfo; never put side effects inside.

## Dependency policy

- Git submodules in `external/`, pinned to the commit of a release tag (table in `PLAN.md` §10). No FetchContent,
  no system packages except the Vulkan SDK tools.
- Adding/upgrading a dependency is its own commit: update the submodule, the pin table, and note API changes.
  Check the upstream changelog for breaking changes before bumping (imgui backends move fields often).
- Third-party code is never edited in place. Configure through CMake options/defines; patches need a decision.
- Third-party targets are added with `SYSTEM EXCLUDE_FROM_ALL`; never weaken engine warnings for them.

## How to add a compute shader / pass

1. Write `shaders/<name>.comp`: `#version 460`, `#include "shared/gpu_types.h"`, `#include "lib/frame.glsl"`,
   `layout(local_size_x = 8, local_size_y = 8) in;`. Read `GpuPush` from push constants, `GpuFrame` via
   `push.frame` (BDA), images from the bindless arrays by index.
2. Add it to the `hd_add_shaders(engine ...)` list in `src/CMakeLists.txt` (includes are tracked by depfile).
3. If it needs new shared structs/constants: add to `gpu_types.h`, add `_Static_assert`s, document in `PLAN.md` §3.
4. In `src/render/pass_<name>.c`: `pass_<name>_init` (create pipeline via `gpu_compute_pipeline("<name>")`,
   create/register images or buffers, debug names), `pass_<name>_record(cmd, ...)` (label + timestamps, push
   constants, dispatch `ceil(w/8) × ceil(h/8)`), `pass_<name>_shutdown`, and `_resize` if it owns screen-size images.
5. Wire it into `renderer.c` in frame-graph order with the **minimal** barriers (producer stage/access → consumer
   stage/access). Update the frame-graph table in `PLAN.md` §6.
6. Add its timing row to the UI panel and, if it produces a buffer, a debug view.
7. Run with `--validation=sync` and check 0 messages before committing.

## Rules

- **Small commits**, one logical step each (a task in `docs/M*_TASKS.md` is one or a few commits). Message:
  `area: imperative summary` (e.g. `gpu: add swapchain recreation`).
- **The build must pass** on the commit (no warnings in engine code) — at minimum the Linux Debug preset;
  Windows before closing a task.
- **Run with validation before committing**: launch the Debug build, exercise the changed path (resize,
  minimise, toggles), confirm the validation message counter in the UI / shutdown log is **0**; use
  `--validation=sync` for anything touching barriers, and `--validation=gpuav` for new BDA access patterns.
- Don't commit generated files (`build/`, `*.spv`), captures, or local IDE settings.
- Measurements go in `docs/notes/Mx.md` with GPU, driver, resolution, and camera bookmark.
- Never silence a validation message or warning without a written reason next to it.
