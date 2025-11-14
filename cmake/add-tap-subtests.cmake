# Helper function to add individual TAP subtests from test scripts
# This mimics the behavior of autotools tap-driver.sh by registering each subtest individually

# Helper function to add a TAP test with fy-tool path via generator expression
# This enables proper support for multi-config generators (Visual Studio, Xcode)
# by passing the actual tool path at runtime rather than relying on hardcoded paths.
function(add_tap_test test_name suite_name test_id)
    cmake_parse_arguments(TAP "DISABLED;WILL_FAIL" "WORKING_DIR" "LABELS;EXTRA_ENV" ${ARGN})

    if(TAP_WORKING_DIR)
        set(_tap_working_dir "${TAP_WORKING_DIR}")
    else()
        set(_tap_working_dir "${CMAKE_CURRENT_BINARY_DIR}/test")
    endif()

    add_test(
        NAME "${test_name}"
        COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh"
            "${suite_name}" "${test_id}"
        WORKING_DIRECTORY "${_tap_working_dir}"
    )

    # Build environment
    set(base_env
        "FY_TOOL=$<TARGET_FILE:fy-tool>"
        "LIBFYAML_TEST=$<TARGET_FILE:libfyaml-test>"
        "TEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}/test"
        "YAML_TEST_SUITE=${yaml_test_suite_SOURCE_DIR}"
        "JSON_TEST_SUITE=${json_test_suite_SOURCE_DIR}"
    )
    if(TAP_EXTRA_ENV)
        list(APPEND base_env ${TAP_EXTRA_ENV})
    endif()

    set_tests_properties("${test_name}" PROPERTIES DEPENDS fy-tool)

    set_tests_properties("${test_name}" PROPERTIES ENVIRONMENT "${base_env}")

    if(TAP_LABELS)
        set_tests_properties("${test_name}" PROPERTIES LABELS "${TAP_LABELS}")
    endif()
    if(TAP_DISABLED)
        set_tests_properties("${test_name}" PROPERTIES DISABLED TRUE)
    endif()
    if(TAP_WILL_FAIL)
        set_tests_properties("${test_name}" PROPERTIES WILL_FAIL TRUE)
    endif()
endfunction()

# Helper macro to register a single testsuite test
# Used by add_testsuite_tests to avoid code duplication
macro(_register_testsuite_test test_name test_dir test_id skip_list xfail_list extra_env)
    if(EXISTS "${test_dir}/===")
        # Read description from === file
        file(READ "${test_dir}/===" _test_desc)
        string(STRIP "${_test_desc}" _test_desc)

        # Build test name with description if available
        if(_test_desc)
            set(_test_name_full "${test_name}/${test_id} - ${_test_desc}")
        else()
            set(_test_name_full "${test_name}/${test_id}")
        endif()

        # Set basic properties
        set(_test_labels "${test_name}")
        set(_test_disabled FALSE)

        # Check if test should be skipped
        if(test_id IN_LIST skip_list)
            set(_test_disabled TRUE)
            set(_test_labels "${test_name};skipped")
        endif()

        # Check if test is expected to fail (xfail)
        if(test_id IN_LIST xfail_list)
            set(_test_labels "${test_name};xfail")
        endif()

        if(_test_disabled)
            add_tap_test("${_test_name_full}" "${test_name}" "${test_id}"
                LABELS "${_test_labels}"
                EXTRA_ENV "${extra_env}"
                DISABLED
            )
        else()
            add_tap_test("${_test_name_full}" "${test_name}" "${test_id}"
                LABELS "${_test_labels}"
                EXTRA_ENV "${extra_env}"
            )
        endif()
    endif()
endmacro()

# Function to run a single test from libfyaml-test
function(add_libfyaml_tests)
    file(GLOB C_FILES "${CMAKE_CURRENT_SOURCE_DIR}/test/libfyaml-test-*.c")

    # Check for fy_check_testcase_add_test(tc, test_name)
    set(CREATE_PATTERN "fy_check_suite_add_test_case[ \t]*[(][^,]*,[ \t]*\"([^\"]+)\"")
    set(ADD_PATTERN "fy_check_testcase_add_test[ \t]*[(][^,]*,[ \t]*([^)]+)")
    foreach(FILE_PATH IN LISTS C_FILES)
        # Read the file line by line
        file(STRINGS "${FILE_PATH}" LINES)
        set(SUITE "")
        foreach(LINE IN LISTS LINES)
            if(LINE MATCHES "${CREATE_PATTERN}")
                string(REGEX REPLACE ".*${CREATE_PATTERN}.*" "\\1" SUITE "${LINE}")
            endif()

            if(LINE MATCHES "${ADD_PATTERN}")
                string(REGEX REPLACE ".*${ADD_PATTERN}.*" "\\1" TEST_NAME "${LINE}")
                add_tap_test("libfyaml/${SUITE}/${TEST_NAME}" libfyaml "${TEST_NAME}"
                    LABELS "libfyaml"
                )
            endif()
        endforeach()
    endforeach()

endfunction()


# Function to run a single test from testerrors.test
function(add_testerrors_tests)
    # Use wildcard glob and filter with regex (Windows-compatible)
    file(GLOB all_dirs "${CMAKE_CURRENT_SOURCE_DIR}/test/test-errors/*")

    foreach(dir ${all_dirs})
        if(NOT IS_DIRECTORY "${dir}")
            continue()
        endif()
        get_filename_component(test_id "${dir}" NAME)
        # Filter: must be 4 digits
        if(NOT test_id MATCHES "^[0-9][0-9][0-9][0-9]$")
            continue()
        endif()

        # Read description from === file
        set(desc_file "${dir}/===")
        if(EXISTS "${desc_file}")
            file(READ "${desc_file}" test_desc)
            string(STRIP "${test_desc}" test_desc)
        else()
            set(test_desc "")
        endif()

        # Build test name with description if available
        if(test_desc)
            set(test_name_full "testerrors/${test_id} - ${test_desc}")
        else()
            set(test_name_full "testerrors/${test_id}")
        endif()

        add_tap_test("${test_name_full}" testerrors "${test_id}"
            LABELS "testerrors"
        )
    endforeach()
endfunction()

# Function to add testemitter tests
function(add_testemitter_tests test_name extra_args)
    file(GLOB yaml_files "${CMAKE_CURRENT_SOURCE_DIR}/test/emitter-examples/*.yaml")

    foreach(yaml_file ${yaml_files})
        get_filename_component(test_id "${yaml_file}" NAME)

        add_tap_test("${test_name}/${test_id}" "${test_name}" "${test_id}"
            LABELS "${test_name}"
            EXTRA_ENV "EXTRA_DUMP_ARGS=${extra_args}"
        )
    endforeach()
endfunction()

# Function to add testsuite tests (yaml test suite)
# Scans test-suite-data directory (populated by FetchContent at configure time)
function(add_testsuite_tests test_name test_script)
    set(test_dir "${yaml_test_suite_SOURCE_DIR}")

    # Define skip/xfail lists based on test suite variant
    # These match the lists in test/*.test scripts
    if(test_name STREQUAL "testsuite-json")
        set(skip_list "UGM3")
        set(xfail_list "C4HZ")
    elseif(test_name STREQUAL "testsuite-resolution")
        set(skip_list "2JQS" "X38W")
        set(xfail_list "")
    elseif(test_name STREQUAL "testsuite-evstream")
        set(skip_list "2JQS")
        set(xfail_list "")
    else()
        set(skip_list "")
        set(xfail_list "")
    endif()

    set(extra_env "JQ=${JQ_EXECUTABLE}")

    message(STATUS "Registering individual ${test_name} subtests")

    # Use wildcard glob and filter with regex (Windows-compatible)
    file(GLOB all_base_dirs "${test_dir}/*")

    foreach(base_test ${all_base_dirs})
        if(NOT IS_DIRECTORY "${base_test}")
            continue()
        endif()
        get_filename_component(base_id "${base_test}" NAME)
        # Filter: must be 4 alphanumeric chars (YAML test suite IDs like "229Q", "2JQS")
        if(NOT base_id MATCHES "^[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]$")
            continue()
        endif()

        # Register base test
        _register_testsuite_test("${test_name}" "${base_test}" "${base_id}"
            "${skip_list}" "${xfail_list}" "${extra_env}")

        # Check for subtests (2-digit or 3-digit subdirectories)
        file(GLOB all_subtests "${base_test}/*")
        foreach(subtest ${all_subtests})
            if(NOT IS_DIRECTORY "${subtest}")
                continue()
            endif()
            get_filename_component(subtest_id "${subtest}" NAME)
            # Filter: must be 2 or 3 digits
            if(NOT subtest_id MATCHES "^[0-9][0-9][0-9]?$")
                continue()
            endif()

            set(full_test_id "${base_id}/${subtest_id}")
            _register_testsuite_test("${test_name}" "${subtest}" "${full_test_id}"
                "${skip_list}" "${xfail_list}" "${extra_env}")
        endforeach()
    endforeach()
endfunction()

# Function to add jsontestsuite tests
# Scans json-test-suite-data directory (populated by FetchContent at configure time)
function(add_jsontestsuite_tests)
    set(test_dir "${json_test_suite_SOURCE_DIR}/test_parsing")

    message(STATUS "Registering individual jsontestsuite subtests")

    # Scan for all .json files and categorize by prefix
    file(GLOB all_tests "${test_dir}/*.json")
    foreach(test_file ${all_tests})
        get_filename_component(test_id "${test_file}" NAME)

        # Categorize by first character: y=pass, n=fail, i=impl-defined
        string(SUBSTRING "${test_id}" 0 1 prefix)
        if(prefix STREQUAL "y")
            set(label "jsontestsuite;jsontestsuite-pass")
        elseif(prefix STREQUAL "n")
            set(label "jsontestsuite;jsontestsuite-fail")
        elseif(prefix STREQUAL "i")
            set(label "jsontestsuite;jsontestsuite-impl")
        else()
            continue()
        endif()

        add_tap_test("jsontestsuite/${test_id}" jsontestsuite "${test_id}"
            LABELS "${label}"
            EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
        )
    endforeach()
endfunction()

# Helper macro for checking if a reflection test should be skipped based on env file
macro(_should_skip_reflection_test test_path result_var)
    set(${result_var} FALSE)
    set(_env_file "${test_path}/env")
    if(EXISTS "${_env_file}")
        file(READ "${_env_file}" _env_contents)
        string(STRIP "${_env_contents}" _env_contents)

        # Parse env file for requirements
        if(_env_contents MATCHES "intbits=([0-9]+)")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_intbits}")
                set(${result_var} TRUE)
            endif()
        endif()
        if(_env_contents MATCHES "shortbits=([0-9]+)")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_shortbits}")
                set(${result_var} TRUE)
            endif()
        endif()
        if(_env_contents MATCHES "longbits=([0-9]+)")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_longbits}")
                set(${result_var} TRUE)
            endif()
        endif()
        if(_env_contents MATCHES "charbits=([0-9]+)")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_charbits}")
                set(${result_var} TRUE)
            endif()
        endif()
        if(_env_contents MATCHES "longlongbits=([0-9]+)")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_longlongbits}")
                set(${result_var} TRUE)
            endif()
        endif()
        if(_env_contents MATCHES "charsigned=([yn])")
            if(NOT "${CMAKE_MATCH_1}" STREQUAL "${_refl_charsigned}")
                set(${result_var} TRUE)
            endif()
        endif()
    endif()
endmacro()

# Helper macro to register a single reflection test
macro(_register_reflection_test test_dir test_id xfail_list)
    if(EXISTS "${test_dir}/===")
        # Read description from === file
        file(READ "${test_dir}/===" _test_desc)
        string(STRIP "${_test_desc}" _test_desc)

        # Build test name with description if available
        if(_test_desc)
            set(_test_name_full "testreflection/${test_id} - ${_test_desc}")
        else()
            set(_test_name_full "testreflection/${test_id}")
        endif()

        # Check if test should be skipped based on environment
        _should_skip_reflection_test("${test_dir}" _test_should_skip)

        # Set basic properties
        set(_test_labels "testreflection")
        set(_test_disabled FALSE)
        set(_test_will_fail FALSE)

        if(_test_should_skip)
            set(_test_disabled TRUE)
            set(_test_labels "testreflection;skipped")
        endif()

        if(test_id IN_LIST xfail_list)
            set(_test_labels "testreflection;xfail")
            set(_test_will_fail TRUE)
        endif()

        # Register the test with appropriate flags
        if(_test_disabled AND _test_will_fail)
            add_tap_test("${_test_name_full}" testreflection "${test_id}"
                LABELS "${_test_labels}" DISABLED WILL_FAIL)
        elseif(_test_disabled)
            add_tap_test("${_test_name_full}" testreflection "${test_id}"
                LABELS "${_test_labels}" DISABLED)
        elseif(_test_will_fail)
            add_tap_test("${_test_name_full}" testreflection "${test_id}"
                LABELS "${_test_labels}" WILL_FAIL)
        else()
            add_tap_test("${_test_name_full}" testreflection "${test_id}"
                LABELS "${_test_labels}")
        endif()
    endif()
endmacro()

# Function to add reflection tests
function(add_testreflection_tests)
    set(test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test/reflection-data")

    # Skip/xfail lists from testreflection.test
    set(xfail_list "025PB08/00" "025UXE6/00" "025VL2J/00")

    # Probe system characteristics (matching testreflection.test logic)
    include(CheckTypeSize)
    check_type_size("unsigned int" SIZEOF_UINT)
    check_type_size("unsigned short" SIZEOF_USHORT)
    check_type_size("unsigned long" SIZEOF_ULONG)
    check_type_size("unsigned char" SIZEOF_UCHAR)
    check_type_size("long long" SIZEOF_LONGLONG)

    # Calculate bit sizes (store in _refl_ prefixed vars for macro access)
    math(EXPR _refl_intbits "${SIZEOF_UINT} * 8")
    math(EXPR _refl_shortbits "${SIZEOF_USHORT} * 8")
    math(EXPR _refl_longbits "${SIZEOF_ULONG} * 8")
    math(EXPR _refl_charbits "${SIZEOF_UCHAR} * 8")
    set(_refl_longlongbits 64)

    # Check if char is signed
    include(CheckCSourceRuns)
    check_c_source_runs("
        #include <limits.h>
        int main() { return (CHAR_MIN < 0) ? 0 : 1; }
    " CHAR_IS_SIGNED)
    if(CHAR_IS_SIGNED)
        set(_refl_charsigned "y")
    else()
        set(_refl_charsigned "n")
    endif()

    message(STATUS "Reflection test system characteristics:")
    message(STATUS "  intbits=${_refl_intbits} shortbits=${_refl_shortbits} longbits=${_refl_longbits}")
    message(STATUS "  charbits=${_refl_charbits} longlongbits=${_refl_longlongbits} charsigned=${_refl_charsigned}")

    # Use wildcard glob and filter with regex (Windows-compatible)
    file(GLOB all_base_dirs "${test_dir}/*")

    foreach(base_test ${all_base_dirs})
        if(NOT IS_DIRECTORY "${base_test}")
            continue()
        endif()
        get_filename_component(base_id "${base_test}" NAME)
        # Filter: 3 digits followed by 4 alphanumeric chars (starting with letter)
        if(NOT base_id MATCHES "^[0-9][0-9][0-9][A-Z][A-Z0-9][A-Z0-9][A-Z0-9]$")
            continue()
        endif()

        # Register base test
        _register_reflection_test("${base_test}" "${base_id}" "${xfail_list}")

        # Check for subtests (2-digit or 3-digit subdirectories)
        file(GLOB all_subtests "${base_test}/*")
        foreach(subtest ${all_subtests})
            if(NOT IS_DIRECTORY "${subtest}")
                continue()
            endif()
            get_filename_component(subtest_id "${subtest}" NAME)
            # Filter: must be 2 or 3 digits
            if(NOT subtest_id MATCHES "^[0-9][0-9][0-9]?$")
                continue()
            endif()

            set(full_test_id "${base_id}/${subtest_id}")
            _register_reflection_test("${subtest}" "${full_test_id}" "${xfail_list}")
        endforeach()
    endforeach()
endfunction()

# Function to add Python pytest tests, one CTest entry per test function/method.
# Uses Python's ast module at CMake configure time to enumerate test IDs — no
# import of libfyaml needed, immune to semicolons/special chars in source.
#
# CTest name:  python/<suite>/<file_stem>::[ClassName::]test_name
# test_id:     <rel_file>::[ClassName::]test_name  (pytest node-ID format)
# Suite name:  "python"  (handled by run-single-tap-test.sh)
# Working dir: <srcdir>/python-libfyaml
#
# Required env injected via EXTRA_ENV:
#   PYTHON3_EXECUTABLE, PYTHONPATH, LD_LIBRARY_PATH
# Optional (when HAVE_ASAN):
#   LD_PRELOAD, ASAN_OPTIONS
function(add_python_tests)
    set(_py_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/python-libfyaml")

    # Discover test files
    file(GLOB _py_test_files
        "${_py_src_dir}/tests/core/test_*.py"
        "${_py_src_dir}/tests/pyyaml_compat/test_*.py")

    # Base environment
    set(_py_env
        "PYTHON3_EXECUTABLE=${Python3_EXECUTABLE}"
        "PYTHONPATH=${CMAKE_CURRENT_BINARY_DIR}/python-libfyaml"
        "LD_LIBRARY_PATH=${CMAKE_CURRENT_BINARY_DIR}"
    )

    # ASan: find libasan and extend environment
    if(HAVE_ASAN)
        execute_process(
            COMMAND ${CMAKE_C_COMPILER} -print-file-name=libasan.so
            OUTPUT_VARIABLE _libasan_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_libasan_path AND EXISTS "${_libasan_path}")
            list(APPEND _py_env
                "LD_PRELOAD=${_libasan_path}"
                "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:detect_stack_use_after_return=1"
            )
        endif()
    endif()

    # AST script: walks the parse tree to find test functions and methods.
    # Outputs one pytest node-ID per line; never imports the test module itself.
    set(_ast_script [=[
import ast, sys
tree = ast.parse(open(sys.argv[1]).read())
for node in ast.iter_child_nodes(tree):
    if isinstance(node, ast.FunctionDef) and node.name.startswith('test_'):
        print(node.name)
    elif isinstance(node, ast.ClassDef) and node.name.startswith('Test'):
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.FunctionDef) and child.name.startswith('test_'):
                print(node.name + '::' + child.name)
]=])

    foreach(_py_file ${_py_test_files})
        # Relative path from python-libfyaml/ (e.g. tests/core/test_basic.py)
        file(RELATIVE_PATH _rel_path "${_py_src_dir}" "${_py_file}")

        # Suite label: "core" or "pyyaml_compat"
        if(_rel_path MATCHES "tests/core/")
            set(_suite "core")
        else()
            set(_suite "pyyaml_compat")
        endif()

        get_filename_component(_file_stem "${_py_file}" NAME_WE)

        # Enumerate test IDs via AST at configure time (no libfyaml build needed)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -c "${_ast_script}" "${_py_file}"
            OUTPUT_VARIABLE _raw_ids
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _ast_result
        )
        if(NOT _ast_result EQUAL 0)
            message(WARNING "add_python_tests: AST scan failed for ${_rel_path}")
            continue()
        endif()

        # Split newline-separated IDs into a CMake list (safe: node IDs have no semicolons)
        string(REPLACE "\n" ";" _id_list "${_raw_ids}")

        foreach(_node_id IN LISTS _id_list)
            if(_node_id)
                add_tap_test(
                    "python/${_suite}/${_file_stem}::${_node_id}"
                    "python"
                    "${_rel_path}::${_node_id}"
                    LABELS "python"
                    EXTRA_ENV "${_py_env}"
                    WORKING_DIR "${_py_src_dir}"
                )
            endif()
        endforeach()
    endforeach()
endfunction()
