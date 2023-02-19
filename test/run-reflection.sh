#!/bin/env bash
d=`realpath $1`
if [ $? -ne 0 ]; then
	echo "bad directory"
	exit 1
fi
if [ ! -d "$d" ]; then
	echo "directory does not exist"
	exit 1
fi
rm -f out.yaml out.event
echo "> contents of $d"
ls "$d"
echo "> contents of ==="
cat "$d/==="
def="$d/definition.h"
meta="$d/meta"
entry=`cat $d/entry`
echo "> dump of $d/in.yaml"
../src/fy-tool --dump "$d/in.yaml"
if [ $? -ne 0 ]; then
	exit 1
fi
if [ -f "$def" ]; then
	echo "> dump of $d/definition.h"
	cat "$def"
fi
if [ -f "$meta" ]; then
	echo "> dump of $d/meta"
	cat "$meta"
fi
echo "> dump of $d/entry"
cat "$d/entry"
echo "> executing reflection on $d/in.yaml, entry '${entry}'"
META_ARGS=()
DEF_ARGS=()
if [ -f "$def" ]; then
	DEF_ARGS=("--import-c-file" "$def")
fi
if [ -f "$meta" ]; then
	metaval=`cat $meta | head -n 1`
	META_ARGS=("--entry-meta" "${metaval}")
fi
echo "\$ ../src/fy-tool --reflect ${DEF_ARGS[@]} ${META_ARGS[@]} --entry-type '${entry}' $d/in.yaml"
../src/fy-tool --reflect "${DEF_ARGS[@]}" "${META_ARGS[@]}" --entry-type "${entry}" "$d/in.yaml" | tee out.yaml
if [ $? -ne 0 ]; then
	exit 1
fi
echo "> creating testsuite out.event"
../src/fy-tool --testsuite --disable-flow-markers --disable-doc-markers --disable-scalar-styles out.yaml | tee out.event
if [ $? -ne 0 ]; then
	exit 1
fi
echo "> comparing with $d/test.event"
diff -u "$d/test.event" out.event
if [ $? -ne 0 ]; then
	exit 1
fi
echo "> OK"
