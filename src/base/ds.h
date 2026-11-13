#pragma once

// Engine entry point for stb_ds. Allocation is routed through mem_* in ds_impl.c.
// Init-time/tooling only, or with capacity reserved up front (arrsetcap) — see CLAUDE.md, Memory.
#include <stb_ds.h>
