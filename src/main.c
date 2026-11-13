#include <stdio.h>
#include <string.h>

#include <volk.h>
#include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <cglm/version.h>
#include <stb_ds.h>
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <cimgui_impl.h>
#include <tracy/TracyC.h>

// Prints the version of every linked dependency. Also proves each one links (T1).
static int print_versions(void)
{
    VkResult vr = volkInitialize();
    uint32_t inst = vr == VK_SUCCESS ? volkGetInstanceVersion() : 0;
    printf("vulkan headers  %u.%u.%u\n", VK_API_VERSION_MAJOR(VK_HEADER_VERSION_COMPLETE),
           VK_API_VERSION_MINOR(VK_HEADER_VERSION_COMPLETE), VK_API_VERSION_PATCH(VK_HEADER_VERSION_COMPLETE));
    printf("vulkan loader   %s %u.%u.%u\n", vr == VK_SUCCESS ? "found" : "NOT FOUND", VK_API_VERSION_MAJOR(inst),
           VK_API_VERSION_MINOR(inst), VK_API_VERSION_PATCH(inst));
    printf("volk            %d\n", VOLK_HEADER_VERSION);

    VkResult (*volatile vma_create)(const VmaAllocatorCreateInfo *, VmaAllocator *) = vmaCreateAllocator;
    printf("vma             %s\n", vma_create ? "linked" : "missing");

    printf("glfw            %s\n", glfwGetVersionString());

    vec3 x = { 1, 0, 0 }, y = { 0, 1, 0 }, z;
    glm_vec3_cross(x, y, z);
    printf("cglm            %d.%d.%d (x cross y = %.0f %.0f %.0f)\n", CGLM_VERSION_MAJOR, CGLM_VERSION_MINOR,
           CGLM_VERSION_PATCH, z[0], z[1], z[2]);

    int *arr = NULL;
    for (int i = 0; i < 4; i++) arrput(arr, i);
    printf("stb_ds          arrlen=%d\n", (int)arrlen(arr));
    arrfree(arr);

    bool (*volatile imgui_vk_init)(ImGui_ImplVulkan_InitInfo *) = ImGui_ImplVulkan_Init;
    bool (*volatile imgui_glfw_init)(GLFWwindow *, bool) = ImGui_ImplGlfw_InitForVulkan;
    printf("imgui           %s (backends %s)\n", igGetVersion(),
           imgui_vk_init && imgui_glfw_init ? "linked" : "missing");

#ifdef TRACY_ENABLE
    printf("tracy           enabled\n");
#else
    printf("tracy           disabled\n");
#endif
    return 0;
}

int main(int argc, char **argv)
{
    TracyCZoneN(zone, "main", 1);
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) rc = print_versions();
    }
    TracyCZoneEnd(zone);
    return rc;
}
