#!/usr/bin/env bash

# Wrapper script to run a single TAP subtest
# Usage: run-single-tap-test.sh <test_suite_name> <test_id>

test_suite="$1"
test_id="$2"

case "$test_suite" in
    testerrors)
        dir="${SRCDIR}/test-errors/${test_id}"
        desctxt=$(cat 2>/dev/null "$dir/===")

        t=$(mktemp)
        res="not ok"

        pass_yaml=0
        ${TOP_BUILDDIR}/src/fy-tool --dump -r "$dir/in.yaml" >"$t" 2>&1
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
        f="${SRCDIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${TOP_BUILDDIR}/src/fy-tool --dump "$f" | \
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers - >"$t2"
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
        f="${SRCDIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${TOP_BUILDDIR}/src/fy-tool --dump --streaming "$f" | \
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers - >"$t2"
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
        f="${SRCDIR}/emitter-examples/${test_id}"

        t1=$(mktemp)
        t2=$(mktemp)

        res="not ok"

        pass_parse=0
        ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers "$f" >"$t1"
        if [ $? -eq 0 ]; then
            ${TOP_BUILDDIR}/src/fy-tool --dump --streaming --recreating "$f" | \
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --disable-flow-markers - >"$t2"
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
        tst="test-suite-data/${test_id}"
        desctxt=$(cat 2>/dev/null "$tst/===")

        t=$(mktemp)

        res="not ok"

        pass_yaml=0
        ${TOP_BUILDDIR}/src/fy-tool --testsuite "$tst/in.yaml" >"$t"
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
        tst="test-suite-data/${test_id}"
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
	${TOP_BUILDDIR}/src/fy-tool --testsuite --document-event-stream "$tst/in.yaml" >"$t" 2>/dev/null
	if [ $? -eq 0 ]; then
	    diff -u "$tst/test.event" "$t"
	    if [ $? -eq 0 ]; then
	        res="ok"
	    fi
	fi

        rm -f "$t1"

        echo "$res 1 $test_id - $desctxt"

        if [ "$res" == "ok" ]; then
            exit 0
        else
            exit 1
        fi
        ;;

    jsontestsuite)
        tf="$test_id"
        f="json-test-suite-data/test_parsing/${tf}"

        # Determine expected result based on prefix
        case "$tf" in
            y_*)
                # Expected to pass
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --streaming "$f" >/dev/null 2>&1
                if [ $? -eq 0 ]; then
                    res="ok"
                else
                    res="not ok"
                fi
                ;;
            n_*)
                # Expected to fail
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --streaming "$f" >/dev/null 2>&1
                if [ $? -eq 0 ]; then
                    res="not ok"
                else
                    res="ok"
                fi
                ;;
            i_*)
                # Implementation defined - we'll accept either result as ok
                ${TOP_BUILDDIR}/src/fy-tool --testsuite --streaming "$f" >/dev/null 2>&1
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
