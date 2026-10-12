cmake_minimum_required(VERSION 3.21)
project(kfiv_ee_initthread_tests LANGUAGES CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/PackagePaths.cmake")
kfiv_require_runtime()

set(FFMPEG_DIR "${RUNTIME_BUILD_DIR}/ThirdParty/ffmpeg-prefix/src/ffmpeg_external" CACHE PATH "FFmpeg package root")
if(NOT MSVC)
    message(FATAL_ERROR "These regressions validate the native Windows MSVC runtime")
endif()

# The control compiles the actual thread syscalls with only the old InitThread
# result restored: the caller's id, with no topThread and no priority change.
file(READ "${PS2RECOMP_DIR}/ps2xRuntime/src/lib/Kernel/Syscalls/Thread.cpp" THREAD_SOURCE)
string(REPLACE "setReturnS32(ctx, ee.initThread());"
               "setReturnS32(ctx, EeScheduler::kMainThreadId);"
               CONTROL_SOURCE "${THREAD_SOURCE}")
if(CONTROL_SOURCE STREQUAL THREAD_SOURCE)
    message(FATAL_ERROR "Actual InitThread handler was not found; control cannot restore the original")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/Thread-initthread-control.cpp" "${CONTROL_SOURCE}")

function(add_initthread_test target)
    add_executable(${target} ee_initthread_tests.cpp
        "${PS2RECOMP_DIR}/ps2xTest/src/test_function_table.cpp")
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE PS2X_HAS_FFMPEG=1 _CRT_SECURE_NO_WARNINGS)
    target_include_directories(${target} PRIVATE
        "${PS2RECOMP_DIR}/ps2xRuntime/include"
        "${PS2RECOMP_DIR}/ps2xRuntime/src/lib/Kernel"
        "${PS2RECOMP_DIR}/ps2xRuntime/src/lib/Kernel/Syscalls"
        "${PS2RECOMP_DIR}/ps2xIOP/include")
    target_compile_options(${target} PRIVATE /W4 /permissive- /bigobj)
    target_link_options(${target} PRIVATE /STACK:16777216)
    target_link_libraries(${target} PRIVATE
        "${RUNTIME_BUILD_DIR}/ps2xRuntime/$<CONFIG>/ps2_runtime.lib"
        "${RUNTIME_BUILD_DIR}/ps2xIOP/$<CONFIG>/ps2_iop.lib"
        "${RUNTIME_BUILD_DIR}/_deps/raylib-build/raylib/$<CONFIG>/raylib.lib"
        "${FFMPEG_DIR}/bin/avcodec.lib" "${FFMPEG_DIR}/bin/avformat.lib"
        "${FFMPEG_DIR}/bin/avutil.lib" "${FFMPEG_DIR}/bin/swresample.lib"
        "${FFMPEG_DIR}/bin/swscale.lib"
        opengl32 glu32 winmm bcrypt secur32 ws2_32 user32 advapi32 ole32 shell32 gdi32)
    if(EXISTS "${RUNTIME_BUILD_DIR}/parallel-gs/gs")
        target_link_libraries(${target} PRIVATE
            "${RUNTIME_BUILD_DIR}/parallel-gs/gs/$<CONFIG>/parallel-gs.lib"
            "${RUNTIME_BUILD_DIR}/parallel-gs/Granite/vulkan/$<CONFIG>/granite-vulkan.lib"
            "${RUNTIME_BUILD_DIR}/parallel-gs/Granite/third_party/$<CONFIG>/granite-volk.lib"
            "${RUNTIME_BUILD_DIR}/parallel-gs/Granite/util/$<CONFIG>/granite-util.lib"
            "${RUNTIME_BUILD_DIR}/parallel-gs/Granite/math/$<CONFIG>/granite-math.lib")
    endif()
    add_custom_command(TARGET ${target} POST_BUILD COMMAND "${CMAKE_COMMAND}"
        "-Dsource_dir=${FFMPEG_DIR}/bin" "-Ddest_dir=$<TARGET_FILE_DIR:${target}>"
        -P "${PS2RECOMP_DIR}/ps2xRuntime/cmake/CopyFfmpegDlls.cmake")
endfunction()

add_initthread_test(kfiv_ee_initthread_tests)
add_initthread_test(kfiv_ee_initthread_control)
target_sources(kfiv_ee_initthread_control PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/Thread-initthread-control.cpp")
target_compile_definitions(kfiv_ee_initthread_control PRIVATE EXPECT_ORIGINAL_INITTHREAD=1)
enable_testing()
add_test(NAME kfiv_ee_initthread_sdk_regression COMMAND kfiv_ee_initthread_tests)
add_test(NAME kfiv_ee_initthread_original_starvation_control COMMAND kfiv_ee_initthread_control)
set_tests_properties(kfiv_ee_initthread_sdk_regression kfiv_ee_initthread_original_starvation_control PROPERTIES TIMEOUT 30)
