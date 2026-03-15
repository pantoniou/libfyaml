fy-tool
=======

Synopsis
--------

**fy-tool** [*OPTIONS*] [<*file*> ...]

**fy-dump** [*OPTIONS*] [<*file*> ...]

**fy-testsuite** [*OPTIONS*] <*file*>

**fy-filter** [*OPTIONS*] [-f *FILE*] [<*path*> ...]

**fy-join** [*OPTIONS*] [-T *PATH*] [-F *PATH*] [-t *PATH*] [<*file*> ...]

**fy-ypath** [*OPTIONS*] <*ypath-expression*> [<*file*> ...]

**fy-compose** [*OPTIONS*] [<*file*> ...]

**fy-b3sum** [*OPTIONS*] [<*file*> ...]

Description
-----------

:program:`fy-tool` is the libfyaml command-line front-end for parsing,
rewriting, querying, joining, testing and hashing YAML and
JSON data.

The executable changes behaviour either by the invoked program name
(``fy-dump``, ``fy-testsuite``, ``fy-filter``, ``fy-join``, ``fy-ypath``,
``fy-compose``, ``fy-b3sum``) or by an explicit mode-select option such as
``--dump`` or ``--filter``.

The default mode is ``dump``.

Options
-------

.. program:: fy-tool

.. rubric:: General options

.. option:: -h, --help

   Display the built-in help message.

.. option:: -v, --version

   Display the libfyaml version.

.. option:: -q, --quiet

   Suppress informational and non-fatal diagnostic output.

.. option:: -d LEVEL, --debug-level LEVEL

   Set the minimum diagnostic severity emitted by the library. The default is
   ``3`` (warning level).

.. option:: -C MODE, --color MODE

   Control ANSI color output. Supported values are ``on``, ``off``, and
   ``auto``. The default is ``auto``.

.. option:: -V, --visible

   Make spaces, tabs, and line breaks visible in text output.

.. option:: --dry-run

   Parse the input but suppress normal output generation.

.. rubric:: Diagnostic options

.. option:: --disable-diag MODULE

   Disable diagnostic output for the named module.

.. option:: --enable-diag MODULE

   Enable diagnostic output for the named module.

.. option:: --show-diag OPTION

   Show the named diagnostic field. Supported values are ``source``,
   ``position``, ``type``, and ``module``.

.. option:: --hide-diag OPTION

   Hide the named diagnostic field.

.. rubric:: Parser options

.. option:: -I DIR, --include DIR

   Add ``DIR`` to the input include search path.

.. option:: -j MODE, --json MODE

   Select JSON input handling. Supported values are ``no``, ``force``, and
   ``auto``. The default is ``auto``.

.. option:: -r, --resolve

   Resolve anchors and merge keys while parsing.

.. option:: --yaml-1.1

   Force YAML 1.1 rules when the input does not declare a version.

.. option:: --yaml-1.2

   Force YAML 1.2 rules when the input does not declare a version.

.. option:: --yaml-1.3

   Force YAML 1.3 rules when the input does not declare a version.

.. option:: -l, --follow

   Follow aliases during path traversal and related path-based operations.

.. option:: --sloppy-flow-indentation

   Accept relaxed indentation rules in flow mode.

.. option:: --prefer-recursive

   Prefer recursive algorithms over iterative ones.

.. option:: --ypath-aliases

   Enable alias processing for ypath evaluation.

.. option:: --allow-duplicate-keys

   Allow duplicate mapping keys.

.. option:: --collect-errors

   Collect multiple parsing errors before reporting failure.

.. option:: --disable-accel

   Disable parser/emitter acceleration features.

.. option:: --disable-buffering

   Disable stdio buffering and use direct file-descriptor reads.

.. option:: --disable-mmap

   Disable memory-mapped input.

.. option:: --disable-depth-limit

   Disable the normal nesting-depth limit.

.. rubric:: Emitter options

.. option:: -i INDENT, --indent INDENT

   Set the output indentation width. The default is ``2``.

.. option:: -w WIDTH, --width WIDTH

   Set the preferred output width. Use ``inf`` for no width limit. The default
   is ``80``.

.. option:: -m MODE, --mode MODE

   Select the output style. Supported values are ``original``, ``block``,
   ``flow``, ``flow-oneline``, ``json``, ``json-tp``, ``json-oneline``,
   ``dejson``, ``pretty``, ``flow-compact``, and ``json-compact``.

.. option:: -s, --sort

   Sort mapping keys before emitting output.

.. option:: -c, --comment

   Preserve and emit comments where supported.

.. option:: --strip-labels

   Strip anchors and aliases from emitted output.

.. option:: --strip-tags

   Strip emitted tags.

.. option:: --strip-doc

   Strip document start and end markers.

.. option:: --strip-empty-kv

   Omit keys whose values are empty or null.

.. option:: --null-output

   Suppress normal output generation.

.. option:: --no-ending-newline

   Do not append a final trailing newline.

.. option:: --preserve-flow-layout

   Preserve single-line flow collection layout when possible.

.. option:: --indented-seq-in-map

   Indent block sequences that appear inside block mappings.

.. option:: --streaming

   Use streaming output mode. This is the default.

.. option:: --no-streaming

   Disable streaming output mode.

.. option:: --recreating

   Recreate streaming events instead of passing them through directly.

.. rubric:: Testsuite options

.. option:: --disable-flow-markers

   Omit testsuite flow markers.

.. option:: --disable-doc-markers

   Omit testsuite document markers.

.. option:: --disable-scalar-styles

   Omit testsuite scalar-style markers.

.. option:: --document-event-stream

   Build a document first and then generate the event stream from it.

.. option:: --tsv-format

   Emit testsuite output in TSV format.

.. rubric:: Input options

.. option:: -f FILE, --file FILE

   Read from ``FILE`` instead of standard input where the selected mode uses a
   single input source. If ``FILE`` begins with ``>``, the remaining text is
   treated as literal input content.

.. rubric:: Join options

.. option:: -T PATH, --to PATH

   Join into ``PATH``. The default is ``/``.

.. option:: -F PATH, --from PATH

   Take joined input from ``PATH``. The default is ``/``.

.. option:: -t PATH, --trim PATH

   Emit only ``PATH`` from the joined result. The default is ``/``.

.. rubric:: YPATH options

.. option:: -F PATH, --from PATH

   Start evaluation from ``PATH``. The default is ``/``.

.. option:: --dump-pathexpr

   Dump the parsed path expression before evaluation.

.. option:: --noexec

   Parse the expression but do not execute it.

.. rubric:: Compose options

.. option:: --dump-path

   Dump the path while composing a document.

.. rubric:: B3SUM options

.. option:: --b3sum

   Enable BLAKE3 hashing mode.

.. option:: --derive-key CONTEXT

   Use key-derivation mode with ``CONTEXT``.

.. option:: --no-names

   Omit file names from checksum output.

.. option:: --raw

   Emit raw digest bytes.

.. option:: --length N

   Emit only ``N`` output bytes, up to the BLAKE3 maximum.

.. option:: --check

   Verify checksums read from input files.

.. option:: --keyed

   Use keyed hashing, reading the secret key from standard input.

.. option:: --backend BACKEND

   Select the BLAKE3 backend implementation.

.. option:: --list-backends

   List available BLAKE3 backends.

.. option:: --num-threads N

   Set the worker-thread count. ``0`` means automatic selection and ``-1``
   disables threaded hashing.

.. option:: --file-buffer N

   Set the per-file I/O buffer size.

.. option:: --mmap-min-chunk N

   Set the minimum chunk size used for memory-mapped hashing input.

.. option:: --mmap-max-chunk N

   Set the maximum chunk size used for memory-mapped hashing input.

.. rubric:: Modes

.. option:: --dump

   Select dump mode. This is the default and also the mode used by
   :program:`fy-dump`.

.. option:: --testsuite

   Select testsuite mode. This is also the mode used by
   :program:`fy-testsuite`.

.. option:: --filter

   Select filter mode. This is also the mode used by :program:`fy-filter`.

.. option:: --join

   Select join mode. This is also the mode used by :program:`fy-join`.

.. option:: --ypath

   Select ypath query mode. This is also the mode used by :program:`fy-ypath`.

.. option:: --scan-dump

   Select the internal scan-dump mode.

.. option:: --parse-dump

   Select the internal parse-dump mode.

.. option:: --compose

   Select composer-driver dump mode. This is also the mode used by
   :program:`fy-compose`.

.. option:: --yaml-version-dump

   Print YAML version information.

Examples
--------

Dump a YAML file back out using the default emitter mode:

.. code-block:: bash

   $ fy-dump invoice.yaml

Resolve anchors and strip labels from the result:

.. code-block:: bash

   $ fy-dump -r --strip-labels simple-anchors.yaml

Read literal input text via ``--file``:

.. code-block:: bash

   $ fy-filter --file \">foo: bar\" /

Select data from a file using a path expression:

.. code-block:: bash

   $ fy-filter --file simple-anchors.yaml /baz/bar

Join two root mappings into a single document:

.. code-block:: bash

   $ fy-join simple-anchors.yaml invoice.yaml

Generate testsuite event output:

.. code-block:: bash

   $ fy-testsuite invoice.yaml

List available BLAKE3 backends:

.. code-block:: bash

   $ fy-b3sum --list-backends

Author
------

Pantelis Antoniou <pantelis.antoniou@konsulko.com>

Bugs
----

* The only supported input and output character encoding is UTF8.
* Sorting does not respect language settings.
* There is no way for the user to specific a different coloring scheme.

See also
--------

:manpage:`libfyaml(3)`
