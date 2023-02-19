# Helper function to add individual TAP subtests from test scripts
# This mimics the behavior of autotools tap-driver.sh by registering each subtest individually

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

        add_test(
            NAME "${test_name_full}"
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                testerrors "${test_id}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
        )

        # Set properties
        set_tests_properties("${test_name_full}" PROPERTIES
            ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test"
            LABELS "testerrors"
        )
    endforeach()
endfunction()

# Function to add testemitter tests
function(add_testemitter_tests test_name extra_args)
    file(GLOB yaml_files "${CMAKE_CURRENT_SOURCE_DIR}/test/emitter-examples/*.yaml")

    foreach(yaml_file ${yaml_files})
        get_filename_component(test_id "${yaml_file}" NAME)

        add_test(
            NAME "${test_name}/${test_id}"
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                "${test_name}" "${test_id}"
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
        )

        set_tests_properties("${test_name}/${test_id}" PROPERTIES
            ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;EXTRA_DUMP_ARGS=${extra_args}"
            LABELS "${test_name}"
        )
    endforeach()
endfunction()

# Function to add testsuite tests (yaml test suite)
# Can work either by scanning the test-suite-data directory (if it exists)
# or by using a pre-generated list of tests from testsuite-tests.cmake
function(add_testsuite_tests test_name test_script)
    set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/test/test-suite-data")
    set(use_pregenerated_list FALSE)

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

    # Check if we should use the pre-generated list
    if(NOT EXISTS "${test_dir}")
        # Try to use pre-generated list if available
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/testsuite-tests.cmake")
            message(STATUS "Using pre-generated test list from cmake/testsuite-tests.cmake for ${test_name}")
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/testsuite-tests.cmake")
            set(use_pregenerated_list TRUE)
        else()
            # Neither data nor pre-generated list exists, register monolithic fallback
            message(STATUS "Test suite data not found - registering monolithic ${test_name} test")
            message(STATUS "Run 'ctest -R yaml-test-suite-setup' then re-run cmake to register individual subtests")

            add_test(NAME ${test_name}
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/${test_script}
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )
            set_tests_properties(${test_name} PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                FIXTURES_REQUIRED yaml-test-suite-data
            )
            return()
        endif()
    else()
        message(STATUS "Registering individual ${test_name} subtests from filesystem")
    endif()

    if(use_pregenerated_list)
        # Use pre-generated TESTSUITE_ALL_TESTS list
        foreach(test_id ${TESTSUITE_ALL_TESTS})
            # test_id is either "BASE" or "BASE/SUBTEST"
            string(REPLACE "/" ";" test_parts "${test_id}")
            list(LENGTH test_parts parts_count)

            add_test(
                NAME "${test_name}/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    "${test_name}" "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("${test_name}/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "${test_name}"
                FIXTURES_REQUIRED "yaml-test-suite-data"
            )
        endforeach()
    else()
        # Scan the filesystem (original behavior)
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

                add_test(
                    NAME "${test_name_full}"
                    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                        "${test_name}" "${test_id}"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                )

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

                set_tests_properties("${test_name_full}" PROPERTIES
                    ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                    LABELS "${test_labels}"
                    FIXTURES_REQUIRED "yaml-test-suite-data"
                    DISABLED ${test_disabled}
                )
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

                    add_test(
                        NAME "${test_name_full}"
                        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                            "${test_name}" "${full_test_id}"
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                    )

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

                    set_tests_properties("${test_name_full}" PROPERTIES
                        ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                        LABELS "${test_labels}"
                        FIXTURES_REQUIRED "yaml-test-suite-data"
                        DISABLED ${test_disabled}
                    )
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

                    add_test(
                        NAME "${test_name_full}"
                        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                            "${test_name}" "${full_test_id}"
                        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                    )

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

                    set_tests_properties("${test_name_full}" PROPERTIES
                        ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                        LABELS "${test_labels}"
                        FIXTURES_REQUIRED "yaml-test-suite-data"
                        DISABLED ${test_disabled}
                    )
                endif()
            endforeach()
        endforeach()
    endif()
endfunction()

# Function to add jsontestsuite tests
# Can work either by scanning the json-test-suite-data directory (if it exists)
# or by using a pre-generated list of tests from jsontestsuite-tests.cmake
function(add_jsontestsuite_tests)
    set(test_dir "${CMAKE_CURRENT_BINARY_DIR}/test/json-test-suite-data/test_parsing")
    set(use_pregenerated_list FALSE)

    # Check if we should use the pre-generated list
    if(NOT EXISTS "${test_dir}")
        # Try to use pre-generated list if available
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jsontestsuite-tests.cmake")
            message(STATUS "Using pre-generated test list from cmake/jsontestsuite-tests.cmake")
            include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/jsontestsuite-tests.cmake")
            set(use_pregenerated_list TRUE)
        else()
            # Neither data nor pre-generated list exists, register monolithic fallback
            message(STATUS "JSON test suite data not found - registering monolithic jsontestsuite test")
            message(STATUS "Run 'ctest -R json-test-suite-setup' then re-run cmake to register individual subtests")

            add_test(NAME jsontestsuite
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test/jsontestsuite.test
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )
            set_tests_properties(jsontestsuite PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                FIXTURES_REQUIRED json-test-suite-data
            )
            return()
        endif()
    else()
        message(STATUS "Registering individual jsontestsuite subtests from filesystem")
    endif()

    if(use_pregenerated_list)
        # Use pre-generated lists
        # Expected to pass (y_*.json)
        foreach(test_id ${JSONTESTSUITE_PASS_TESTS})
            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-pass"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()

        # Expected to fail (n_*.json)
        foreach(test_id ${JSONTESTSUITE_FAIL_TESTS})
            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-fail"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()

        # Implementation defined (i_*.json)
        foreach(test_id ${JSONTESTSUITE_IMPL_TESTS})
            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-impl"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()
    else()
        # Scan the filesystem (original behavior)
        # Expected to pass (y_*.json)
        file(GLOB pass_tests "${test_dir}/y_*.json")
        foreach(test_file ${pass_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-pass"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()

        # Expected to fail (n_*.json)
        file(GLOB fail_tests "${test_dir}/n_*.json")
        foreach(test_file ${fail_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-fail"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()

        # Implementation defined (i_*.json)
        file(GLOB impl_tests "${test_dir}/i_*.json")
        foreach(test_file ${impl_tests})
            get_filename_component(test_id "${test_file}" NAME)

            add_test(
                NAME "jsontestsuite/${test_id}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    jsontestsuite "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

            set_tests_properties("jsontestsuite/${test_id}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test;JQ=${JQ_EXECUTABLE}"
                LABELS "jsontestsuite;jsontestsuite-impl"
                FIXTURES_REQUIRED "json-test-suite-data"
            )
        endforeach()
    endif()
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

            add_test(
                NAME "${test_name_full}"
                COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                    testreflection "${test_id}"
                WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
            )

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

            set_tests_properties("${test_name_full}" PROPERTIES
                ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test"
                LABELS "${test_labels}"
                DISABLED ${test_disabled}
                WILL_FAIL ${test_will_fail}
            )
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

                add_test(
                    NAME "${test_name_full}"
                    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                        testreflection "${full_test_id}"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                )

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

                set_tests_properties("${test_name_full}" PROPERTIES
                    ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test"
                    LABELS "${test_labels}"
                    DISABLED ${test_disabled}
                    WILL_FAIL ${test_will_fail}
                )
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

                add_test(
                    NAME "${test_name_full}"
                    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run-single-tap-test.sh
                        testreflection "${full_test_id}"
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/test
                )

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

                set_tests_properties("${test_name_full}" PROPERTIES
                    ENVIRONMENT "TOP_SRCDIR=${CMAKE_CURRENT_SOURCE_DIR};TOP_BUILDDIR=${CMAKE_CURRENT_BINARY_DIR};SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/test;BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/test"
                    LABELS "${test_labels}"
                    DISABLED ${test_disabled}
                    WILL_FAIL ${test_will_fail}
                )
            endif()
        endforeach()
    endforeach()
endfunction()
