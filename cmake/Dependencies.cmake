# Third-party targets. Everything lives in external/ as pinned git submodules (PLAN.md §10).
# All are added SYSTEM (headers don't trigger engine warnings) and EXCLUDE_FROM_ALL (built only if linked).

set(HD_EXT ${PROJECT_SOURCE_DIR}/external)

if(NOT EXISTS ${HD_EXT}/volk/volk.h OR NOT EXISTS ${HD_EXT}/cimgui/imgui/imgui.h)
    message(FATAL_ERROR "Submodules missing. Run: git submodule update --init --recursive")
endif()

# --- Vulkan headers (pinned; the installed SDK only provides glslc + validation layers) ----------------------------
set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory(${HD_EXT}/Vulkan-Headers ${CMAKE_BINARY_DIR}/external/Vulkan-Headers SYSTEM EXCLUDE_FROM_ALL)

# --- volk: Vulkan loader; no prototypes anywhere, so every vk* call goes through volk ------------------------------
set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${HD_EXT}/volk ${CMAKE_BINARY_DIR}/external/volk SYSTEM EXCLUDE_FROM_ALL)
target_link_libraries(volk PUBLIC Vulkan::Headers)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

# --- VMA: header-only; implementation compiled in src/gpu/vma_impl.cpp ---------------------------------------------
add_subdirectory(${HD_EXT}/VulkanMemoryAllocator ${CMAKE_BINARY_DIR}/external/VulkanMemoryAllocator SYSTEM EXCLUDE_FROM_ALL)
target_link_libraries(VulkanMemoryAllocator INTERFACE volk)
# Function pointers come from vmaImportVulkanFunctionsFromVolk().
target_compile_definitions(VulkanMemoryAllocator INTERFACE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

# --- GLFW ----------------------------------------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
add_subdirectory(${HD_EXT}/glfw ${CMAKE_BINARY_DIR}/external/glfw SYSTEM EXCLUDE_FROM_ALL)
target_compile_definitions(glfw INTERFACE GLFW_INCLUDE_NONE)

# --- cglm / stb: header-only, include paths only (cglm's own CMake adds global flags + CPack; not needed) ---------
add_library(hd_cglm INTERFACE)
target_include_directories(hd_cglm SYSTEM INTERFACE ${HD_EXT}/cglm/include)

add_library(hd_stb INTERFACE)
target_include_directories(hd_stb SYSTEM INTERFACE ${HD_EXT}/stb)

# --- Dear ImGui through cimgui (C++ isolated here; engine sees only cimgui.h / cimgui_impl.h) ----------------------
# cimgui's own CMakeLists doesn't build backends, so the target is defined here.
set(HD_IMGUI ${HD_EXT}/cimgui/imgui)
add_library(hd_imgui STATIC
    ${HD_EXT}/cimgui/cimgui.cpp
    ${HD_EXT}/cimgui/cimgui_impl.cpp
    ${HD_IMGUI}/imgui.cpp
    ${HD_IMGUI}/imgui_demo.cpp
    ${HD_IMGUI}/imgui_draw.cpp
    ${HD_IMGUI}/imgui_tables.cpp
    ${HD_IMGUI}/imgui_widgets.cpp
    ${HD_IMGUI}/backends/imgui_impl_glfw.cpp
    ${HD_IMGUI}/backends/imgui_impl_vulkan.cpp)
target_include_directories(hd_imgui SYSTEM PUBLIC ${HD_EXT}/cimgui)
target_include_directories(hd_imgui SYSTEM PRIVATE ${HD_IMGUI} ${HD_IMGUI}/backends ${PROJECT_SOURCE_DIR}/cmake/imgui)
# IMGUI_USER_CONFIG carries IMGUI_IMPL_API=extern "C" (backend functions callable from C) and the volk switch.
target_compile_definitions(hd_imgui
    PRIVATE IMGUI_USER_CONFIG="hd_imconfig.h"
    PUBLIC CIMGUI_USE_GLFW CIMGUI_USE_VULKAN IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)
target_link_libraries(hd_imgui PUBLIC volk PRIVATE glfw)
if(WIN32)
    target_link_libraries(hd_imgui PRIVATE imm32)
endif()

# --- Tracy (optional). Include path always present so TracyC.h macros compile to no-ops when disabled -------------
add_library(hd_tracy INTERFACE)
target_include_directories(hd_tracy SYSTEM INTERFACE ${HD_EXT}/tracy/public)
if(HD_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_STATIC ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    add_subdirectory(${HD_EXT}/tracy ${CMAKE_BINARY_DIR}/external/tracy SYSTEM EXCLUDE_FROM_ALL)
    target_link_libraries(hd_tracy INTERFACE Tracy::TracyClient)
endif()
