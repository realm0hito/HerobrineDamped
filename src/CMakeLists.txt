add_executable(engine
    main.c
)
hd_target_options(engine)
target_include_directories(engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
