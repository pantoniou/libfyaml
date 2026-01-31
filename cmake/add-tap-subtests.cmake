# Helper function to add individual TAP subtests from test scripts
# This mimics the behavior of autotools tap-driver.sh by registering each subtest individually

# Helper function to add a TAP test with fy-tool path via generator expression
# This enables proper support for multi-config generators (Visual Studio, Xcode)
# by passing the actual tool path at runtime rather than relying on hardcoded paths.
function(add_tap_test test_name suite_name test_id)
    cmake_parse_arguments(TAP "DISABLED;WILL_FAIL" "" "LABELS;EXTRA_ENV" ${ARGN})

    add_test(
        NAME "${test_name}"
	COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh"
            "$<TARGET_FILE:fy-tool>"
            "${suite_name}" "${test_id}"
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
    )

    # Build environment
    set(base_env
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

# Function to run a single test from testerrors.test
function(add_testerrors_tests)
    file(GLOB error_dirs "${CMAKE_CURRENT_SOURCE_DIR}/test/test-errors/[0-9][0-9][0-9][0-9]")

    foreach(dir ${error_dirs})
        get_filename_component(test_id "${dir}" NAME)

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

    message(STATUS "Registering individual ${test_name} subtests")

    # Scan the filesystem
    file(GLOB base_tests "${test_dir}/[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]")

        foreach(base_test ${base_tests})
            get_filename_component(test_id "${base_test}" NAME)

            # Check for main test
            if(EXISTS "${base_test}/===")
                # Read description from === file
                file(READ "${base_test}/===" test_desc)
                string(STRIP "${test_desc}" test_desc)

                # Build test name with description if available
                if(test_desc)
                    set(test_name_full "${test_name}/${test_id} - ${test_desc}")
                else()
                    set(test_name_full "${test_name}/${test_id}")
                endif()

                # Set basic properties
                set(test_labels "${test_name}")
                set(test_disabled FALSE)

                # Check if test should be skipped
                if(test_id IN_LIST skip_list)
                    set(test_disabled TRUE)
                    set(test_labels "${test_name};skipped")
                endif()

                # Check if test is expected to fail (xfail)
                # Note: TAP xfail (TODO) tests still pass (exit 0), they just mark the result as TODO
                # So we don't use WILL_FAIL here, just add a label for filtering
                if(test_id IN_LIST xfail_list)
                    set(test_labels "${test_name};xfail")
                endif()

                if(test_disabled)
                    add_tap_test("${test_name_full}" "${test_name}" "${test_id}"
                        LABELS "${test_labels}"
                        EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                        DISABLED
                    )
                else()
                    add_tap_test("${test_name_full}" "${test_name}" "${test_id}"
                        LABELS "${test_labels}"
                        EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                    )
                endif()
            endif()

            # Check for 2-digit subtests
            file(GLOB subtests_2d "${base_test}/[0-9][0-9]")
            foreach(subtest ${subtests_2d})
                if(EXISTS "${subtest}/===")
                    get_filename_component(subtest_id "${subtest}" NAME)
                    set(full_test_id "${test_id}/${subtest_id}")

                    # Read description from === file
                    file(READ "${subtest}/===" test_desc)
                    string(STRIP "${test_desc}" test_desc)

                    # Build test name with description if available
                    if(test_desc)
                        set(test_name_full "${test_name}/${full_test_id} - ${test_desc}")
                    else()
                        set(test_name_full "${test_name}/${full_test_id}")
                    endif()

                    # Set basic properties
                    set(test_labels "${test_name}")
                    set(test_disabled FALSE)

                    # Check if test should be skipped
                    if(full_test_id IN_LIST skip_list)
                        set(test_disabled TRUE)
                        set(test_labels "${test_name};skipped")
                    endif()

                    # Check if test is expected to fail (xfail)
                    # Note: TAP xfail (TODO) tests still pass (exit 0), they just mark the result as TODO
                    # So we don't use WILL_FAIL here, just add a label for filtering
                    if(full_test_id IN_LIST xfail_list)
                        set(test_labels "${test_name};xfail")
                    endif()

                    if(test_disabled)
                        add_tap_test("${test_name_full}" "${test_name}" "${full_test_id}"
                            LABELS "${test_labels}"
                            EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                            DISABLED
                        )
                    else()
                        add_tap_test("${test_name_full}" "${test_name}" "${full_test_id}"
                            LABELS "${test_labels}"
                            EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                        )
                    endif()
                endif()
            endforeach()

            # Check for 3-digit subtests
            file(GLOB subtests_3d "${base_test}/[0-9][0-9][0-9]")
            foreach(subtest ${subtests_3d})
                if(EXISTS "${subtest}/===")
                    get_filename_component(subtest_id "${subtest}" NAME)
                    set(full_test_id "${test_id}/${subtest_id}")

                    # Read description from === file
                    file(READ "${subtest}/===" test_desc)
                    string(STRIP "${test_desc}" test_desc)

                    # Build test name with description if available
                    if(test_desc)
                        set(test_name_full "${test_name}/${full_test_id} - ${test_desc}")
                    else()
                        set(test_name_full "${test_name}/${full_test_id}")
                    endif()

                    # Set basic properties
                    set(test_labels "${test_name}")
                    set(test_disabled FALSE)

                    # Check if test should be skipped
                    if(full_test_id IN_LIST skip_list)
                        set(test_disabled TRUE)
                        set(test_labels "${test_name};skipped")
                    endif()

                    # Check if test is expected to fail (xfail)
                    # Note: TAP xfail (TODO) tests still pass (exit 0), they just mark the result as TODO
                    # So we don't use WILL_FAIL here, just add a label for filtering
                    if(full_test_id IN_LIST xfail_list)
                        set(test_labels "${test_name};xfail")
                    endif()

                    if(test_disabled)
                        add_tap_test("${test_name_full}" "${test_name}" "${full_test_id}"
                            LABELS "${test_labels}"
                            EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                            DISABLED
                        )
                    else()
                        add_tap_test("${test_name_full}" "${test_name}" "${full_test_id}"
                            LABELS "${test_labels}"
                            EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
                        )
                    endif()
                endif()
            endforeach()
        endforeach()
endfunction()

# Function to add jsontestsuite tests
# Scans json-test-suite-data directory (populated by FetchContent at configure time)
function(add_jsontestsuite_tests)
    set(test_dir "${json_test_suite_SOURCE_DIR}/test_parsing")

    message(STATUS "Registering individual jsontestsuite subtests")

    # Scan the filesystem
    # Expected to pass (y_*.json)
    file(GLOB pass_tests "${test_dir}/y_*.json")
        foreach(test_file ${pass_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_tap_test("jsontestsuite/${test_id}" jsontestsuite "${test_id}"
                LABELS "jsontestsuite;jsontestsuite-pass"
                EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
            )
        endforeach()

        # Expected to fail (n_*.json)
        file(GLOB fail_tests "${test_dir}/n_*.json")
        foreach(test_file ${fail_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_tap_test("jsontestsuite/${test_id}" jsontestsuite "${test_id}"
                LABELS "jsontestsuite;jsontestsuite-fail"
                EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
            )
        endforeach()

        # Implementation defined (i_*.json)
        file(GLOB impl_tests "${test_dir}/i_*.json")
        foreach(test_file ${impl_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_tap_test("jsontestsuite/${test_id}" jsontestsuite "${test_id}"
                LABELS "jsontestsuite;jsontestsuite-impl"
                EXTRA_ENV "JQ=${JQ_EXECUTABLE}"
            )
        endforeach()
endfunction()

# Function to add reflection tests
function(add_testreflection_tests)
    set(test_dir "${CMAKE_CURRENT_SOURCE_DIR}/test/reflection-data")

    # Skip/xfail lists from testreflection.test
    # xfaillist="025PB08/00 025UXE6/00 025VL2J/00"
    set(xfail_list "025PB08/00" "025UXE6/00" "025VL2J/00")

    # Probe system characteristics (matching testreflection.test logic)
    include(CheckTypeSize)
    check_type_size("unsigned int" SIZEOF_UINT)
    check_type_size("unsigned short" SIZEOF_USHORT)
    check_type_size("unsigned long" SIZEOF_ULONG)
    check_type_size("unsigned char" SIZEOF_UCHAR)
    check_type_size("long long" SIZEOF_LONGLONG)

    # Calculate bit sizes
    math(EXPR intbits "${SIZEOF_UINT} * 8")
    math(EXPR shortbits "${SIZEOF_USHORT} * 8")
    math(EXPR longbits "${SIZEOF_ULONG} * 8")
    math(EXPR charbits "${SIZEOF_UCHAR} * 8")
    set(longlongbits 64)  # Always 64 bits as per testreflection.test

    # Check if char is signed
    include(CheckCSourceRuns)
    check_c_source_runs("
        #include <limits.h>
        int main() { return (CHAR_MIN < 0) ? 0 : 1; }
    " CHAR_IS_SIGNED)
    if(CHAR_IS_SIGNED)
        set(charsigned "y")
    else()
        set(charsigned "n")
    endif()

    message(STATUS "Reflection test system characteristics:")
    message(STATUS "  intbits=${intbits} shortbits=${shortbits} longbits=${longbits}")
    message(STATUS "  charbits=${charbits} longlongbits=${longlongbits} charsigned=${charsigned}")

    # Helper function to check if test should be skipped based on env file
    macro(should_skip_test test_path result_var)
        set(${result_var} FALSE)
        set(env_file "${test_path}/env")
        if(EXISTS "${env_file}")
            file(READ "${env_file}" env_contents)
            string(STRIP "${env_contents}" env_contents)

            # Parse env file for requirements
            if(env_contents MATCHES "intbits=([0-9]+)")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${intbits}")
                    set(${result_var} TRUE)
                endif()
            endif()
            if(env_contents MATCHES "shortbits=([0-9]+)")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${shortbits}")
                    set(${result_var} TRUE)
                endif()
            endif()
            if(env_contents MATCHES "longbits=([0-9]+)")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${longbits}")
                    set(${result_var} TRUE)
                endif()
            endif()
            if(env_contents MATCHES "charbits=([0-9]+)")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${charbits}")
                    set(${result_var} TRUE)
                endif()
            endif()
            if(env_contents MATCHES "longlongbits=([0-9]+)")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${longlongbits}")
                    set(${result_var} TRUE)
                endif()
            endif()
            if(env_contents MATCHES "charsigned=([yn])")
                if(NOT "${CMAKE_MATCH_1}" STREQUAL "${charsigned}")
                    set(${result_var} TRUE)
                endif()
            endif()
        endif()
    endmacro()

    # Scan reflection-data directory for tests
    # Note: CMake GLOB doesn't support [A-Z0-9] ranges like shell globs do
    # We need to use a wildcard and filter in the loop
    file(GLOB base_tests "${test_dir}/*")

    # Filter to only include directories matching the pattern [0-9][0-9][0-9][A-Z][A-Z0-9][A-Z0-9][A-Z0-9]
    set(filtered_base_tests)
    foreach(test_candidate ${base_tests})
        if(IS_DIRECTORY "${test_candidate}")
            get_filename_component(test_name "${test_candidate}" NAME)
            # Check if the name matches the pattern: 3 digits followed by 4 alphanumeric chars (starting with letter)
            if(test_name MATCHES "^[0-9][0-9][0-9][A-Z][A-Z0-9][A-Z0-9][A-Z0-9]$")
                list(APPEND filtered_base_tests "${test_candidate}")
            endif()
        endif()
    endforeach()
    set(base_tests ${filtered_base_tests})

    foreach(base_test ${base_tests})
        get_filename_component(test_id "${base_test}" NAME)

        # Check for main test (base directory with === file)
        if(EXISTS "${base_test}/===")
            # Read description from === file
            file(READ "${base_test}/===" test_desc)
            string(STRIP "${test_desc}" test_desc)

            # Build test name with description if available
            if(test_desc)
                set(test_name_full "testreflection/${test_id} - ${test_desc}")
            else()
                set(test_name_full "testreflection/${test_id}")
            endif()

            # Check if test should be skipped based on environment
            should_skip_test("${base_test}" test_should_skip)

            # Set basic properties
            set(test_labels "testreflection")
            set(test_disabled FALSE)
            set(test_will_fail FALSE)

            # Check if test should be skipped
            if(test_should_skip)
                set(test_disabled TRUE)
                set(test_labels "testreflection;skipped")
            endif()

            # Check if test is expected to fail (xfail)
            if(test_id IN_LIST xfail_list)
                set(test_labels "testreflection;xfail")
                set(test_will_fail TRUE)
            endif()

            if(test_disabled AND test_will_fail)
                add_tap_test("${test_name_full}" testreflection "${test_id}"
                    LABELS "${test_labels}"
                    DISABLED
                    WILL_FAIL
                )
            elseif(test_disabled)
                add_tap_test("${test_name_full}" testreflection "${test_id}"
                    LABELS "${test_labels}"
                    DISABLED
                )
            elseif(test_will_fail)
                add_tap_test("${test_name_full}" testreflection "${test_id}"
                    LABELS "${test_labels}"
                    WILL_FAIL
                )
            else()
                add_tap_test("${test_name_full}" testreflection "${test_id}"
                    LABELS "${test_labels}"
                )
            endif()
        endif()

        # Check for 2-digit subtests
        file(GLOB subtests_2d "${base_test}/[0-9][0-9]")
        foreach(subtest ${subtests_2d})
            if(EXISTS "${subtest}/===")
                get_filename_component(subtest_id "${subtest}" NAME)
                set(full_test_id "${test_id}/${subtest_id}")

                # Read description from === file
                file(READ "${subtest}/===" test_desc)
                string(STRIP "${test_desc}" test_desc)

                # Build test name with description if available
                if(test_desc)
                    set(test_name_full "testreflection/${full_test_id} - ${test_desc}")
                else()
                    set(test_name_full "testreflection/${full_test_id}")
                endif()

                # Check if test should be skipped based on environment
                should_skip_test("${subtest}" test_should_skip)

                # Set basic properties
                set(test_labels "testreflection")
                set(test_disabled FALSE)
                set(test_will_fail FALSE)

                # Check if test should be skipped
                if(test_should_skip)
                    set(test_disabled TRUE)
                    set(test_labels "testreflection;skipped")
                endif()

                # Check if test is expected to fail (xfail)
                if(full_test_id IN_LIST xfail_list)
                    set(test_labels "testreflection;xfail")
                    set(test_will_fail TRUE)
                endif()

                if(test_disabled AND test_will_fail)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        DISABLED
                        WILL_FAIL
                    )
                elseif(test_disabled)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        DISABLED
                    )
                elseif(test_will_fail)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        WILL_FAIL
                    )
                else()
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                    )
                endif()
            endif()
        endforeach()

        # Check for 3-digit subtests
        file(GLOB subtests_3d "${base_test}/[0-9][0-9][0-9]")
        foreach(subtest ${subtests_3d})
            if(EXISTS "${subtest}/===")
                get_filename_component(subtest_id "${subtest}" NAME)
                set(full_test_id "${test_id}/${subtest_id}")

                # Read description from === file
                file(READ "${subtest}/===" test_desc)
                string(STRIP "${test_desc}" test_desc)

                # Build test name with description if available
                if(test_desc)
                    set(test_name_full "testreflection/${full_test_id} - ${test_desc}")
                else()
                    set(test_name_full "testreflection/${full_test_id}")
                endif()

                # Check if test should be skipped based on environment
                should_skip_test("${subtest}" test_should_skip)

                # Set basic properties
                set(test_labels "testreflection")
                set(test_disabled FALSE)
                set(test_will_fail FALSE)

                # Check if test should be skipped
                if(test_should_skip)
                    set(test_disabled TRUE)
                    set(test_labels "testreflection;skipped")
                endif()

                # Check if test is expected to fail (xfail)
                if(full_test_id IN_LIST xfail_list)
                    set(test_labels "testreflection;xfail")
                    set(test_will_fail TRUE)
                endif()

                if(test_disabled AND test_will_fail)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        DISABLED
                        WILL_FAIL
                    )
                elseif(test_disabled)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        DISABLED
                    )
                elseif(test_will_fail)
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                        WILL_FAIL
                    )
                else()
                    add_tap_test("${test_name_full}" testreflection "${full_test_id}"
                        LABELS "${test_labels}"
                    )
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
