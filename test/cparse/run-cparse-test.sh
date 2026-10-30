#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run-cparse-test.sh MODE NAME
#
# Run one case of cases.txt.
#
#   MODE expected   compare the cparse-tool output with expected/NAME.txt; a case
#                   of the kind error must fail, and the message is compared
#   MODE libclang   compare the declarations with the ones libclang gives
#   MODE clangpp    compare the preprocessed tokens with clang -E -P
#   MODE update     write expected/NAME.txt
#
# The exit status 77 is a skip: libclang does not know the target.
#
# Environment: CPARSE_TOOL, and for the other modes CPARSE_LIBCLANG_DUMP or
# CLANG. TEST_DIR is the directory of the tests (default: where this file is).
set -u

mode=${1:?usage: run-cparse-test.sh MODE NAME}
name=${2:?usage: run-cparse-test.sh MODE NAME}
here=${TEST_DIR:-$(cd "$(dirname "$0")" && pwd)}
tool=${CPARSE_TOOL:?CPARSE_TOOL is not set}
# the script changes directory, so the programs need absolute names
abspath() {
	echo "$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
}
tool=$(abspath "$tool")
dump=${CPARSE_LIBCLANG_DUMP:+$(abspath "$CPARSE_LIBCLANG_DUMP")}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

line=$(grep -v '^#' "$here/cases.txt" | awk -v n="$name" '$1 == n')
if [ -z "$line" ]; then
	echo "no case $name" >&2
	exit 2
fi
# shellcheck disable=SC2086
set -- $line
kind=$2
fixture=$3
shift 3
opts=("$@")

cd "$here" || exit 2

# the version libclang names anonymous tags with (16 and later use the typedef)
cparse_run() {
	local ver=$1 cmd=$2

	if [ "$kind" = comments ] || [ "$kind" = comments-golden ]; then
		LIBCLANG_DUMP_COMMENTS=1 "$tool" -V "$ver" ${opts[@]+"${opts[@]}"} "$cmd" "$fixture"
	else
		"$tool" -V "$ver" ${opts[@]+"${opts[@]}"} "$cmd" "$fixture"
	fi
}

clang_opts() {
	local o=()
	local i=0

	while [ $i -lt ${#opts[@]} ]; do
		case ${opts[$i]} in
		-T)
			i=$((i + 1))
			o+=(-target "${opts[$i]}")
			;;
		*)
			o+=("${opts[$i]}")
			;;
		esac
		i=$((i + 1))
	done
	echo ${o[@]+"${o[@]}"}
}

case $mode in
expected|update)
	cmd=decls
	[ "$kind" = pp ] && cmd=pp
	[ "$kind" = predefs ] && cmd=check-predefs
	if [ "$kind" = error ]; then
		# the tool must fail; its messages are the output
		if cparse_run 18 $cmd > "$tmp/out" 2>&1; then
			echo "cparse-tool did not fail" >&2
			exit 1
		fi
	elif ! cparse_run 18 $cmd > "$tmp/out" 2> "$tmp/err"; then
		echo "cparse-tool failed" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	if [ "$mode" = update ]; then
		mkdir -p expected
		cp "$tmp/out" "expected/$name.txt"
		exit 0
	fi
	if ! diff -u "expected/$name.txt" "$tmp/out"; then
		echo "output differs from expected/$name.txt" >&2
		exit 1
	fi
	;;
libclang)
	dump=${dump:?CPARSE_LIBCLANG_DUMP is not set}
	ver=$("$dump" --major)
	if ! cparse_run "$ver" decls > "$tmp/mine" 2> "$tmp/err"; then
		echo "cparse-tool failed" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	args=()
	if [ "$kind" = comments ]; then
		args+=(-fparse-all-comments)
		export LIBCLANG_DUMP_COMMENTS=1
	fi
	# shellcheck disable=SC2046
	if ! "$dump" ${args[@]+"${args[@]}"} $(clang_opts) -x c "$fixture" > "$tmp/ref" 2> "$tmp/err"; then
		# an older libclang may not know a newer target
		if grep -q "unknown target triple" "$tmp/err"; then
			echo "libclang $ver does not know the target" >&2
			exit 77
		fi
		echo "libclang dump failed" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	if ! diff -u "$tmp/ref" "$tmp/mine"; then
		echo "declarations differ from libclang $ver" >&2
		exit 1
	fi
	;;
clangpp)
	clang=${CLANG:?CLANG is not set}
	toks() {
		sed -E 's/^[0-9]+:[0-9]+ [a-z]+ (bol )?(sp )?//'
	}
	# cparse consumes `#pragma pack(...)`, clang leaves it in its -E output: remove the
	# token sequence from a one-token-per-line stream
	strip_pragma_pack() {
		awk -v q="'" '
			{ t[NR] = $0 }
			END {
				i = 1
				while (i <= NR) {
					if (t[i] == q "#" q && t[i + 1] == q "pragma" q && t[i + 2] == q "pack" q) {
						j = i + 2
						while (j <= NR && t[j] != q ")" q)
							j++
						i = j + 1
						continue
					}
					print t[i]
					i++
				}
			}'
	}
	if ! "$tool" ${opts[@]+"${opts[@]}"} pp "$fixture" > "$tmp/mine" 2> "$tmp/err"; then
		echo "cparse-tool failed" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	# shellcheck disable=SC2046
	if ! "$clang" -E -P $(clang_opts) "$fixture" > "$tmp/ref" 2>/dev/null; then
		echo "clang failed" >&2
		exit 1
	fi
	# the files carry their own names; tokenize both outputs the same way
	cp "$tmp/ref" "$tmp/ref.c"
	cp "$tmp/mine" "$tmp/mine.c"
	"$tool" tokens "$tmp/ref.c" 2>/dev/null | toks \
		| strip_pragma_pack > "$tmp/ref.t"
	"$tool" tokens "$tmp/mine.c" 2>/dev/null | toks > "$tmp/mine.t"
	# the blank lines do not count (diff -B is not in every diff)
	sed '/^$/d' "$tmp/ref.t" > "$tmp/ref.n"
	sed '/^$/d' "$tmp/mine.t" > "$tmp/mine.n"
	if ! diff -u "$tmp/ref.n" "$tmp/mine.n"; then
		echo "tokens differ from clang" >&2
		exit 1
	fi
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
exit 0
