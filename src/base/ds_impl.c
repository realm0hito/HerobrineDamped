// Single stb_ds implementation TU; its allocations are counted by mem_*.
#include "base/mem.h"

#define STBDS_REALLOC(c, p, s) mem_realloc(p, s)
#define STBDS_FREE(c, p)       mem_free(p)
#define STB_DS_IMPLEMENTATION
#include <stb_ds.h>
