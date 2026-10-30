#!/bin/sh
# SPDX-License-Identifier: MIT
#
# run-wasm-tests.sh BUILD_DIR
#
# Run the cparse and reflection tests with the library built for WebAssembly
# (Emscripten, run by node). BUILD_DIR is a CMake build with the static library:
#
#   emcmake cmake -G Ninja -S . -B build-wasm -DBUILD_SHARED_LIBS=OFF \
#       -DENABLE_LIBCLANG=OFF
#   cmake --build build-wasm
#
# The tools are made from the compile options of the library. The tests are the
# cparse cases (against the expected files), the C round trip, and the macro file
# tests. They run the tools directly, so ctest does not run them for Emscripten;
# the other tests of ctest (the unit tests and the YAML test suites) run under node.
#
# Environment: EMCC, the compiler (default: emcc), and NODE (default: node).
set -u

build=${1:?usage: run-wasm-tests.sh BUILD_DIR}
here=$(cd "$(dirname "$0")" && pwd)
test_dir=$(dirname "$here")
root=$(dirname "$test_dir")
emcc=${EMCC:-emcc}
node=${NODE:-node}

build=$(cd "$build" && pwd)
lib=$build/libfyaml_static.a
[ -f "$lib" ] || lib=$build/libfyaml.a
[ -f "$lib" ] || { echo "no static library in $build" >&2; exit 2; }

# the options the library was built with (the private headers depend on them)
flags=$(ninja -C "$build" -t commands fyaml_static | grep -m 1 'cparse-parse\.c' |
	tr ' ' '\n' | grep -E '^-(D|I)' | grep -v '^-DVERSION' | tr '\n' ' ')

tools=$build/wasm-tools
mkdir -p "$tools"

make_tool() {
	src=$1
	name=$2

	# the options of the Emscripten programs of CMakeLists.txt: the library is
	# made with threads, so the tools are too
	# shellcheck disable=SC2086
	$emcc -O1 -pthread $flags -I"$build" -I"$root/src/cparse" "$src" "$lib" \
		-sPTHREAD_POOL_SIZE=2 -sPROXY_TO_PTHREAD -sINITIAL_MEMORY=134217728 \
		-sSTACK_SIZE=8388608 -Wno-pthreads-mem-growth \
		-sENVIRONMENT=node -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 \
		--pre-js "$root/cmake/emscripten-node.js" -o "$tools/$name.js" || return 1
	printf '#!/bin/sh\nexec %s "%s/%s.js" "$@"\n' "$node" "$tools" "$name" > "$tools/$name"
	chmod +x "$tools/$name"
}

make_tool "$test_dir/cparse/cparse-tool.c" cparse-tool || exit 1
make_tool "$test_dir/reflection-backend-dump.c" reflection-backend-dump || exit 1

pass=0
fail=0
check() {
	label=$1
	shift
	if "$@" > /dev/null 2>&1; then
		pass=$((pass + 1))
	else
		fail=$((fail + 1))
		echo "FAIL $label"
	fi
}

for name in $(grep -v '^#' "$test_dir/cparse/cases.txt" | cut -d' ' -f1); do
	CPARSE_TOOL=$tools/cparse-tool check "cparse/$name" \
		"$test_dir/cparse/run-cparse-test.sh" expected "$name"
done

for header in "$test_dir"/reflection-roundtrip/*.h; do
	BACKEND_DUMP=$tools/reflection-backend-dump check "reflection-roundtrip/$(basename "$header")" \
		"$test_dir/run-reflection-roundtrip.sh" roundtrip "$header"
done

for header in "$test_dir"/reflection-macros/*.h; do
	BACKEND_DUMP=$tools/reflection-backend-dump check "reflection-macros/$(basename "$header")" \
		"$test_dir/run-reflection-roundtrip.sh" macros "$header"
done

echo "wasm tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
