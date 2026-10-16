# The PC port of King's Field IV: the decomp's game code (src/game) compiled for the PC, with the PS2
# side (port/) under it. See docs/port/README.md. The PS2 build of the decomp (build.sh) is separate.
cmake_minimum_required(VERSION 3.20)
project(kf4 C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The PC port is 64-bit only")
endif()
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "The game code is GNU C: build it with clang or gcc (see docs/port/BUILDING.md)")
endif()

# ---- How the game code is compiled ---------------------------------------------------------------
# ee-gcc 2.9's behaviour where the game depends on it: no type-based alias analysis, wrapping signed
# overflow, no fused multiply-add, signed char, GCC's bit-field layout (also on Windows).
set(KF4_GAME_FLAGS
    -fno-strict-aliasing -fwrapv -ffp-contract=off -fno-fast-math -fsigned-char -fno-common
    -fgnu89-inline)
if(WIN32)
    list(APPEND KF4_GAME_FLAGS -mno-ms-bitfields)
endif()
# The step-0 rules (docs/port/RULES.md) as errors: nothing may squeeze a 64-bit pointer into 32 bits
# or call a function it has not declared.
set(KF4_GAME_ERRORS
    -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast -Werror=implicit-function-declaration
    -Werror=implicit-int -Werror=int-conversion -Werror=return-type)
if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    # (clang only) an address in a 64-bit integer must not be narrowed back to 32 bits without a cast
    list(APPEND KF4_GAME_ERRORS -Werror=incompatible-function-pointer-types -Werror=shorten-64-to-32
         -Wno-unused-but-set-variable -Wno-unused-variable -Wno-unused-function
         -Wno-missing-braces -Wno-parentheses-equality)
endif()

# ---- The PS2 side ---------------------------------------------------------------------------------
add_library(kf4_port STATIC
    port/platform/memory.c
    port/platform/ee_address.c
    port/math/libvu0.c
    port/sdk/stubs.c
    port/sdk/game_stubs.c)
target_include_directories(kf4_port PUBLIC port/include)
target_compile_options(kf4_port PRIVATE ${KF4_GAME_FLAGS})
if(NOT WIN32)
    target_link_libraries(kf4_port PUBLIC m)
endif()

# ---- The game, one object library per folder of src/game ------------------------------------------
# The libdev members the decomp keeps as src/game/func_0022*.c are PS2 debugging code: not built.
file(GLOB KF4_GAME_FOLDERS LIST_DIRECTORIES true RELATIVE ${CMAKE_SOURCE_DIR}/src/game
     ${CMAKE_SOURCE_DIR}/src/game/*)
set(KF4_GAME_TARGETS)
foreach(folder ${KF4_GAME_FOLDERS})
    if(IS_DIRECTORY ${CMAKE_SOURCE_DIR}/src/game/${folder})
        file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/${folder}/*.c)
        set(target game_${folder})
        add_library(${target} OBJECT ${sources})
        list(APPEND KF4_GAME_TARGETS ${target})
    endif()
endforeach()
file(GLOB top_sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/game/*.c)
list(FILTER top_sources EXCLUDE REGEX "/func_0022[0-9A-F]+\\.c$")
add_library(game_top OBJECT ${top_sources})
list(APPEND KF4_GAME_TARGETS game_top)
# the game's main is called by the host's (port/platform/main_host.c)
set_source_files_properties(${CMAKE_SOURCE_DIR}/src/game/main.c PROPERTIES
    COMPILE_DEFINITIONS "main=kf4_game_main")

foreach(target ${KF4_GAME_TARGETS})
    target_include_directories(${target} PRIVATE src/game port/include)
    target_compile_options(${target} PRIVATE ${KF4_GAME_FLAGS} ${KF4_GAME_ERRORS})
endforeach()
add_custom_target(game DEPENDS ${KF4_GAME_TARGETS})

# ---- The program ----------------------------------------------------------------------------------
set(KF4_GAME_OBJECTS)
foreach(target ${KF4_GAME_TARGETS})
    list(APPEND KF4_GAME_OBJECTS $<TARGET_OBJECTS:${target}>)
endforeach()
add_executable(kf4 port/platform/main_host.c ${KF4_GAME_OBJECTS})
target_compile_options(kf4 PRIVATE ${KF4_GAME_FLAGS})
target_link_libraries(kf4 PRIVATE kf4_port)
