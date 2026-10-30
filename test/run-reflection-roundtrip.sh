#!/bin/bash
# SPDX-License-Identifier: MIT
#
# run-reflection-roundtrip.sh MODE HEADER
#
#   MODE roundtrip  reflect HEADER with the cparse backend, generate C from the
#                   reflection, reflect the C again and compare both
#   MODE roundtrip-i386  the same for the i386 target (32 bit layout rules, on any host)
#   MODE layout     compare the reflection of HEADER with the file NAME.expected
#                   that sits next to it (a 64 bit host)
#   MODE macros     read HEADER with the macros of NAME.macros, as the
#                   configuration of the backend and as -imacros, and compare
#                   with NAME.expected; without the compiled-in macros the
#                   result must differ
#   MODE backends   compare the reflection of HEADER by the clang backend with
#                   the one by the cparse backend
#
# Environment: BACKEND_DUMP, the reflection-backend-dump tool.
set -u

mode=${1:?usage: run-reflection-roundtrip.sh MODE HEADER}
header=${2:?usage: run-reflection-roundtrip.sh MODE HEADER}
dump=${BACKEND_DUMP:?BACKEND_DUMP is not set}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# the compiler builtin va_list is not in the generated C
filter() {
	grep -v -E '__va_list_tag|__builtin_va_list'
}

case $mode in
roundtrip|roundtrip-i386)
	opts=()
	[ "$mode" = roundtrip-i386 ] && opts=(-target i386-linux-gnu)
	if ! BACKEND_DUMP_GENERATE=1 "$dump" cparse "$header" ${opts[@]+"${opts[@]}"} > "$tmp/generated.h" 2> "$tmp/err"; then
		echo "generating C failed" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	BACKEND_DUMP_SYSTEM=1 "$dump" cparse "$header" ${opts[@]+"${opts[@]}"} 2> /dev/null | filter > "$tmp/first"
	if ! "$dump" cparse "$tmp/generated.h" ${opts[@]+"${opts[@]}"} 2> "$tmp/err" | filter > "$tmp/second"; then
		cat "$tmp/err" >&2
		exit 1
	fi
	if [ -s "$tmp/err" ]; then
		echo "the generated C does not parse" >&2
		cat "$tmp/err" >&2
		exit 1
	fi
	if ! diff -u "$tmp/first" "$tmp/second"; then
		echo "the reflection of the generated C differs" >&2
		exit 1
	fi
	;;
layout)
	expected=${header%.h}.expected
	"$dump" cparse "$header" 2> /dev/null > "$tmp/layout"
	if ! diff -u "$expected" "$tmp/layout"; then
		echo "the reflection differs from $expected" >&2
		exit 1
	fi
	;;
macros)
	macros=${header%.h}.macros
	expected=${header%.h}.expected
	BACKEND_DUMP_MACROS=$macros "$dump" cparse "$header" 2> /dev/null > "$tmp/config"
	if ! diff -u "$expected" "$tmp/config"; then
		echo "the macros of the configuration do not give $expected" >&2
		exit 1
	fi
	"$dump" cparse "$header" -imacros "$macros" 2> /dev/null > "$tmp/option"
	if ! diff -u "$expected" "$tmp/option"; then
		echo "-imacros does not give $expected" >&2
		exit 1
	fi
	BACKEND_DUMP_NO_PREDEFS=1 BACKEND_DUMP_MACROS=$macros "$dump" cparse "$header" \
		2> /dev/null > "$tmp/none"
	if diff -q "$expected" "$tmp/none" > /dev/null; then
		echo "no_predefs did not remove the compiled-in macros" >&2
		exit 1
	fi
	;;
backends)
	"$dump" clang "$header" 2> /dev/null > "$tmp/clang"
	"$dump" cparse "$header" 2> /dev/null > "$tmp/cparse"
	if ! diff -u "$tmp/clang" "$tmp/cparse"; then
		echo "the backends differ" >&2
		exit 1
	fi
	;;
*)
	echo "unknown mode $mode" >&2
	exit 2
	;;
esac
exit 0
