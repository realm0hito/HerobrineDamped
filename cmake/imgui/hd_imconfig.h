// Dear ImGui user config for the hd_imgui target (compiled as C++ only).
#pragma once

// C linkage for backend entry points so the engine (C11) can call ImGui_Impl* declared in cimgui_impl.h.
#define IMGUI_IMPL_API extern "C"
// Vulkan backend uses volk's function pointers instead of loader prototypes.
#define IMGUI_IMPL_VULKAN_USE_VOLK
