cmake_minimum_required(VERSION 3.22)
project(RavenswatchInstantMastery LANGUAGES C CXX ASM_MASM)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build with an x64 MSVC toolchain (Visual Studio 2022 or newer).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(winhttp SHARED
    src/mod.cpp
    src/mastery_hook.asm
    src/proxy.cpp
    src/proxy.asm
    src/winhttp.def
)
# MASM does not create nested object directories itself. Keep assembly object
# names flat and distinct from the proxy.cpp object for Visual Studio builds.
set_source_files_properties(src/proxy.asm PROPERTIES
    VS_SETTINGS "ObjectFileName=$(IntDir)proxy_asm.obj")
set_source_files_properties(src/mastery_hook.asm PROPERTIES
    VS_SETTINGS "ObjectFileName=$(IntDir)mastery_hook_asm.obj")
target_compile_definitions(winhttp PRIVATE UNICODE _UNICODE)
target_compile_options(winhttp PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/EHsc>")
target_link_libraries(winhttp PRIVATE minhook bcrypt)
# DllCanUnloadNow/DllGetClassObject intentionally retain the system DLL's
# ordinals as well as their names, unlike ordinary COM DLL export libraries.
target_link_options(winhttp PRIVATE /IGNORE:4222)
set_target_properties(winhttp PROPERTIES PREFIX "" OUTPUT_NAME "winhttp")

include(CTest)
if(BUILD_TESTING)
    add_executable(proxy_smoke tests/proxy_smoke.cpp)
    target_compile_definitions(proxy_smoke PRIVATE UNICODE _UNICODE)
    target_compile_options(proxy_smoke PRIVATE /W4 /EHsc)
    add_test(NAME proxy_forwarding COMMAND proxy_smoke $<TARGET_FILE:winhttp>)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_tests.cpp")
        add_executable(mastery_native_tests
            tests/native_tests.cpp
            tests/native_fixture.asm
            src/mastery_hook.asm)
        set_source_files_properties(tests/native_fixture.asm PROPERTIES
            VS_SETTINGS "ObjectFileName=$(IntDir)native_fixture_asm.obj")
        target_include_directories(mastery_native_tests PRIVATE src)
        target_compile_options(mastery_native_tests PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/EHsc>")
        target_link_libraries(mastery_native_tests PRIVATE minhook)
        add_test(NAME mastery_native_hooks COMMAND mastery_native_tests)
    endif()
endif()
