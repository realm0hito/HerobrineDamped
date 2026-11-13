#include "base/base.h"

#include <volk.h>

// Vulkan-Headers no longer ships vk_enum_string_helper.h (moved to Vulkan-Utility-Libraries).
const char *vk_result_name(i32 result)
{
    switch ((VkResult)result) {
#define X(r)                                                                                                           \
    case r: return #r;
        X(VK_SUCCESS)
        X(VK_NOT_READY)
        X(VK_TIMEOUT)
        X(VK_EVENT_SET)
        X(VK_EVENT_RESET)
        X(VK_INCOMPLETE)
        X(VK_ERROR_OUT_OF_HOST_MEMORY)
        X(VK_ERROR_OUT_OF_DEVICE_MEMORY)
        X(VK_ERROR_INITIALIZATION_FAILED)
        X(VK_ERROR_DEVICE_LOST)
        X(VK_ERROR_MEMORY_MAP_FAILED)
        X(VK_ERROR_LAYER_NOT_PRESENT)
        X(VK_ERROR_EXTENSION_NOT_PRESENT)
        X(VK_ERROR_FEATURE_NOT_PRESENT)
        X(VK_ERROR_INCOMPATIBLE_DRIVER)
        X(VK_ERROR_TOO_MANY_OBJECTS)
        X(VK_ERROR_FORMAT_NOT_SUPPORTED)
        X(VK_ERROR_FRAGMENTED_POOL)
        X(VK_ERROR_UNKNOWN)
        X(VK_ERROR_OUT_OF_POOL_MEMORY)
        X(VK_ERROR_INVALID_EXTERNAL_HANDLE)
        X(VK_ERROR_FRAGMENTATION)
        X(VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS)
        X(VK_PIPELINE_COMPILE_REQUIRED)
        X(VK_ERROR_SURFACE_LOST_KHR)
        X(VK_ERROR_NATIVE_WINDOW_IN_USE_KHR)
        X(VK_SUBOPTIMAL_KHR)
        X(VK_ERROR_OUT_OF_DATE_KHR)
        X(VK_ERROR_INCOMPATIBLE_DISPLAY_KHR)
        X(VK_ERROR_VALIDATION_FAILED_EXT)
        X(VK_ERROR_INVALID_SHADER_NV)
        X(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT)
#undef X
    default: return "VK_<unknown>";
    }
}

void vk_check_failed(i32 result, const char *expr, const char *file, int line)
{
    panic(file, line, "%s returned %s (%d)", expr, vk_result_name(result), result);
}
