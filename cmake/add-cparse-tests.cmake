# SPDX-License-Identifier: MIT
#
# Tests of the cparse C front end and of the reflection built on it.
#
# The cases of test/cparse/cases.txt run against expected output. With
# libclang they run against libclang too. The round trip tests generate C from
# the headers in test/reflection-roundtrip and read it back.

function(add_cparse_tests)
    set(_test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test")
    set(_tools_dir "${CMAKE_CURRENT_BINARY_DIR}/test")

    # the tools need the private symbols of the static library
    foreach(_tool cparse-tool reflection-backend-dump)
        if(_tool STREQUAL "cparse-tool")
            set(_tool_src "${_test_dir}/cparse/cparse-tool.c")
        else()
            set(_tool_src "${_test_dir}/reflection-backend-dump.c")
        endif()
        add_executable(${_tool} "${_tool_src}")
        target_compile_options(${_tool} PRIVATE ${COMMON_C_FLAGS})
        target_compile_definitions(${_tool} PRIVATE
            HAVE_CONFIG_H
            ${COMMON_C_DEFINITIONS}
        )
        target_include_directories(${_tool} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
            ${CMAKE_CURRENT_SOURCE_DIR}/src/util
            ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
            ${CMAKE_CURRENT_SOURCE_DIR}/src/generic
            ${CMAKE_CURRENT_SOURCE_DIR}/src/reflection
            ${CMAKE_CURRENT_SOURCE_DIR}/src/cparse
            ${CMAKE_CURRENT_BINARY_DIR}
        )
        link_whole_archive_static(${_tool} fyaml_static)
        target_link_libraries(${_tool} PRIVATE
            $<$<AND:$<BOOL:${HAVE_LIBCLANG}>,$<NOT:$<BOOL:${LIBCLANG_DLOPEN}>>>:${LIBCLANG_LIBS}>
            $<$<BOOL:${LIBCLANG_DLOPEN}>:${CMAKE_DL_LIBS}>
        )
        if(HAVE_LIBCLANG)
            target_link_directories(${_tool} PRIVATE ${LIBCLANG_LINK_DIRS})
        endif()
        if(HAVE_ASAN AND NOT MSVC)
            target_link_options(${_tool} PRIVATE ${ASAN_C_FLAGS})
        endif()
        add_dependencies(${_tool} fyaml_static)
        set_target_properties(${_tool} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_tools_dir}")
        list(APPEND TEST_DEPENDENCIES ${_tool})
    endforeach()

    # libclang prints the declarations in the format of cparse-tool
    if(HAVE_LIBCLANG)
        add_executable(cparse-libclang-dump "${_test_dir}/cparse/libclang-dump.c")
        target_compile_options(cparse-libclang-dump PRIVATE ${COMMON_C_FLAGS})
        target_include_directories(cparse-libclang-dump PRIVATE ${LIBCLANG_INCLUDES})
        target_link_directories(cparse-libclang-dump PRIVATE ${LIBCLANG_LINK_DIRS})
        target_link_libraries(cparse-libclang-dump PRIVATE ${LIBCLANG_LIBS})
        set_target_properties(cparse-libclang-dump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${_tools_dir}")
        list(APPEND TEST_DEPENDENCIES cparse-libclang-dump)
    endif()
    set(TEST_DEPENDENCIES ${TEST_DEPENDENCIES} PARENT_SCOPE)

    find_program(CLANG_EXECUTABLE NAMES clang clang-20 clang-19 clang-18 clang-17 clang-16 clang-15)

    # the cases
    file(STRINGS "${_test_dir}/cparse/cases.txt" _cases REGEX "^[^#]")
    foreach(_case ${_cases})
        string(REGEX MATCH "^[^ ]+" _name "${_case}")
        string(REGEX REPLACE "^[^ ]+ ([^ ]+) .*$" "\\1" _kind "${_case}")

        add_test(
            NAME "cparse/${_name}"
            COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/cparse/run-cparse-test.sh" expected "${_name}"
        )
        set_tests_properties("cparse/${_name}" PROPERTIES
            LABELS "cparse"
            ENVIRONMENT "CPARSE_TOOL=$<TARGET_FILE:cparse-tool>"
        )

        if(HAVE_LIBCLANG AND (_kind STREQUAL "decls" OR _kind STREQUAL "comments"))
            add_test(
                NAME "cparse-libclang/${_name}"
                COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/cparse/run-cparse-test.sh" libclang "${_name}"
            )
            set_tests_properties("cparse-libclang/${_name}" PROPERTIES
                SKIP_RETURN_CODE 77
                LABELS "cparse;libclang"
                ENVIRONMENT "CPARSE_TOOL=$<TARGET_FILE:cparse-tool>;CPARSE_LIBCLANG_DUMP=$<TARGET_FILE:cparse-libclang-dump>"
            )
        endif()

        if(CLANG_EXECUTABLE AND _kind STREQUAL "pp")
            add_test(
                NAME "cparse-clang/${_name}"
                COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/cparse/run-cparse-test.sh" clangpp "${_name}"
            )
            set_tests_properties("cparse-clang/${_name}" PROPERTIES
                LABELS "cparse;clang"
                ENVIRONMENT "CPARSE_TOOL=$<TARGET_FILE:cparse-tool>;CLANG=${CLANG_EXECUTABLE}"
            )
        endif()
    endforeach()

    # generate C from a header, read it back, compare the two reflections
    file(GLOB _headers "${_test_dir}/reflection-roundtrip/*.h")
    foreach(_header ${_headers})
        get_filename_component(_name "${_header}" NAME_WE)

        add_test(
            NAME "reflection-roundtrip/${_name}"
            COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/run-reflection-roundtrip.sh" roundtrip "${_header}"
        )
        set_tests_properties("reflection-roundtrip/${_name}" PROPERTIES
            LABELS "reflection;roundtrip"
            ENVIRONMENT "BACKEND_DUMP=$<TARGET_FILE:reflection-backend-dump>"
        )

        # the 32 bit layout rules, on any host
        add_test(
            NAME "reflection-roundtrip-i386/${_name}"
            COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/run-reflection-roundtrip.sh" roundtrip-i386 "${_header}"
        )
        set_tests_properties("reflection-roundtrip-i386/${_name}" PROPERTIES
            LABELS "reflection;roundtrip"
            ENVIRONMENT "BACKEND_DUMP=$<TARGET_FILE:reflection-backend-dump>"
        )

        # the expected files are for a 64 bit host
        if(EXISTS "${_test_dir}/reflection-roundtrip/${_name}.expected" AND CMAKE_SIZEOF_VOID_P EQUAL 8)
            add_test(
                NAME "reflection-layout/${_name}"
                COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/run-reflection-roundtrip.sh" layout "${_header}"
            )
            set_tests_properties("reflection-layout/${_name}" PROPERTIES
                LABELS "reflection;layout"
                ENVIRONMENT "BACKEND_DUMP=$<TARGET_FILE:reflection-backend-dump>"
            )
        endif()

        if(HAVE_LIBCLANG)
            add_test(
                NAME "reflection-backends/${_name}"
                COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/run-reflection-roundtrip.sh" backends "${_header}"
            )
            set_tests_properties("reflection-backends/${_name}" PROPERTIES
                LABELS "reflection;libclang"
                ENVIRONMENT "BACKEND_DUMP=$<TARGET_FILE:reflection-backend-dump>"
            )
        endif()
    endforeach()

    # a header read with the macros of a file, and without the compiled-in ones
    file(GLOB _macro_headers "${_test_dir}/reflection-macros/*.h")
    foreach(_header ${_macro_headers})
        get_filename_component(_name "${_header}" NAME_WE)

        add_test(
            NAME "reflection-macros/${_name}"
            COMMAND "${BASH_EXECUTABLE}" "${_test_dir}/run-reflection-roundtrip.sh" macros "${_header}"
        )
        set_tests_properties("reflection-macros/${_name}" PROPERTIES
            LABELS "reflection;macros"
            ENVIRONMENT "BACKEND_DUMP=$<TARGET_FILE:reflection-backend-dump>"
        )
    endforeach()
endfunction()

# A fuzz run of the input of cparse. It is not part of "all" or of ctest: build
# and run it by hand with "cmake --build . --target cparse-fuzz". The variable
# CPARSE_FUZZ_ARGS in the cache sets the options, for example "-s 7 -n 200000".
function(add_cparse_fuzz_target)
    set(_test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test")

    add_executable(cparse-fuzz-input EXCLUDE_FROM_ALL "${_test_dir}/cparse/fuzz-input.c")
    target_compile_options(cparse-fuzz-input PRIVATE ${COMMON_C_FLAGS})
    target_compile_definitions(cparse-fuzz-input PRIVATE
        HAVE_CONFIG_H
        ${COMMON_C_DEFINITIONS}
    )
    target_include_directories(cparse-fuzz-input PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/src/util
        ${CMAKE_CURRENT_SOURCE_DIR}/src/allocator
        ${CMAKE_CURRENT_SOURCE_DIR}/src/generic
        ${CMAKE_CURRENT_SOURCE_DIR}/src/reflection
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cparse
        ${CMAKE_CURRENT_BINARY_DIR}
    )
    link_whole_archive_static(cparse-fuzz-input fyaml_static)
    if(HAVE_ASAN AND NOT MSVC)
        target_link_options(cparse-fuzz-input PRIVATE ${ASAN_C_FLAGS})
    endif()
    add_dependencies(cparse-fuzz-input fyaml_static)
    set_target_properties(cparse-fuzz-input PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test")

    file(GLOB _fixtures "${_test_dir}/cparse/*.c" "${_test_dir}/cparse/*.h")
    list(FILTER _fixtures EXCLUDE REGEX "(cparse-tool|libclang-dump|fuzz-input)\\.c$")
    set(CPARSE_FUZZ_ARGS "" CACHE STRING "options of the cparse-fuzz target, for example -s 7 -n 200000")
    separate_arguments(_fuzz_args NATIVE_COMMAND "${CPARSE_FUZZ_ARGS}")
    add_custom_target(cparse-fuzz
        COMMAND cparse-fuzz-input ${_fuzz_args} ${_fixtures}
        DEPENDS cparse-fuzz-input
        COMMENT "Fuzzing the input of cparse"
        USES_TERMINAL
    )
endfunction()
