# Script to set up test suite data in an idempotent way

if(NOT DEFINED REPO_NAME)
    set(REPO_NAME "test-suite-data")
endif()

set(REPO_DIR "${TEST_DIR}/${REPO_NAME}")

# Check if the repository already exists
if(EXISTS "${REPO_DIR}/.git")
    message(STATUS "Repository ${REPO_NAME} already exists, updating...")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} fetch -q origin
        WORKING_DIRECTORY ${REPO_DIR}
        RESULT_VARIABLE GIT_FETCH_RESULT
    )
    if(NOT GIT_FETCH_RESULT EQUAL 0)
        message(WARNING "Failed to fetch updates for ${REPO_NAME}")
    endif()
else()
    message(STATUS "Cloning ${REPO_NAME}...")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} clone -q "${TESTSUITEURL}" ${REPO_NAME}
        WORKING_DIRECTORY ${TEST_DIR}
        RESULT_VARIABLE GIT_CLONE_RESULT
    )
    if(NOT GIT_CLONE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to clone ${REPO_NAME}")
    endif()
endif()

# Checkout the specific commit
execute_process(
    COMMAND ${GIT_EXECUTABLE} checkout -q --detach ${TESTSUITECHECKOUT}
    WORKING_DIRECTORY ${REPO_DIR}
    RESULT_VARIABLE GIT_CHECKOUT_RESULT
)
if(NOT GIT_CHECKOUT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to checkout ${TESTSUITECHECKOUT} in ${REPO_NAME}")
endif()

message(STATUS "Test suite ${REPO_NAME} set up successfully")
