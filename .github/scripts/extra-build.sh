#!/bin/sh
# Configure, build and test libfyaml on the extra platforms workflow.
# POSIX sh only - this runs on Linux containers, the BSDs and under QEMU.
#
# Environment:
#   BUILD_TYPE        CMake build type (default Release)
#   TESTING           ON to build and run the tests, OFF to only build (default ON)
#   JOBS              parallel build/test jobs (default 2)
#   CMAKE_EXTRA_ARGS  extra arguments for the configure step
#   CTEST_ARGS        extra arguments for ctest
set -eu

BUILD_TYPE=${BUILD_TYPE:-Release}
TESTING=${TESTING:-ON}
JOBS=${JOBS:-2}

uname -a
cmake --version | head -n 1

# libclang and the python bindings are optional and not available everywhere
cmake -S . -B build \
	-DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
	-DBUILD_TESTING="$TESTING" \
	-DENABLE_LIBCLANG=OFF \
	-DENABLE_PYTHON_BINDINGS=OFF \
	${CMAKE_EXTRA_ARGS:-}

cmake --build build --parallel "$JOBS"

if [ "$TESTING" = "ON" ]; then
	cd build
	ctest --output-on-failure --parallel "$JOBS" ${CTEST_ARGS:-}
fi
