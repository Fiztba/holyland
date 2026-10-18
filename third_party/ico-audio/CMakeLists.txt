# The software SPU2 from ico-pc (MIT, see LICENSE and UPSTREAM.md): integer-only code with no
# platform dependency. port/audio drives it.
add_library(ico_audio STATIC adpcm.c spu2.c spu2_sd.c spu2_tables.c)
target_include_directories(ico_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(KF4_TESTS)
    add_executable(ico_adpcm_test test/adpcm_test.c)
    target_link_libraries(ico_adpcm_test PRIVATE ico_audio)
    add_executable(ico_spu2_test test/spu2_test.c)
    target_link_libraries(ico_spu2_test PRIVATE ico_audio)
    add_executable(ico_spu2_bench test/spu2_bench.c)
    target_link_libraries(ico_spu2_bench PRIVATE ico_audio)
    if(NOT WIN32)
        target_link_libraries(ico_spu2_test PRIVATE m)
    endif()
    add_test(NAME ico_adpcm COMMAND ico_adpcm_test)
    add_test(NAME ico_spu2 COMMAND ico_spu2_test)
    add_test(NAME ico_spu2_render_crc COMMAND ico_spu2_bench --check)
endif()
