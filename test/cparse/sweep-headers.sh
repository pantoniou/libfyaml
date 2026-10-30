#!/bin/bash
# SPDX-License-Identifier: MIT
# sweep-headers.sh [HEADER_DIR]
#
# Manual check. For each top level header in HEADER_DIR (default /usr/include)
# that libclang compiles alone, compare `cparse-tool decls` with the
# declarations libclang gives. CPARSE_ARGS must hold the predefined macros and
# include paths that libclang uses.
#
# Environment: CPARSE_TOOL, CPARSE_LIBCLANG_DUMP, CPARSE_ARGS (optional) and
# WORK, a directory for the results (default: a new temporary directory).
set -u
dir=${1:-/usr/include}
tool=${CPARSE_TOOL:?CPARSE_TOOL is not set}
dump=${CPARSE_LIBCLANG_DUMP:?CPARSE_LIBCLANG_DUMP is not set}
v=$("$dump" --major)
work=${WORK:-$(mktemp -d)}
mkdir -p "$work"
same=0; diffc=0; err=0; timeouts=0; clang_err=0

for h in "$dir"/*.h; do
	n=$(basename "$h")
	f=$work/t_$n.c
	echo "#include <$n>" > "$f"
	if ! timeout 20 "$dump" -x c "$f" > "$f.ref" 2> "$f.ref.err" || grep -q '^clang:' "$f.ref.err"; then
		clang_err=$((clang_err + 1))
		continue
	fi
	# shellcheck disable=SC2086
	timeout 20 "$tool" -V "$v" ${CPARSE_ARGS:-} decls "$f" > "$f.mine" 2> "$f.mine.err"
	rc=$?
	if [ $rc -eq 124 ]; then
		timeouts=$((timeouts + 1)); echo "TIMEOUT $n"; continue
	fi
	if [ $rc -ne 0 ]; then
		err=$((err + 1)); echo "ERR $n: $(head -1 "$f.mine.err" | cut -c1-150)"
	fi
	if diff "$f.ref" "$f.mine" > "$f.diff"; then
		same=$((same + 1))
	else
		diffc=$((diffc + 1)); echo "DIFF $n: $(grep -c '^[<>]' "$f.diff") lines"
	fi
done
echo "same=$same diff=$diffc err=$err timeout=$timeouts not-compilable-by-clang=$clang_err"
[ $diffc -eq 0 ] && [ $err -eq 0 ] && [ $timeouts -eq 0 ]
