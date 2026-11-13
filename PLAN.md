# PLAN — HerobrineDamped voxel engine

Status: **planning, awaiting approval**. No engine code exists yet.
Working name `HerobrineDamped` (repo name) stands in for `<PROJECT_NAME>`; see Open Questions.

Contents
1. Architecture overview
2. Coordinate spaces and precision
3. Data layouts (exact)
4. Traversal algorithm
5. GPU memory, pools, uploads
6. Frame graph per milestone
7. Milestones (acceptance, debug views, effort)
8. Repository layout
9. CMake structure
10. Dependencies (verified pins)
11. Vulkan / GPU requirements
12. Risks
13. Open questions (need your decision)

Milestone 1 task breakdown: [`docs/M1_TASKS.md`](docs/M1_TASKS.md).

---

## 1. Architecture overview

```
            CPU                                              GPU
 ┌──────────────────────────────┐            ┌───────────────────────────────────────────┐
 │ app: input, fly camera (int  │  GpuFrame  │ TopGrid  N^3 u32 (toroidal window)        │
 │      cell + float frac)      │──(BDA)────▶│   │ entry → chunk slot | uniform | empty  │
 │ world: chunk hash int3→chunk │            │   ▼                                       │
 │        streaming window      │  upload /  │ ChunkPool  slot: Mask512 + 512 BrickRef   │
 │        edit queue / log      │  commands  │   │ ref → brick idx | uniform | empty     │
 │ jobs: CPU workers (M3)       │──────────▶ │   ▼                                       │
 │ alloc: arenas, slot stacks   │            │ BrickOcc  idx: Mask512 (64 B)             │
 │ physics mirror (M5)          │◀─readback─ │ BrickMeta idx: 8 B → MatPool (palette+idx)│
 └──────────────────────────────┘            │ Materials table (u16 id → GpuMaterial)    │
                                             └───────────────────────────────────────────┘
 Frame:  [upload] → [gen/edit cs] → TRACE cs (3-level DDA) → VisBuffer (RG32UI)
         → RESOLVE cs (material, normal, shading | debug view) → color RGBA16F
         → blit to swapchain → cimgui (dynamic rendering) → present
```

Key decisions (justification in the relevant section):

- **Uniform 512-ary nodes.** Chunk (64³ voxels) = 8³ bricks; brick = 8³ voxels. Both levels are the same
  shape: a 512-bit `Mask512` + 512 children. One traversal routine serves both levels. With the top grid this
  is a fixed-depth 3-level tree (top grid → chunk → brick → voxel). This is the "wide-shallow" structure: at
  most 3 dependent memory fetches from top grid to voxel bit.
- **Morton order inside every Mask512.** Bit `i = morton3(x,y,z)`, x,y,z ∈ [0,8). Consequence: each aligned
  4³ octant is one contiguous 64-bit word pair, each aligned 2³ block is one byte. Hierarchical empty-space skip
  (8³ → 4³ → 2³ → 1) falls out of the bit layout with no extra storage. This also reconciles the M2 spec's
  "64-bit occupancy masks": a brick is 8 × 64-bit octant masks = 64 bytes.
- **Uniform-solid elision.** A fully solid single-material brick or chunk is encoded *in its parent's reference*
  (`0x8000_0000 | material`). It costs zero pool memory. Underground and interior regions are nearly free.
- **All buffers via buffer device address (BDA)**; descriptors only for images (one bindless set).
- **Camera-relative "render space"** rebased at chunk granularity; integer cell coordinates in every DDA.
- **Visibility buffer** stores exact voxel-in-chunk coordinates + face + ray distance, so the resolve pass never
  reconstructs integer coordinates from floats alone.

## 2. Coordinate spaces and precision

| Space | Type | Unit | Notes |
|---|---|---|---|
| World voxel | `int32_t[3]` | 1 voxel | ±2³¹ voxels = ±214 km at 10 cm, ±107 km at 5 cm. Y up, right-handed. |
| Chunk coord | `int32_t[3]` | 64 voxels | `chunk = voxel >> 6` (arithmetic shift = floor division). |
| Camera | `int32 cell[3]` + `float frac[3]` | voxel | `frac` renormalised into [0,1) after each move, carrying into `cell`. Never a large float. |
| Render space | `float` / `int32` | voxel | `rs = world − render_origin`, `render_origin = camera_chunk * 64`. Rebased when the camera changes chunk. Magnitudes ≤ N·64/2 (2048 for N=64): float ulp ≤ 2.4e-4 voxel. |
| Meters | `float` | m | UI / gameplay only: `m = voxel * voxel_size`. Never used in shaders. |

Rules:
- Shaders only see render space. CPU subtracts **in integers first** (`world − origin`), converts to float last.
- View matrix contains rotation only; ray origin = `cam_pos_rs`, ray direction from rotation + pixel.
- Temporal passes (M6): previous camera position is expressed in *current* render space
  (`prev_world − origin_now`, integer subtraction) so reprojection survives rebases.
- No epsilon nudging anywhere in traversal (see §4).

## 3. Data layouts (exact)

All GPU structs live in one header `shaders/shared/gpu_types.h`, included by both C and GLSL (macro shim maps
`uint32_t`, `float3`, `int3`, `uint64_t` / buffer-reference types). GLSL uses `GL_EXT_scalar_block_layout`
(`scalar` layout = C layout for these types). C side `_Static_assert`s every `sizeof` and critical `offsetof`.

### 3.1 Mask512 (shared by chunks and bricks)

```c
typedef struct Mask512 { uint32_t w[16]; } Mask512;   // 64 B, 64 B aligned in pools

// x,y,z in [0,8)
static inline uint32_t spread3(uint32_t v) { return (v & 1u) | ((v & 2u) << 2) | ((v & 4u) << 4); }
static inline uint32_t morton3(uint32_t x, uint32_t y, uint32_t z) { return spread3(x) | spread3(y) << 1 | spread3(z) << 2; }
```

Bit `i` (0..511): `w[i >> 5] >> (i & 31) & 1`.

| Bits of `i` | Meaning | Mask granularity |
|---|---|---|
| `i[8:6]` = (z2 y2 x2) | octant (4³ block) | `w[2o] \| w[2o+1]` == 0 → 4³ empty (64-bit test) |
| `i[8:3]` | 2³ block | byte `i>>3` == 0 → 2³ empty |
| `i` | cell | single bit |

Shaders work in `uint` words only (no `shaderInt64` requirement). A "64-bit octant mask" is a `uvec2`.

### 3.2 Reference encoding (`Ref`, 32 bit) — used by top grid and chunk children

| Value | Meaning |
|---|---|
| `0xFFFF_FFFF` | `REF_EMPTY` — all air |
| `0xFFFF_FFFE` | `REF_NOT_RESIDENT` — top grid only: not generated yet (rendered as air, tinted in debug view) |
| `0x8000_0000 \| mat` (`mat` < 2¹⁶, bits 16..30 = 0) | `REF_UNIFORM` — fully solid, single material, **no pool storage** |
| `< 0x8000_0000` | index into the next pool (chunk slot for the top grid, brick index for a chunk) |

### 3.3 Brick (8³ voxels) — structure of arrays across pools

```c
// BrickOcc pool: one Mask512 per brick index. 64 B, exactly one cache line / two 32 B sectors.
// BrickMeta pool: 8 B per brick index.
typedef struct GpuBrickMeta {
    uint32_t data;   // bits==0: material id (uniform-material mixed brick, no MatPool use)
                     // bits>0 : word offset into MatPool
    uint32_t info;   // [0:2]   idx_code: 0→0 bits, 1→1, 2→2, 3→4, 4→8 bits per solid voxel
                     // [3:10]  palette_count − 1   (1..256)
                     // [11:20] solid_count         (0..512)  == popcount(occ)
                     // [21:31] reserved (flags: emissive-any, edited, LOD level...)
} GpuBrickMeta;
```

MatPool layout at `data` (uint32 words, allocation is a size-class block, §5):
```
[ palette: palette_count × uint16 material id, packed 2 per word, ceil(pc/2) words ]
[ indices: solid_count × B bits, little-endian packed; B ∈ {1,2,4,8} divides 32 → never straddles a word ]
```
Indices are **rank-compacted**: only solid voxels store an index. For voxel `i`:
```
r   = Σ_{k < i>>5} bitCount(occ.w[k]) + bitCount(occ.w[i>>5] & ((1u << (i & 31)) - 1u))
bit = r * B
idx = (MatPool[data + pal_words + (bit >> 5)] >> (bit & 31)) & ((1u << B) - 1u)
mat = palette[idx]
```
Cost: ≤ 16 popcounts, paid once per pixel in RESOLVE, never during traversal.
Worst case brick payload: 128 words palette + 128 words indices = 256 words (1 KiB). Typical surface brick
(~150 solid voxels, ≤ 4 materials): 1 + 10 words.

Per-voxel colour variation: computed, not stored — `albedo * (1 + var * (hash(world_voxel) − 0.5))`, with
`var` from the material. Uses world voxel coords (render coords + origin as ints) so it is stable across rebases.

### 3.4 Chunk (64³ voxels)

GPU (ChunkPool slot, fixed size):
```c
typedef struct GpuChunk {
    Mask512  brick_mask;      // bit morton3(bx,by,bz) set ⇔ brick not empty (mixed or uniform)
    uint32_t brick_ref[512];  // Ref, Morton order; REF_EMPTY where mask bit is 0
} GpuChunk;                   // 2112 B
```
Fixed 2112 B per *resident non-trivial* chunk keeps the allocator trivial (slot stack). Empty and uniform
chunks never get a slot. Estimated surface chunks for N=64: ~10–15k → 21–32 MB. (Compacting `brick_ref` via
rank is a later optimisation if memory demands it.)

CPU (`world/`):
```c
typedef struct Chunk {
    int32_t  coord[3];
    uint32_t top_ref;        // what the GPU top grid holds for this chunk (Ref)
    uint32_t state;          // QUEUED | GENERATING | RESIDENT | EVICTING
    uint32_t edit_head;      // index into edit log (M5), ~0u if none
    uint32_t gen_frame;      // frame the gen was submitted (readback matching)
    Mask512 *phys_bricks;    // M5: CPU occupancy mirror (NULL unless near player), arena-owned
} Chunk;
// ChunkMap: open addressing, linear probing, key = packed int3 (21 bits each → u64), fixed capacity
// power of two, tombstones; capacity chosen at init (no rehash in hot path). Not stb_ds.
```

### 3.5 Top grid (streamed window)

```c
// GPU buffer: uint32_t Ref[N*N*N], N = 1 << grid_log2 (default N=64 → 1 MiB; 409.6 m cube at 10 cm)
// Toroidal addressing: no data moves when the window slides.
uint32_t top_slot(int3 c) { return (c.x & (N-1)) | (c.y & (N-1)) << log2N | (c.z & (N-1)) << 2*log2N; }
```
- Window = chunks `[grid_min, grid_min + N)`, `grid_min = camera_chunk − N/2`. Passed as render-space voxel
  coordinates (`grid_min_rs = grid_min*64 − render_origin`).
- Shader clips the ray to the window AABB first; anything outside is air (M1–M3) or the next LOD (M4).
- When the window slides, CPU writes `REF_NOT_RESIDENT` into the vacated slots (via a small compute "patch" pass
  or `vkCmdUpdateBuffer`/copy regions) and frees their chunk slots and bricks (§5).
- M4 adds a 4³-chunk coarse occupancy bit volume (N³/64 bits) to skip big air spans in one step.

### 3.6 Materials

```c
typedef struct GpuMaterial {     // 16 B, indexed by uint16 material id; 0 reserved = "debug magenta"
    uint32_t albedo;             // RGBA8, rgb sRGB-encoded, a = colour variation amount
    uint32_t emissive;           // RGB8 + 8-bit exponent (intensity 2^(e−128)), 0 = none
    uint32_t params;             // [0:7] roughness, [8:15] metallic, [16:23] variation scale, [24:31] flags
    uint32_t reserved;
} GpuMaterial;
```

### 3.7 Visibility buffer

`VK_FORMAT_R32G32_UINT` storage image, 8 B/pixel (16.6 MB at 1440p).

| Field | Bits | Content |
|---|---|---|
| `.x` | 0..31 | `floatBitsToUint(t)`: ray distance in voxels (render space). Miss: `+inf` (`0x7F80_0000`). |
| `.y` | 0..17 | voxel-in-chunk coords: `lx \| ly<<6 \| lz<<12` (each 0..63) — exact integers |
| `.y` | 18..20 | face: 0 +X, 1 −X, 2 +Y, 3 −Y, 4 +Z, 5 −Z, 7 miss |
| `.y` | 21..23 | LOD level (M4), 0 before |
| `.y` | 24..31 | reserved (M1: 0) |

Exact world voxel recovery in RESOLVE (no epsilon): `approx = floor(o + d*t − 0.5*n)` is within ±1 voxel of the
true voxel; local coords are exact; so `chunk = (approx − local + 32) >> 6` (arithmetic shift) is exact, and
`voxel = chunk*64 + local`. Error budget is ±31 voxels, the float error is ≪ 1.

Debug side image: `R32_UINT steps` (DDA iterations per pixel), written only when the heatmap view is active
(push-constant flag; branch is uniform).

### 3.8 Per-frame constants and push constants

```c
typedef struct GpuFrame {            // scalar layout, in a per-frame-slot host-visible buffer, read via BDA
    float3   cam_pos;   float tan_half_fov;     // render space
    float3   cam_right; float aspect;
    float3   cam_up;    float pixel_angle;      // radians per pixel (cone LOD, M4)
    float3   cam_fwd;   uint32_t frame_index;
    int3     grid_min;  uint32_t grid_log2;     // render-space voxel coord of window min corner
    uint32_t width, height, debug_view, flags;
    float3   sun_dir;   float time;
    uint64_t top_grid, chunk_pool, brick_occ, brick_meta, mat_pool, materials;   // BDA
    uint64_t dense_grid;                        // M1 only
} GpuFrame;

typedef struct GpuPush {             // ≤ 128 B (guaranteed minimum)
    uint64_t frame;                  // BDA of GpuFrame for this slot
    uint32_t img[6];                 // bindless storage-image indices (vis, steps, color, ...)
    uint32_t pass_param[4];
} GpuPush;
```

### 3.9 Milestone 1 dense grid

`256³` voxels, 1 byte material id per voxel (0 = air), packed 4 per `uint32` (`x` fastest): 16 MiB storage buffer,
read as `uint` words (avoids requiring `storageBuffer8BitAccess`). Placed at world voxel origin [0,256)³.
Generated once on the CPU (sphere + terraced value-noise heightfield + a few pillars to stress grazing rays),
uploaded through the staging path. Replaced by the brickmap in M2 but kept as a reference renderer/debug mode.

## 4. Traversal algorithm

Amanatides–Woo, with three rules that remove epsilons:

1. **Integer cell, plane-derived t.** State is integer `cell` (ivec3) and per-axis `t_next` computed from the
   *integer* plane: `t_next[a] = (float(cell[a] + (step[a] > 0 ? s : 0)) − o[a]) * inv_d[a]`, recomputed each step
   (not accumulated with `t_delta`) — no drift, planes are exactly representable integers in render space.
   `inv_d = 1/d` with zero components replaced by `±3.4e38` (sign-preserving) to avoid `0*inf = NaN`.
2. **Exact face on entry, clamp elsewhere.** When descending into a child (brick inside chunk, voxel inside
   brick, finer step inside a coarse block), the coordinate on the entry axis is set exactly
   (`step > 0 ? lo : hi`), the other two are `clamp(floor(o + d*t), lo, hi)`. Float error can only move a value
   across the parent's boundary, which the clamp corrects. Ascending is `cell >> 3` (pure integer).
3. **Adaptive step size from masks.** Inside a Mask512 node, the step size `s ∈ {4,2,1}` is picked from the
   octant (64-bit) and 2³ (byte) masks; `cell` is aligned with `cell & ~(s−1)`, advanced by `step*s`.
   Empty octant → one DDA step crosses 4 cells.

Levels: top grid (step = 1 chunk, entry test `Ref`), chunk node (8³ bricks, Mask512 + `brick_ref`), brick node
(8³ voxels, Mask512). Uniform refs terminate immediately as a hit at the entry face. Max-iteration cap per ray
(UI-tunable, default 512) writes a distinct "budget exceeded" colour in debug views.

Hardware RT later (M7): each mixed brick is one procedural AABB; the intersection logic is the brick-level
routine above, unchanged.

## 5. GPU memory, pools, uploads

| Pool | Element | Default capacity | Size | Allocator | Owner |
|---|---|---|---|---|---|
| TopGrid | u32 Ref | 64³ | 1 MiB | none (toroidal) | CPU writes |
| ChunkPool | 2112 B | 16 384 | 33 MiB | slot stack | CPU |
| BrickOcc | 64 B | 2²⁰ | 64 MiB | slot stack | M2 CPU, M3+ GPU |
| BrickMeta | 8 B | 2²⁰ | 8 MiB | same index as BrickOcc | — |
| MatPool | u32 words | 2²⁴ words | 64 MiB | size classes 4..256 words (2^k), per-class free stack, 64 KiB pages bump-carved | M2 CPU, M3+ GPU |
| Materials | 16 B | 65 536 | 1 MiB | static table | CPU |
| Staging ring | bytes | 32 MiB × frames-in-flight | 64 MiB | linear ring, reset per slot | CPU |

- All device-local pools are single `VkBuffer`s from VMA with `SHADER_DEVICE_ADDRESS` usage; shaders use
  `buffer_reference` pointers from `GpuFrame`.
- **GPU-resident allocation (M3+).** Generation runs on the GPU and does not know in advance which bricks are
  mixed. Free stacks for bricks and MatPool size classes live in GPU memory (`uint32 count; uint32 items[]`).
  Allocation within a pass = `atomicAdd(count, −1)`; frees happen in a *separate* pass (`free_push`) so pushes and
  pops never race. CPU reads counters back (a few frames late) for stats/budgeting only.
- **Chunk slots stay CPU-owned**: CPU knows residency; GEN_FINALIZE reports "empty/uniform" per chunk and the CPU
  reclaims those slots on readback.
- **Uploads**: persistent-mapped host-visible staging ring per frame slot; copy commands batched as one
  `vkCmdCopyBuffer2` per destination buffer per frame; single barrier `COPY → COMPUTE_SHADER` afterwards.
  ReBAR/host-visible device memory is not assumed.

## 6. Frame graph per milestone

Common skeleton (2 frames in flight):
```
wait fence[slot] → read timestamp queries[slot] → reset frame arena
acquire(img) → acquire_sem[slot]
record:
  FRAME_BEGIN barrier (COMPUTE|TRANSFER → COMPUTE|TRANSFER, covers cross-frame WAR on single-instanced images)
  … passes …
  swapchain: UNDEFINED → TRANSFER_DST ; BLIT color → swap ; → COLOR_ATTACHMENT ; UI ; → PRESENT_SRC
submit: wait acquire_sem[slot] @ ALL_TRANSFER, signal render_done_sem[img] (one per swapchain image), fence[slot]
present: wait render_done_sem[img]
```
Storage images stay in `GENERAL` permanently (valid for storage, blit source, and sampling); only
execution/memory barriers between passes. Every pass is bracketed by timestamps + a debug-utils label.

| M | Passes (in order) | New resources |
|---|---|---|
| 1 | (once) UPLOAD dense grid → **TRACE_DENSE** (vis, steps) → **RESOLVE** (color) → BLIT → UI | dense grid SSBO, vis RG32UI, steps R32UI, color RGBA16F, GpuFrame ×2 |
| 2 | UPLOAD (bricks/chunks/top grid regions) → **TRACE** (3-level) → RESOLVE → BLIT → UI | all §5 pools, materials |
| 3 | STREAM_PATCH (top grid evictions) → FREE_PUSH → **GEN_BRICK** (1 workgroup/brick, 64 threads, 1 thread = 2³ block = one mask byte) → **GEN_FINALIZE** (1 wg/chunk: collapse empty/uniform, write top grid Ref) → TRACE → RESOLVE → BLIT → UI → READBACK (gen results, counters) | GPU free stacks, gen job buffer, readback ring |
| 4 | as 3, GEN per LOD level with per-level budget; TRACE switches level by cone width | per-level top grids + coarse occupancy volume |
| 5 | as 4 + **EDIT_APPLY** (after GEN) + READBACK physics bricks | edit command buffer, edit log (CPU) |
| 6 | TRACE → RESOLVE_GBUF (normal, albedo, material) → SHADOW (sun ray) → AO → CACHE_UPDATE (world hash radiance cache) → RESTIR_TEMPORAL → RESTIR_SPATIAL → DENOISE → COMPOSITE/TONEMAP → TAA → BLIT → UI | history images, motion vectors, hash cache buffer, blue-noise textures |
| 7 | alt TRACE_HWRT (ray query, procedural AABB per brick) + BLAS/TLAS build/refit passes | AS buffers, scratch |

## 7. Milestones

Effort assumes part-time (~10–15 h/week). Every milestone ends with: validation clean (incl. sync validation run),
Linux + Windows build, debug view(s) listed, and a short `docs/notes/Mx.md` (measurements, what went wrong).

### M1 — Bootstrap + dense DDA (40–60 h)
Window + Vulkan 1.3 bootstrap, compute → storage image → blit → swapchain, 256³ dense grid, fly camera, step
heatmap, cimgui timings.
- **Acceptance**: clean build (0 warnings at `/W4` / `-Wall -Wextra`) on Linux gcc+clang and Windows MSVC+clang-cl;
  0 validation messages (standard and sync-validation runs) over 60 s of flight incl. resize, minimise, present-mode
  switch; 256³ scene at 1080p with TRACE < 2 ms on an RTX 3060-class GPU; camera stays precise when teleported to
  `(2e9, 0, 2e9)` voxels relative to a grid placed there; no heap allocation per frame (debug alloc counter = 0
  after warm-up).
- **Debug views**: shaded, normal, depth, step heatmap (with legend + max-steps slider), raw vis decode,
  chunk-boundary overlay. Panel: CPU frame ms, GPU ms per pass (rolling 240 frames), present mode, resolution scale.
- Detailed tasks: `docs/M1_TASKS.md`.

### M2 — Brickmap (30–45 h)
Top grid + chunks + bricks + palette materials, CPU-built from the same scene function as M1 and from a larger
procedural scene (e.g. 1024³ terrain), CPU allocators, upload path, per-voxel colour variation.
- **Acceptance**: M1 scene renders pixel-identical (vis buffer compare via debug readback) with dense vs brick
  traversal; memory stats in UI match analytic expectation; heatmap shows octant/byte skips (fewer steps than
  dense on the same view); bench mode (recorded camera path → CSV of pass times) exists.
- **Debug views**: brick/chunk occupancy colouring (empty/uniform/mixed), skip-level heatmap, palette size view,
  memory panel (pool occupancy).

### M3 — Streaming + GPU generation + jobs (60–90 h)
Toroidal top-grid window, GPU SDF/noise generation into pools, GPU free stacks, CPU job system (fixed workers,
bounded MPMC queue, counters), infinite world at 10 cm.
- **Acceptance**: fly at 100 m/s for 5 min with no hitches > 1 ms over the frame budget attributable to streaming;
  per-frame generation budget respected (configurable chunks/frame); pool counters stable over a closed loop path
  (no leaks); SDF early-out ratio reported (>90 % of bricks skipped without per-voxel evaluation on terrain).
- **Debug views**: residency view (NOT_RESIDENT tint), generation age heatmap, allocator panel.

### M4 — LOD for km-scale view (40–60 h)
Clipmap levels with voxel size 2^L, cone-width level switch in TRACE, coarse occupancy volume.
- **Acceptance**: ≥ 10 km view distance at 10 cm base voxels; no visible holes at level seams on the reference
  path; TRACE time grows sub-linearly with view distance (bench table).
- **Debug views**: LOD level colouring, cone-vs-voxel ratio view.

### M5 — Edits + collision mirror (30–50 h)
CSG edit queue (union/subtract/paint; sphere/box/capsule), EDIT_APPLY compute, per-chunk edit log replayed after
regeneration, CPU occupancy mirror near the player via readback, swept-AABB player collision.
- **Acceptance**: 1000 sphere edits/s sustained without hitches; evicted + re-streamed chunks keep edits;
  player cannot tunnel through 1-voxel walls at 50 m/s.
- **Debug views**: dirty-brick flash, physics-mirror region overlay.

### M6 — Lighting (120–200 h, split into 6a–6e)
6a sun shadows → 6b AO → 6c world-space hashed radiance cache → 6d ReSTIR GI → 6e denoise + TAA.
- **Acceptance (each)**: reference comparison against a brute-force accumulated path-traced mode; stated ms budget;
  stable under camera rebase.
- **Debug views**: each buffer viewable; cache-cell visualisation; reservoir M/weight view.

### M7 — Optional HW RT path (40–60 h)
BLAS = procedural AABB per mixed brick per chunk, TLAS = chunk instances; ray query in compute.
- **Acceptance**: same images as compute path; bench report compute vs HW RT on ≥2 GPUs.

## 8. Repository layout

```
HerobrineDamped/
├── CMakeLists.txt              root: project, options, includes cmake/*, add_subdirectory(src)
├── CMakePresets.json           linux-{gcc,clang}-{debug,release}, win-{msvc,clangcl}-{debug,release}
├── CLAUDE.md  PLAN.md  README.md  LICENSE  .gitignore  .gitmodules  .clang-format
├── cmake/
│   ├── CompilerOptions.cmake   warnings, C11, MSVC/clang-cl flags, sanitizer option
│   ├── Dependencies.cmake      add_subdirectory(external/* SYSTEM EXCLUDE_FROM_ALL), cimgui target
│   └── Shaders.cmake           hd_add_shaders(target ...) → glslc custom commands w/ depfiles
├── external/                   git submodules, pinned (see §10)
│   ├── Vulkan-Headers/ volk/ VulkanMemoryAllocator/ glfw/ cglm/ stb/ cimgui/(→imgui) tracy/
├── shaders/
│   ├── shared/gpu_types.h      C + GLSL shared structs/constants (§3)
│   ├── lib/                    dda.glsl, mask512.glsl, hash.glsl, color.glsl, frame.glsl
│   ├── trace_dense.comp  resolve.comp                       (M1)
│   └── trace.comp  gen_brick.comp  gen_finalize.comp  ...  (M2+)
├── src/
│   ├── main.c                  arg parsing, app loop
│   ├── base/                   base.h (types, macros, VK_CHECK, ASSERT), arena.c, log.c, os.h,
│   │                           os_win32.c, os_posix.c (time, file, threads later), atomics.h, ds_impl.c
│   ├── gpu/                    vk_ctx.c (instance/device/queues/VMA), vk_swapchain.c, vk_frame.c,
│   │                           vk_sync.c (barrier helpers), vk_debug.c (names/labels/messenger),
│   │                           vk_pipeline.c (compute pipelines, SPIR-V load, hot reload),
│   │                           vk_bindless.c, vk_upload.c (staging ring), vk_timing.c, vma_impl.cpp
│   ├── render/                 renderer.c (frame graph), pass_trace.c, pass_resolve.c, pass_present.c
│   ├── world/                  dense_grid.c (M1), scene_fn.c, chunk_map.c, pools.c, stream.c, gen.c, edit.c
│   ├── app/                    camera.c, input.c, ui.c (cimgui panels)
│   └── jobs/                   (M3) jobs.c
├── docs/
│   ├── M1_TASKS.md
│   └── notes/                  per-milestone measurements and post-mortems
└── tools/                      (later) bench CSV plotting, capture scripts
```

## 9. CMake structure

- `cmake_minimum_required(VERSION 3.25)` — needed for `add_subdirectory(... SYSTEM)` (third-party warnings
  silenced without touching their sources) and reliable `DEPFILE` on Ninja/Makefiles/VS.
- `project(HerobrineDamped LANGUAGES C CXX)`; `CMAKE_C_STANDARD 11`, `C_STANDARD_REQUIRED ON`, `C_EXTENSIONS OFF`;
  `CMAKE_CXX_STANDARD 17` for third-party C++ only.
- Targets:
  - `engine` (executable): all of `src/**.c` + `src/gpu/vma_impl.cpp`. Engine-only warnings:
    GCC/Clang `-Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused-parameter -Werror=implicit-function-declaration`;
    MSVC/clang-cl `/W4 /permissive- /utf-8 /Zc:preprocessor`. `-Werror`/`/WX` via option `HD_WERROR` (ON in CI).
  - Third-party (all `SYSTEM`, `EXCLUDE_FROM_ALL`): `Vulkan::Headers`, `volk` (static, `VOLK_PULL_IN_VULKAN=OFF`, we
    link `Vulkan::Headers` explicitly so the header version is the pinned one, not whatever SDK is installed),
    `VulkanMemoryAllocator` (interface), `glfw` (static, examples/tests/docs/install OFF), `cglm_headers`
    (interface, header-only inline API), `stb` (interface: include dir), `hd_imgui` (static C++: cimgui.cpp,
    cimgui_impl.cpp, imgui*.cpp, backends/imgui_impl_{glfw,vulkan}.cpp; defines `CIMGUI_USE_GLFW CIMGUI_USE_VULKAN
    IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_IMPL_API=extern "C"`), `TracyClient` (only if
    `HD_TRACY=ON`).
  - Global compile definitions: `VK_NO_PROTOTYPES` (volk), `GLFW_INCLUDE_NONE`, `VMA_STATIC_VULKAN_FUNCTIONS=0`,
    `VMA_DYNAMIC_VULKAN_FUNCTIONS=0` (functions imported with `vmaImportVulkanFunctionsFromVolk`).
  - `shaders` custom target: `hd_add_shaders(engine shaders/*.comp)` → for each source
    `glslc --target-env=vulkan1.3 -std=460 -I shaders -MD -MF <dep> [-g -O0 | -O] -o <bin>/shaders/<name>.spv`,
    `DEPFILE` so includes trigger rebuilds; `engine` depends on it. `glslc` found via `find_package(Vulkan COMPONENTS glslc)`
    (`Vulkan::glslc`), falling back to `$ENV{VULKAN_SDK}/bin`.
- Options: `HD_TRACY` (OFF), `HD_WERROR` (OFF local / ON CI), `HD_ASAN` (Linux/clang, OFF),
  `HD_SHADER_DEBUG_INFO` (ON in Debug).
- Build types: **Debug** (validation on by default, object names, asserts, `-O0`/`/Od`), **RelWithDebInfo**
  (asserts + names, validation off unless `--validation`), **Release**.
- Runtime finds SPIR-V at `<exe_dir>/shaders/` (glslc output dir = `$<TARGET_FILE_DIR:engine>/shaders`), enabling
  hot reload in Debug.

## 10. Dependencies (verified 2026-11-13 against upstream)

**Policy: git submodules under `external/`, pinned to the commit of a release tag.**
Why submodules over FetchContent: (1) offline, configure-time-free builds; FetchContent downloads per build
directory unless `FETCHCONTENT_BASE_DIR` is shared, which adds friction with 4+ presets; (2) sources are on disk
and visible in the IDE — stepping into VMA/imgui/GLFW is part of the learning goal; (3) cimgui needs its own nested
imgui submodule, which is natural with `--recursive` and awkward with FetchContent; (4) the pin is a gitlink in the
tree, reviewable in diffs. Cost: `git clone --recursive` / `git submodule update --init --recursive` — documented.

| Library | Pin (tag) | Commit | Date | Notes verified |
|---|---|---|---|---|
| Vulkan-Headers | `vulkan-sdk-1.4.363.0` | `6802bb4733b63ed5efd3adb308a6c885ef180ea1` | — | Same SDK tag as volk; decouples headers from the installed SDK. |
| volk | `vulkan-sdk-1.4.363.0` | `54fc0d7a72887cbfc5b030d26897b2b40ac53fb8` | 2026-09-18 | `VOLK_HEADER_VERSION 363`; `volkInitialize/volkLoadInstanceOnly/volkLoadDevice`; CMake `volk` static target, `VOLK_PULL_IN_VULKAN` option. |
| VMA | `v3.5.0` | `0f1a757da579831283413295cc7f3b47eb521c77` | 2026-10-26 | `vmaImportVulkanFunctionsFromVolk`, `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`; header-only interface target → our `vma_impl.cpp`. |
| GLFW | `3.5.1` | `d9d6f0f1f967807ffade6598ea9a631ebaf37a56` | 2026-07-31 | `glfwInitVulkanLoader(vkGetInstanceProcAddr)` (call after `volkInitialize`, before `glfwInit`); Win32/X11/Wayland. |
| SDL3 (alternative) | `release-3.4.20` | `f00dc919ca35b9756316e86c4777392de442f9b2` | 2026-11-03 | Only if chosen over GLFW. |
| cglm | `v0.9.7` | `888aca08d5bdf38e873ec23d6cac42d7f23a669c` | 2026-10-25 | `cglm_headers` INTERFACE target (header-only, inline). |
| stb (stb_ds.h) | no tags | `9600522de53973336fa033a289858a4354cbb04c` | 2026-11-12 | Pin by commit; only `stb_ds.h` used. |
| cimgui | `1.93dock` | `2a6a93fccd10f275ee9206167dc9698f6f00fac1` | 2026-10-18 | imgui submodule `6d32174508fb8c518e7a832cc1d571fadd5cba76` = **imgui 1.93.0 (docking)**. Master is docking-only now. Its CMakeLists does **not** build backends → we define `hd_imgui` ourselves. `cimgui_impl.h` has `CIMGUI_USE_VULKAN`/`CIMGUI_USE_GLFW`, C-compatible `ImGui_ImplVulkan_InitInfo` (incl. `ImVector_VkDynamicState`). |
| Tracy | `v0.15.0` | `f2a4f2529e7e71844c9e434b877770a21ecf7587` | 2026-11-05 | C API `TracyC.h` (`TracyCZoneN`, `TracyCFrameMark`, manual GPU API `___tracy_emit_gpu_*`). `TracyVulkan.hpp` is C++-only → we feed GPU zones from our own timestamp queries. Viewer must match 0.15.x protocol. |

API facts that shape the code (from current sources):
- imgui 1.93 Vulkan backend: pipeline settings moved to `InitInfo.PipelineInfoMain` (since 2025-09-26):
  `PipelineInfoMain.PipelineRenderingCreateInfo` + `UseDynamicRendering = true`. Set `ApiVersion = VK_API_VERSION_1_3`
  so it loads core `vkCmdBeginRendering` (no KHR extension needed). `DescriptorPoolSize > 0` lets the backend own its
  pool. `IMGUI_IMPL_VULKAN_USE_VOLK` makes it use volk's pointers.
- volk refuses to compile if `vulkan.h` was included without `VK_NO_PROTOTYPES` → define it globally.

## 11. Vulkan / GPU requirements

Instance: API 1.3; `VK_EXT_debug_utils` (debug builds); GLFW surface extensions.
Device extensions: `VK_KHR_swapchain`. Optional: `VK_EXT_memory_budget`, `VK_KHR_calibrated_timestamps` (Tracy GPU
clock sync), M7: `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`, `VK_KHR_deferred_host_operations`.

Required features (device rejected otherwise, with a clear message listing what is missing):

| Struct | Features |
|---|---|
| 1.0 | `shaderStorageImageExtendedFormats` (rg32ui/r32ui image formats in GLSL), `shaderInt16` (optional), `samplerAnisotropy` (later) |
| 1.2 | `bufferDeviceAddress`, `descriptorIndexing`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `descriptorBindingUpdateUnusedWhilePending`, `descriptorBindingStorageImageUpdateAfterBind`, `descriptorBindingSampledImageUpdateAfterBind`, `shaderStorageImageArrayNonUniformIndexing`, `scalarBlockLayout`, `timelineSemaphore`, `hostQueryReset` |
| 1.3 | `dynamicRendering`, `synchronization2`, `maintenance4` |

Format checks at init: `R32G32_UINT`, `R32_UINT`, `R16G16B16A16_SFLOAT` support `STORAGE_IMAGE`; RGBA16F has
`BLIT_SRC`; chosen swapchain format has `BLIT_DST` (else fall back to a fullscreen-triangle copy pass — written
only if needed). Queues: one graphics+compute+present queue in M1 (async compute later, not designed in now).
Practical floor: NVIDIA GTX 10xx / AMD RDNA1 / Intel Arc or Xe-LP with current drivers; also runs on Mesa lavapipe
(1.3), useful for headless CI.

## 12. Risks

| Risk | Mitigation |
|---|---|
| **MSVC C11 gaps**: `<stdatomic.h>` needs `/experimental:c11atomics`, `<threads.h>` only VS 17.8+, no `__builtin_*` | Own `atomics.h` (`__atomic_*` / `_Interlocked*`), `os.h` thread wrappers (Win32 / pthreads), bit intrinsics wrapper (`_BitScanForward` / `__builtin_ctz`, `__popcnt`). CI builds MSVC and clang-cl. |
| **BDA bugs are silent** (OOB pointer reads return garbage, hang or TDR) | Validation GPU-AV mode (`--validation=gpuav`) checks BDA bounds; debug-build bounds asserts in GLSL via a `CHECK()` macro writing to a debug buffer. |
| **Sync bugs** | Sync validation run part of the commit checklist; few, explicit barriers; GENERAL layout for storage images. |
| **Swapchain semaphore reuse** (render-done semaphore still in use by present) | One render-done semaphore per swapchain image; acquire semaphores per frame slot. |
| **Windows TDR** on long GPU generation dispatches (>2 s) | Per-frame generation budget, never one giant dispatch. |
| **GPU-side allocator leaks/races** | Separate pop and push passes; counters read back and checked against CPU expectation in debug. |
| **CPU/GPU float divergence** (collision mirror vs rendered world) | Mirror built from GPU readback, never from re-evaluating noise on the CPU. |
| **Edits vs procedural regeneration** | Per-chunk edit log replayed after regeneration (M5); designed now via `Chunk.edit_head`. |
| **LOD seams / popping** | Conservative ("any solid") coarse generation; hysteresis/dither on cone switch; seam bench path. |
| **Third-party API churn** (imgui backend moved fields in 2025) | Exact pins; upgrades are their own commits with notes. |

## 13. Open questions (need your decision)

1. **Third-party C++ vs "only C++ is vma_impl.cpp"**: imgui (via cimgui) and the Tracy client are C++ libraries;
   cimgui only wraps them. Proposal: rule = *engine code* is C11 + the single `vma_impl.cpp`; third-party C++ is
   built in isolated `SYSTEM` targets (`hd_imgui`, `TracyClient`) the engine reaches only through C headers. OK?
2. **GLFW 3.5.1 vs SDL3 3.4.20** — recommend **GLFW**: window/input/surface is all we need, smaller, `glfwInitVulkanLoader`
   integrates with volk. SDL3 wins if you want gamepads, audio, and portable threads/atomics in one dependency.
3. **Submodules vs FetchContent** — recommend **submodules** (§10).
4. **Mask size wording**: spec says both "64-byte (512-bit) brick mask" and "64-bit occupancy masks". Proposal:
   512-bit per brick in Morton order = 8 × 64-bit octant masks (§3.1). OK?
5. **Windows toolchain priority**: MSVC, clang-cl, or both as first-class? (Recommend both in CI; clang-cl as the
   day-to-day compiler for better C11 and codegen.)
6. **Minimum GPU tier** (§11) and whether lavapipe headless CI is wanted (GitHub Actions: build both OSes, run
   N frames on lavapipe with validation, compare a screenshot hash).
7. **Base voxel size and window**: 10 cm with N=64 (±205 m streamed at L0) as default? 5 cm halves that radius.
8. **World extent**: int32 voxel coords (±214 km at 10 cm) acceptable, or do you want int64 chunk coords now?
9. **VRAM budget target** for defaults (4 / 8 / 12 GB)? Pools in §5 total ~235 MiB (incl. 64 MiB staging).
10. **Code prefix / names**: executable `engine`, CMake option prefix `HD_`, no function prefix inside the engine
    (module prefixes `gpu_`, `world_`, `arena_` …). Or a project prefix like `hd_` everywhere?
11. **Shader hot reload** in M1 (≈ +4 h) — yes/no?
12. **Vulkan SDK version installed** on your machines (for glslc / validation layers; headers come from the pin).
