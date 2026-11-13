# Engine-only warning and sanitizer flags. Third-party targets never call this.

function(hd_target_options target)
    if(MSVC)
        # Covers both cl and clang-cl (both define MSVC and take cl-style flags).
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:preprocessor
            $<$<BOOL:${HD_WERROR}>:/WX>)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow -Wno-unused-parameter
            $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes -Werror=implicit-function-declaration>
            $<$<BOOL:${HD_WERROR}>:-Werror>)
        if(HD_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
    # Asserts on in Debug and RelWithDebInfo (CMake's RelWithDebInfo defines NDEBUG by default).
    target_compile_definitions(${target} PRIVATE
        $<$<CONFIG:Debug>:HD_DEBUG=1>
        $<$<CONFIG:Debug,RelWithDebInfo>:HD_ASSERTS=1>)
endfunction()
