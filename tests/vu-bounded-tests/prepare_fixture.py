# SPDX-License-Identifier: GPL-3.0-or-later
"""Derive an authored native verifier in an external build tree.

Canonical runtime and fixture sources are read-only. Nothing from a retail
disc, executable, recording or generated catalog is bundled in this package.
"""
import argparse
from pathlib import Path
import sys
sys.dont_write_bytecode = True
from authored_program import generate

BASE_FILES = (
    'CMakeLists.txt', 'prepare_sources.py', 'reference39-hashes.json',
    'verifier.cpp', 'metadata_cases.inc', 'pipeline_cases.inc',
    'unrelated_stubs.cpp',
)


def inside(path, directory):
    return path == directory or directory in path.parents


def replace_once(text, before, after):
    if text.count(before) != 1:
        raise ValueError(f'Expected one verifier/CMake anchor: {before[:80]!r}')
    return text.replace(before, after, 1)


def write_changed(path, text):
    data = text.encode('utf-8')
    if not path.is_file() or path.read_bytes() != data:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def prepare(source, destination, scratch, build_root, candidate, reference):
    source, destination, scratch, build_root, candidate, reference = (
        path.resolve() for path in
        (source, destination, scratch, build_root, candidate, reference))
    here = Path(__file__).resolve().parent
    if candidate == reference:
        raise ValueError('Candidate and patch-39 reference must be separate runtime directories')
    repository = next((parent for parent in here.parents
        if (parent / '.git').exists() or
           ((parent / 'AGENTS.md').is_file() and (parent / 'tests').is_dir())), here)
    for protected in (repository, source, here, candidate, reference):
        if inside(build_root, protected) or inside(protected, build_root):
            raise ValueError('Build scratch must be separate from all canonical source trees')
    if not inside(destination, build_root) or not inside(scratch, build_root):
        raise ValueError('Derived fixtures and authored inputs must remain in the supplied build tree')
    if inside(destination, scratch) or inside(scratch, destination):
        raise ValueError('Derived source and generated authored input directories must be separate')

    # Read and validate every adaptation before creating output. Keep copying
    # limited to the source-only files the differential fixture actually uses.
    derived = {name: (source / name).read_text(encoding='utf-8') for name in BASE_FILES}
    derived['authored_native_cases.inc'] = (here / 'authored_native_cases.inc').read_text(encoding='utf-8')
    derived['check_native_control.py'] = (here / 'check_native_control.py').read_text(encoding='utf-8')
    for name in ('scheduler_export.inc','cooperative_visibility.inc','force_fallback_source.py'):
        derived[name]=(here/name).read_text()
    prepare=derived['prepare_sources.py']
    marker='                text = replay_access(text, name == "reference")'
    extra='\n                if name == "candidate":\n                    text=text.replace("    VU1State &state()", "    bool verifyDisableNative=false;\\n    void verifyScheduler(uint64_t *out) const;\\n    uint64_t delayPForTests() { for(uint32_t i=0;i<2;++i)if(m_efuMask&(1u<<i)){const uint64_t d=m_efu[i].readyCycle-m_cycle;++m_efu[i].readyCycle;return d;}return 0;}\\n    VU1State &state()")\n'
    prepare=replace_once(prepare,marker,marker+extra)
    marker='                text += classifier_export(text, name)'
    extra='                if name == "candidate":\n                    text=text.replace("m_nativeCatalogMatched = matchesNativeCatalogCache();", "m_nativeCatalogMatched = !verifyDisableNative && matchesNativeCatalogCache();")\n                    text+=(HERE/"scheduler_export.inc").read_text()\n'
    prepare=replace_once(prepare,marker,extra+marker)
    derived['prepare_sources.py']=prepare

    derived['verifier.cpp'] = replace_once(derived['verifier.cpp'],
        '#include "metadata_cases.inc"',
        '#include "metadata_cases.inc"\n#include "authored_native_cases.inc"\n#include "cooperative_visibility.inc"')
    derived['verifier.cpp'] = replace_once(derived['verifier.cpp'],
        '            else if (arg == "--metadata")',
        '            else if (arg == "--authored-native") { authoredNativeSynthetic(); '
        'if (!control.empty()) throw std::runtime_error("negative control was not detected"); '
        'std::cout << "PASS authored-native checks=" << checks << " calls=" << calls << \'\\n\'; return 0; }\n'
        '            else if (arg == "--bounded-visible") { cooperativeVisibility(); return 0; }\n'
        '            else if (arg == "--bounded-p-control") { boundedPTailControl(); return 0; }\n'
        '            else if (arg == "--bounded-fallback") { boundedForcedFallback(); return 0; }\n'
        '            else if (arg == "--metadata")')
    cmake = replace_once(derived['CMakeLists.txt'],
        'target_compile_features(kfiv_vu_performance_verify PRIVATE cxx_std_20)', '''
target_compile_features(kfiv_vu_performance_verify PRIVATE cxx_std_20)
if(NOT EXISTS "${VU_NATIVE_CATALOG}" OR NOT EXISTS "${VU_AUTHORED_CATALOG_DIR}/authored_catalog.inc")
    message(FATAL_ERROR "The outer native fixture must generate the authored catalog and header")
endif()
set_property(SOURCE "${generated}/candidate/src/lib/vu/ps2_vu1_core.cpp" APPEND PROPERTY
    COMPILE_DEFINITIONS "PS2X_VU1_NATIVE_CATALOG=\\"${VU_NATIVE_CATALOG}\\"")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${VU_NATIVE_CATALOG}" "${VU_AUTHORED_CATALOG_DIR}/authored_catalog.inc")
target_compile_definitions(kfiv_vu_performance_verify PRIVATE VU_SPECIALIZATION_VERIFY_COUNTER=1)
target_sources(kfiv_vu_performance_verify PRIVATE "${VU_NATIVE_BOUNDED_CPP}")
get_filename_component(bounded_directory "${VU_NATIVE_BOUNDED_HEADER}" DIRECTORY)
target_include_directories(kfiv_vu_performance_verify PRIVATE "${bounded_directory}")
set_property(SOURCE "${VU_NATIVE_BOUNDED_CPP}" PROPERTY SKIP_UNITY_BUILD_INCLUSION TRUE)
if(MSVC)
    set_property(SOURCE "${VU_NATIVE_BOUNDED_CPP}" APPEND PROPERTY COMPILE_OPTIONS /fp:strict)
else()
    set_property(SOURCE "${VU_CANDIDATE_RUNTIME_DIR}/src/lib/ps2_memory.cpp" APPEND PROPERTY COMPILE_OPTIONS -msse4.1)
endif()

target_include_directories(kfiv_vu_performance_verify PRIVATE "${VU_AUTHORED_CATALOG_DIR}")
''')
    cmake=cmake.replace('set_source_files_properties(${candidate_sources} ${candidate_lift_sources}', 'set_source_files_properties("${VU_NATIVE_BOUNDED_CPP}" ${candidate_sources} ${candidate_lift_sources}')
    if cmake.count('enable_testing()') != 1:
        raise ValueError('Expected one base CTest registration boundary')
    # These tests specifically execute the compiled authored catalog. The base
    # package retains its independent generic synthetic/metadata/replay suite.
    cmake = cmake[:cmake.index('enable_testing()')] + """
# Force only the emitted companion's architecture conditional in build scratch.
# The production generator has no testing option or environment switch.
set(forced_companion "${CMAKE_CURRENT_BINARY_DIR}/catalog.forced-fallback.cpp")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/force_fallback_source.py"
    --source "${VU_NATIVE_BOUNDED_CPP}" --destination "${forced_companion}"
    RESULT_VARIABLE forced_status ERROR_VARIABLE forced_error)
if(NOT forced_status EQUAL 0)
    message(FATAL_ERROR "${forced_error}")
endif()
get_target_property(forced_sources kfiv_vu_performance_verify SOURCES)
list(REMOVE_ITEM forced_sources "${VU_NATIVE_BOUNDED_CPP}")
add_executable(kfiv_vu_bounded_fallback_verify ${forced_sources} "${forced_companion}")
foreach(property IN ITEMS COMPILE_FEATURES COMPILE_OPTIONS COMPILE_DEFINITIONS
        INCLUDE_DIRECTORIES LINK_OPTIONS LINK_LIBRARIES INTERPROCEDURAL_OPTIMIZATION)
    get_target_property(value kfiv_vu_performance_verify ${property})
    if(NOT value STREQUAL "value-NOTFOUND")
        set_property(TARGET kfiv_vu_bounded_fallback_verify PROPERTY ${property} "${value}")
    endif()
endforeach()
enable_testing()
add_test(NAME vu_bounded_forced_target_fallback COMMAND kfiv_vu_bounded_fallback_verify --bounded-fallback)
add_test(NAME vu_bounded_authored_exact COMMAND kfiv_vu_performance_verify --authored-native)
add_test(NAME vu_bounded_cooperative_visibility COMMAND kfiv_vu_performance_verify --bounded-visible)
add_test(NAME vu_bounded_pending_p_negative_control COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/check_native_control.py" --executable "$<TARGET_FILE:kfiv_vu_performance_verify>")
get_property(vu_registered_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${vu_registered_tests} PROPERTIES ENVIRONMENT "PS2X_VU1_LIFT=0" TIMEOUT 180)
"""
    derived['CMakeLists.txt'] = cmake
    generate(scratch)
    for name, content in derived.items():
        write_changed(destination / name, content)
    print(f'Prepared authored native verifier sources in external build scratch: {destination}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('source-fixture', 'destination', 'scratch', 'build-root',
                   'candidate-runtime', 'reference-runtime'):
        parser.add_argument('--' + option, type=Path, required=True)
    args = parser.parse_args()
    prepare(args.source_fixture, args.destination, args.scratch, args.build_root,
            args.candidate_runtime, args.reference_runtime)


if __name__ == '__main__':
    main()
