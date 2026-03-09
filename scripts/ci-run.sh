#!/bin/bash

set -e

usage() {
	cat <<-EOF
	Usage: $(basename "$0") [OPTIONS]

	Trigger a CMake CI workflow run via gh workflow dispatch.

	Options:
	  -m, --mode MODE        Run mode (see below); other flags override mode defaults
	  -j, --job JOB          Job to run (build, asan, smoke)
	  -o, --os OS            OS to run (e.g. ubuntu-latest, macos-latest, windows-latest)
	  -c, --compiler CC      Compiler (e.g. gcc, clang, msvc)
	  -b, --build-type TYPE  Build type (Debug or Release)
	  -t, --ctest-args ARGS  Extra ctest arguments (e.g. "-R foo", "-E bar")
	  -r, --ref REF          Git ref to run on (default: current branch)
	  -n, --dry-run          Print the command without running it
	  -h, --help             Show this help

	Modes:
	  fast     Ubuntu gcc+clang only, build job (quick per-commit check)
	  full     All platforms, all compilers, all jobs (asan + smoke included)
	  build    All platforms, build job only
	  asan     ASAN job only
	  smoke    Smoke tests only
	  linux    Ubuntu build only
	  macos    macOS build only
	  windows  Windows build only
	EOF
	exit "${1:-0}"
}

OPTS=$(getopt -o m:j:o:c:b:t:r:nh \
	--long mode:,job:,os:,compiler:,build-type:,ctest-args:,ref:,dry-run,help \
	-n "$(basename "$0")" -- "$@") || usage 1

eval set -- "$OPTS"

MODE="" JOB="" OS="" COMPILER="" BUILD_TYPE="" CTEST_ARGS="" REF="" DRY_RUN=0

while true; do
	case "$1" in
		-m|--mode)       MODE="$2";       shift 2 ;;
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

# apply mode defaults (explicit flags override these)
case "$MODE" in
	fast)    : "${JOB:=build}";  : "${OS:=ubuntu-latest}" ;;
	full)    ;;   # no filters - runs everything
	build)   : "${JOB:=build}" ;;
	asan)    : "${JOB:=asan}" ;;
	smoke)   : "${JOB:=smoke}" ;;
	linux)   : "${JOB:=build}";  : "${OS:=ubuntu-latest}" ;;
	macos)   : "${JOB:=build}";  : "${OS:=macos-latest}" ;;
	windows) : "${JOB:=build}";  : "${OS:=windows-latest}" ;;
	"")      ;;   # no mode, use flags as-is
	*)       echo "Unknown mode: $MODE"; usage 1 ;;
esac

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
