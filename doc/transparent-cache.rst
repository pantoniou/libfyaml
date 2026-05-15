Transparent Parse Caching
=========================

libfyaml can cache parsed input in a generic arena and reuse that cached
representation on later runs. The cache is transparent in the sense that the
caller still uses the normal parse entry points. When the cache is enabled and
usable, libfyaml decides whether to parse the original input or to load the
cached representation.

What It Does
------------

The transparent cache targets repeated reads of stable YAML or JSON inputs,
especially during process startup. A cache hit avoids re-parsing the original
text and instead loads a previously serialized generic arena.

The cache is currently conservative:

* it is opt-in
* it only applies to inputs that are eligible for content-addressed caching
* incompatible, corrupt, or unreadable entries are treated as cache misses

The cache is content-addressed. The key includes the input bytes and the
parser and decoder options that affect semantics, so changing either the input
or the relevant parse configuration produces a different cache entry.

Relationship With Generics
--------------------------

The cache is implemented on top of the generic subsystem. That is an internal
design choice with visible consequences:

* generic support must be available for transparent caching to work
* the cached payload is a serialized generic arena
* cache hits are best suited to generic-oriented consumers

This does not mean the core parser API becomes a generic API. The core parser,
document, and emitter interfaces remain unchanged. It means the cache backend
uses generics as its stored representation because generics already provide:

* a compact immutable value model
* arena-based storage
* deduplication support
* stable serialization and relocation machinery

For generic consumers, a cache hit can often return directly to generic data.
For non-generic consumers, libfyaml may still need to replay parser events or
reconstruct higher-level structures from the cached generic representation.
That is why the cache/backend relationship matters when reasoning about hit
performance.

How It Works
------------

On a cache miss:

1. libfyaml parses the input normally.
2. In parallel with normal parsing, it builds a generic representation.
3. At successful end of stream, it linearizes the generic arena.
4. It writes a cache file atomically under the cache directory.

On a cache hit:

1. libfyaml computes the same content-addressed key.
2. It locates the cache file and validates the header.
3. It tries to map the stored arena at the recorded address.
4. If fixed-address mapping is not possible, it falls back to relocation.
5. The cached generic arena is then used as the parse result backend.

The zero-copy fast path is the fixed-address mapping case. Relocation is the
fallback path for cases where the original address range is not available.

What Gets Cached
----------------

The cache stores:

* cache format metadata
* the original source name
* parser and decoder flags relevant to behavior
* the serialized generic arena payload

It does not store the internal parser state machine directly.

Cache Location
--------------

By default, libfyaml uses:

* ``$XDG_CACHE_HOME/libfyaml`` when ``XDG_CACHE_HOME`` is set
* otherwise ``~/.cache/libfyaml``

The current cache directory can be queried programmatically via
``fy_parse_cache_get_dir()``. The directory may also be overridden for the
current process via ``fy_parse_cache_override()``.

Public Cache Inspection API
---------------------------

The core public cache inspection API consists of:

* ``fy_parse_cache_get_dir()`` to resolve the active cache directory
* ``fy_parse_cache_file_info_load()`` to inspect one cache file
* ``fy_parse_cache_walk()`` to enumerate cache entries
* ``fy_parse_cache_override()`` to redirect the cache root for the current
  process

The associated metadata container is ``struct fy_parse_cache_file_info``.

When To Use It
--------------

Transparent caching is a good fit when:

* the same files are parsed repeatedly across process starts
* the inputs are large enough that parsing dominates startup cost
* the input contents change infrequently relative to reads

It is less useful when:

* the inputs are tiny
* the inputs are highly volatile
* the workload is dominated by one-off parsing

Configuration Surfaces
----------------------

The cache is disabled unless explicitly enabled by the calling API surface.
Examples include:

* parser configuration using ``FYPCF_ENABLE_CACHE``
* generic parse operations using the corresponding generic parse flag
* Python binding helpers with ``enable_cache=True``

The cache implementation also has environment-controlled behavior for testing
and diagnostics, including cache directory override and relocation forcing.

See Also
--------

* :doc:`intro`
* :doc:`generics-guide`
* :doc:`libfyaml-core`
