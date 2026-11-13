# HerobrineDamped

A from-scratch real-time voxel engine in C11 + Vulkan 1.3. It renders high-resolution procedural worlds
(5–10 cm voxels, long view distance) by ray traversal in GPU compute over a sparse brickmap. No meshing.

Status: planning. See [PLAN.md](PLAN.md) for architecture, data layouts and milestones, and
[docs/M1_TASKS.md](docs/M1_TASKS.md) for the current milestone.

## Build

Requires CMake ≥ 3.25, a C11 compiler, and the Vulkan SDK (for `glslc` and validation layers).

```sh
git clone --recursive https://github.com/realm0hito/HerobrineDamped.git
cd HerobrineDamped
cmake --preset linux-clang-debug     # or win-msvc-debug / win-clangcl-debug
cmake --build --preset linux-clang-debug
```

See [CLAUDE.md](CLAUDE.md) for run flags, conventions, and contribution rules.

## License

MIT, see [LICENSE](LICENSE).
