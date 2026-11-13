add_executable(engine
    main.c
    base/arena.c
    base/ds_impl.c
    base/log.c
    base/mem.c
    base/os.c
    base/selftest.c
    gpu/vk_check.c
    gpu/vma_impl.cpp
)
if(WIN32)
    target_sources(engine PRIVATE base/os_win32.c)
else()
    target_sources(engine PRIVATE base/os_posix.c)
endif()
hd_target_options(engine)
target_include_directories(engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine PRIVATE
    volk
    VulkanMemoryAllocator
    glfw
    hd_cglm
    hd_stb
    hd_imgui
    hd_tracy)

add_test(NAME selftest COMMAND engine --selftest WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
