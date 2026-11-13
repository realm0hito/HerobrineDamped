add_executable(engine
    main.c
    base/ds_impl.c
    gpu/vma_impl.cpp
)
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
