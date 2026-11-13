cmake_minimum_required(VERSION 3.25)

project(HerobrineDamped VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed; use a preset (cmake --preset <name>).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# C++ only for third-party targets and src/gpu/vma_impl.cpp.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(HD_WERROR "Treat engine warnings as errors" OFF)
option(HD_ASAN "Enable AddressSanitizer + UBSan on engine code (GCC/Clang)" OFF)

include(cmake/CompilerOptions.cmake)

add_subdirectory(src)
