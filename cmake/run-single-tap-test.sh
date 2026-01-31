#!/usr/bin/env bash

# Wrapper script to run a single TAP subtest
# Usage: run-single-tap-test.sh <fy_tool_path> <test_suite_name> <test_id>

FY_TOOL="$1"
test_suite="$2"
test_id="$3"

if [ x"$FY_TOOL" = x -o x"$TEST_DIR" = x -o x"$test_suite" = x -o x"$test_id" = x ]; then
    echo "Error: FY_TOOL, TEST_DIR, test_suite, test_id must exist"
fi

function is_windows_bash() {
    case "$OSTYPE" in
        msys*|cygwin*) return 0 ;;
        *) return 1 ;;
    esac
}

# convert to proper posix paths
if is_windows_bash; then
    FY_TOOL=`cygpath $FY_TOOL`
    TEST_DIR=`cygpath $TEST_DIR`
    YAML_TEST_SUITE=`cygpath $YAML_TEST_SUITE`
    JSON_TEST_SUITE=`cygpath $JSON_TEST_SUITE`
fi

# if not given in the environment try to adjust
if [ x"$YAML_TEST_SUITE" = x ]; then
	YAML_TEST_SUITE="${TEST_DIR}/test-suite-data"
fi

if [ x"$JSON_TEST_SUITE" = x ]; then
	JSON_TEST_SUITE="${TEST_DIR}/json-test-suite-data"
fi

# Validate tool exists
if [ ! -x "$FY_TOOL" ]; then
    echo "Error: fy-tool not found or not executable: $FY_TOOL" >&2
    exit 1
fi

# Validate TEST_DIR exists
if [ ! -d "$TEST_DIR" ]; then
    echo "Error: TEST_DIR not found: $TEST_DIR" >&2
    exit 1
fi

case "$test_suite" in
    testerrors)
        dir="${TEST_DIR}/test-errors/${test_id}"
        desctxt=$(cat 2>/dev/null "$dir/===")

        t=$(mktemp)
        res="not ok"

        pass_yaml=0
        ${FY_TOOL} --dump -r --no-streaming "$dir/in.yaml" >"$t" 2>&1
        if [ $? -eq 0 ]; then
            pass_yaml=1
        fi

        errmsg=$(cat "$t" | head -n1 | sed -e 's/^[^:]*//')
        echo "errmsg: $errmsg" >&2

        # Replace with error message
        echo "$errmsg" >"$t"

        # All error tests are expected to fail
        if [ "$pass_yaml" == "0" ]; then
            res="ok"

            # diff is pointless under valgrind
            if [ "x$USE_VALGRIND" == "x" ]; then
                diff_err=0
                diff -u "$dir/test.error" "$t"
                if [ $? -eq 0 ]; then
                    res="ok"
                else
                    res="not ok"
                fi
            fi
        else
            res="not ok"
        fi

        rm -f "$t"

        echo "$res 1 $test_id - $desctxt"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    testemitter)
        f="${TEST_DIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${FY_TOOL} --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${FY_TOOL} --dump "$f" | \
                ${FY_TOOL} --testsuite --disable-flow-markers - >"$t2"
            if [ $? -eq 0 ]; then
                pass_parse=1
            fi
        fi

        if [ "$pass_parse" == "1" ]; then
            diff -u "$t1" "$t2"
            if [ $? -eq 0 ]; then
                res="ok"
            else
                res="not ok"
            fi
        else
            res="not ok"
        fi

        rm -f "$t1" "$t2"

        echo "$res 1 - $test_id"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    testemitter-streaming)
        f="${TEST_DIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${FY_TOOL} --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${FY_TOOL} --dump --streaming "$f" | \
                ${FY_TOOL} --testsuite --disable-flow-markers - >"$t2"
            if [ $? -eq 0 ]; then
                pass_parse=1
            fi
        fi

        if [ "$pass_parse" == "1" ]; then
            diff -u "$t1" "$t2"
            if [ $? -eq 0 ]; then
                res="ok"
            else
                res="not ok"
            fi
        else
            res="not ok"
        fi

        rm -f "$t1" "$t2"

        echo "$res 1 - $test_id"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    testemitter-restreaming)
        f="${TEST_DIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${FY_TOOL} --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${FY_TOOL} --dump --streaming --recreating "$f" | \
                ${FY_TOOL} --testsuite --disable-flow-markers - >"$t2"
            if [ $? -eq 0 ]; then
                pass_parse=1
            fi
        fi

        if [ "$pass_parse" == "1" ]; then
            diff -u "$t1" "$t2"
            if [ $? -eq 0 ]; then
                res="ok"
            else
                res="not ok"
            fi
        else
            res="not ok"
        fi

        rm -f "$t1" "$t2"

        echo "$res 1 - $test_id"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    testsuite|testsuite-json|testsuite-resolution)
        tst="${YAML_TEST_SUITE}/${test_id}"
        desctxt=$(cat 2>/dev/null "$tst/===")

        t=$(mktemp)

        res="not ok"

        pass_yaml=0
        ${FY_TOOL} --testsuite "$tst/in.yaml" >"$t"
        if [ $? -eq 0 ]; then
            pass_yaml=1
        fi

        if [ -e "$tst/error" ]; then
            # Test is expected to fail
            if [ $pass_yaml == "0" ]; then
                res="ok"
            else
                res="not ok"
            fi
        else
            # Test is expected to pass
            if [ $pass_yaml == "1" ]; then
                diff -u "$tst/test.event" "$t"
                if [ $? -eq 0 ]; then
                    res="ok"
                else
                    res="not ok"
                fi
            else
                res="not ok"
            fi
        fi

        rm -f "$t"

        echo "$res 1 $test_id - $desctxt"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    testsuite-evstream)
        tst="${YAML_TEST_SUITE}/${test_id}"
        desctxt=$(cat 2>/dev/null "$tst/===")

        # Skip tests that are expected to fail
        if [ -e "$tst/error" ]; then
            # echo "ok 1 $test_id - $desctxt # SKIP expected to fail"
            echo "skip 1 $test_id - $desctxt # SKIP expected to fail"
            exit 0
        fi

	# xfails
	case "${test_id}" in
            2JQS)
                echo "skip 1 $test_id - $desctxt # SKIP expected to fail"
		exit 0
		;;
        esac

        t=$(mktemp)

        res="not ok"

	# run the test using document-event-stream
	${FY_TOOL} --testsuite --document-event-stream "$tst/in.yaml" >"$t" 2>/dev/null
	if [ $? -eq 0 ]; then
	    diff -u "$tst/test.event" "$t"
	    if [ $? -eq 0 ]; then
	        res="ok"
	    fi
	fi

        rm -f "$t"

        echo "$res 1 $test_id - $desctxt"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    jsontestsuite)
        tf="$test_id"
        f="${JSON_TEST_SUITE}/test_parsing/${tf}"

        # Determine expected result based on prefix
        case "$tf" in
            y_*)
                # Expected to pass
                ${FY_TOOL} --testsuite --streaming "$f" >/dev/null 2>&1
                if [ $? -eq 0 ]; then
                    res="ok"
                else
                    res="not ok"
                fi
                ;;
            n_*)
                # Expected to fail
                ${FY_TOOL} --testsuite --streaming "$f" >/dev/null 2>&1
                if [ $? -eq 0 ]; then
                    res="not ok"
                else
                    res="ok"
                fi
                ;;
            i_*)
                # Implementation defined - we'll accept either result as ok
                ${FY_TOOL} --testsuite --streaming "$f" >/dev/null 2>&1
                res="ok"
                ;;
            *)
                res="not ok"
                ;;
        esac

        echo "$res 1 - $tf"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    *)
        echo "Unknown test suite: $test_suite" >&2
        exit 1
        ;;
esac
