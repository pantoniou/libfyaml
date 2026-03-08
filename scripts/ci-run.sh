#!/bin/bash

set -e

usage() {
	cat <<-EOF
	Usage: $(basename "$0") [OPTIONS]

	Trigger a CMake CI workflow run via gh workflow dispatch.

	Options:
	  -j, --job JOB          Job to run (build, asan, smoke)
	  -o, --os OS            OS to run (e.g. ubuntu-latest, macos-latest, windows-latest)
	  -c, --compiler CC      Compiler (e.g. gcc, clang, msvc)
	  -b, --build-type TYPE  Build type (Debug or Release)
	  -t, --ctest-args ARGS  Extra ctest arguments (e.g. "-R foo", "-E bar")
	  -r, --ref REF          Git ref to run on (default: current branch)
	  -n, --dry-run          Print the command without running it
	  -h, --help             Show this help
	EOF
	exit "${1:-0}"
}

OPTS=$(getopt -o j:o:c:b:t:r:nh \
	--long job:,os:,compiler:,build-type:,ctest-args:,ref:,dry-run,help \
	-n "$(basename "$0")" -- "$@") || usage 1

eval set -- "$OPTS"

JOB="" OS="" COMPILER="" BUILD_TYPE="" CTEST_ARGS="" REF="" DRY_RUN=0

while true; do
	case "$1" in
		-j|--job)        JOB="$2";        shift 2 ;;
		-o|--os)         OS="$2";         shift 2 ;;
		-c|--compiler)   COMPILER="$2";   shift 2 ;;
		-b|--build-type) BUILD_TYPE="$2"; shift 2 ;;
		-t|--ctest-args) CTEST_ARGS="$2"; shift 2 ;;
		-r|--ref)        REF="$2";        shift 2 ;;
		-n|--dry-run)    DRY_RUN=1;       shift ;;
		-h|--help)       usage 0 ;;
		--)              shift; break ;;
		*)               usage 1 ;;
	esac
done

# default ref to current branch
if [ -z "$REF" ]; then
	REF=$(git symbolic-ref --short HEAD 2>/dev/null || git rev-parse --short HEAD)
fi

CMD=(gh workflow run "CMake CI" --ref "$REF")

[ -n "$JOB" ]        && CMD+=(-f "job=$JOB")
[ -n "$OS" ]         && CMD+=(-f "os=$OS")
[ -n "$COMPILER" ]   && CMD+=(-f "compiler=$COMPILER")
[ -n "$BUILD_TYPE" ] && CMD+=(-f "build_type=$BUILD_TYPE")
[ -n "$CTEST_ARGS" ] && CMD+=(-f "ctest_args=$CTEST_ARGS")

if [ "$DRY_RUN" -eq 1 ]; then
	echo "${CMD[@]}"
else
	echo "${CMD[@]}"
	"${CMD[@]}"
fi
