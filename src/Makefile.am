bin_PROGRAMS =
noinst_PROGRAMS =
noinst_LIBRARIES =
lib_LTLIBRARIES =
noinst_LTLIBRARIES =

AM_CPPFLAGS = \
        -I$(top_srcdir)/src \
        -I$(top_srcdir)/include
AM_CFLAGS =

lib_LTLIBRARIES += libfyaml.la
libfyaml_la_SOURCES = \
        lib/fy-parse.c lib/fy-parse.h \
	lib/fy-types.c lib/fy-types.h \
	lib/fy-diag.c lib/fy-diag.h \
	lib/fy-dump.c lib/fy-dump.h \
	lib/fy-atom.c lib/fy-atom.h \
	lib/fy-token.c lib/fy-token.h \
	lib/fy-input.c lib/fy-input.h \
	lib/fy-docstate.c lib/fy-docstate.h \
	lib/fy-doc.c lib/fy-doc.h \
	lib/fy-docbuilder.c lib/fy-docbuilder.h \
	lib/fy-emit.c lib/fy-emit.h lib/fy-emit-accum.h \
	lib/fy-event.h lib/fy-event.c \
	lib/fy-accel.c lib/fy-accel.h \
	lib/fy-walk.c lib/fy-walk.h \
	lib/fy-path.c lib/fy-path.h \
	lib/fy-composer.c lib/fy-composer.h \
	xxhash/xxhash.c xxhash/xxhash.h \
	util/fy-list.h \
	util/fy-typelist.h \
	util/fy-ctype.c util/fy-ctype.h \
	util/fy-utf8.c util/fy-utf8.h \
	util/fy-utils.c util/fy-utils.h \
	util/fy-endian.h \
	util/fy-blob.c util/fy-blob.h \
	util/fy-id.h \
	util/fy-align.h \
	util/fy-bit64.h \
	thread/fy-thread.h thread/fy-thread.c

libfyaml_la_CPPFLAGS = $(AM_CPPFLAGS) \
		       -I$(top_srcdir)/src/lib \
		       -I$(top_srcdir)/src/xxhash \
		       -I$(top_srcdir)/src/util \
		       -I$(top_srcdir)/src/thread
libfyaml_la_CFLAGS = $(AM_CFLAGS)
libfyaml_la_LDFLAGS = -no-undefined $(AM_LDFLAGS) $(AM_LIBLDFLAGS) \
		      -version $(LIBTOOL_VERSION)
libfyaml_la_LIBADD =

# blake3 section
libfyaml_la_CPPFLAGS += -I$(top_srcdir)/src/blake3
libfyaml_la_SOURCES += blake3/blake3.h \
		       blake3/blake3_impl.h \
		       blake3/blake3_internal.h \
		       blake3/blake3_host_state.c \
		       blake3/blake3_backend.c \
		       blake3/blake3_be_cpusimd.c \
		       blake3/fy-blake3.c

LIBB3_COMMON_CPPFLAGS=$(AM_CPPFLAGS) -I$(top_srcdir)/src/util -I$(top_srcdir)/src/thread

# the portable implementation
noinst_LTLIBRARIES += libb3portable.la
libb3portable_la_SOURCES = blake3/blake3_portable.c blake3/blake3.c
libb3portable_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -DHASHER_SUFFIX=portable -DSIMD_DEGREE=1
libb3portable_la_CFLAGS = $(AM_CPPFLAGS)
libfyaml_la_LIBADD += libb3portable.la

# note that each target lib has blake3 compiled again but with a suffix
if TARGET_HAS_SSE2
noinst_LTLIBRARIES += libb3sse2.la
libb3sse2_la_SOURCES = blake3/blake3_sse2.c blake3/blake3_sse2_x86-64_unix.S blake3/blake3.c
libb3sse2_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -msse2 -DHASHER_SUFFIX=sse2 -DSIMD_DEGREE=4
libb3sse2_la_CFLAGS = $(AM_CPPFLAGS) -msse2
libfyaml_la_LIBADD += libb3sse2.la
endif

if TARGET_HAS_SSE41
noinst_LTLIBRARIES += libb3sse41.la
libb3sse41_la_SOURCES = blake3/blake3_sse41.c blake3/blake3_sse41_x86-64_unix.S blake3/blake3.c
libb3sse41_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -msse4.1 -DHASHER_SUFFIX=sse41 -DSIMD_DEGREE=4
libb3sse41_la_CFLAGS = $(AM_CPPFLAGS) -msse4.1
libfyaml_la_LIBADD += libb3sse41.la
endif

if TARGET_HAS_AVX2
noinst_LTLIBRARIES += libb3avx2.la
libb3avx2_la_SOURCES = blake3/blake3_avx2.c blake3/blake3_avx2_x86-64_unix.S blake3/blake3.c
libb3avx2_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -mavx2 -DHASHER_SUFFIX=avx2 -DSIMD_DEGREE=8
libb3avx2_la_CFLAGS = $(AM_CPPFLAGS) -mavx2
libfyaml_la_LIBADD += libb3avx2.la
endif

if TARGET_HAS_AVX512
noinst_LTLIBRARIES += libb3avx512.la
libb3avx512_la_SOURCES = blake3/blake3_avx512.c blake3/blake3_avx512_x86-64_unix.S blake3/blake3.c
libb3avx512_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -mavx512f -mavx512vl -DHASHER_SUFFIX=avx512 -DSIMD_DEGREE=16
libb3avx512_la_CFLAGS = $(AM_CPPFLAGS) -mavx512f -mavx512vl
libfyaml_la_LIBADD += libb3avx512.la
endif

if TARGET_HAS_NEON
noinst_LTLIBRARIES += libb3neon.la
libb3neon_la_SOURCES = blake3/blake3_neon.c blake3/blake3.c
libb3neon_la_CPPFLAGS = $(LIBB3_COMMON_CPPFLAGS) -DHASHER_SUFFIX=neon -DSIMD_DEGREE=4
libb3neon_la_CFLAGS = $(AM_CPPFLAGS)
if TARGET_CPU_ARM
libb3neon_la_CPPFLAGS += -mfpu=neon
libb3neon_la_CFLAGS += -mfpu=neon
endif
libfyaml_la_LIBADD += libb3neon.la
endif

# libfyaml-parser needs both LIBYAML and static
if HAVE_LIBYAML
if HAVE_STATIC

noinst_PROGRAMS += libfyaml-parser

libfyaml_parser_SOURCES = \
	internal/libfyaml-parser.c \
	valgrind/fy-valgrind.h

libfyaml_parser_CPPFLAGS = $(AM_CPPFLAGS) \
			   -I$(top_srcdir)/src/valgrind \
			   -I$(top_srcdir)/src/lib \
			   -I$(top_srcdir)/src/xxhash \
			   -I$(top_srcdir)/src/util \
			   -I$(top_srcdir)/src/thread
libfyaml_parser_LDADD = $(AM_LDADD) $(LIBYAML_LIBS) libfyaml.la
libfyaml_parser_CFLAGS = $(AM_CFLAGS) $(LIBYAML_CFLAGS)

libfyaml_parser_LDFLAGS = $(AM_LDFLAGS) -static
endif
endif

# fy-thread
if HAVE_STATIC

noinst_PROGRAMS += fy-thread

fy_thread_SOURCES = \
	internal/fy-thread.c \
	valgrind/fy-valgrind.h

fy_thread_CPPFLAGS = $(AM_CPPFLAGS) \
			   -I$(top_srcdir)/src/valgrind \
			   -I$(top_srcdir)/src/lib \
			   -I$(top_srcdir)/src/xxhash \
			   -I$(top_srcdir)/src/util \
			   -I$(top_srcdir)/src/thread
fy_thread_LDADD = $(AM_LDADD) $(LIBYAML_LIBS) libfyaml.la
fy_thread_CFLAGS = $(AM_CFLAGS) $(LIBYAML_CFLAGS)

fy_thread_LDFLAGS = $(AM_LDFLAGS) -static
endif

# fy-b3sum
if HAVE_STATIC

noinst_PROGRAMS += fy-b3sum

fy_b3sum_SOURCES = \
	internal/fy-b3sum.c \
	valgrind/fy-valgrind.h

fy_b3sum_CPPFLAGS = $(AM_CPPFLAGS) \
			   -I$(top_srcdir)/src/valgrind \
			   -I$(top_srcdir)/src/lib \
			   -I$(top_srcdir)/src/xxhash \
			   -I$(top_srcdir)/src/util \
			   -I$(top_srcdir)/src/thread \
			   -I$(top_srcdir)/src/blake3
fy_b3sum_LDADD = $(AM_LDADD) $(LIBYAML_LIBS) libfyaml.la
fy_b3sum_CFLAGS = $(AM_CFLAGS) $(LIBYAML_CFLAGS)

fy_b3sum_LDFLAGS = $(AM_LDFLAGS) -static
endif

bin_PROGRAMS += fy-tool

fy_tool_SOURCES = \
	tool/fy-tool.c \
	valgrind/fy-valgrind.h

fy_tool_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/valgrind
fy_tool_LDADD = $(AM_LDADD) libfyaml.la
fy_tool_CFLAGS = $(AM_CFLAGS) $(STATIC_TOOLS_CFLAGS)
fy_tool_LDFLAGS = $(AM_LDFLAGS) $(STATIC_TOOLS_LDFLAGS)

include_HEADERS = \
        $(top_srcdir)/include/libfyaml.h

install-exec-hook:
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-dump)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-filter)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-testsuite)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-join)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-ypath)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-compose)

uninstall-hook:
	(cd "$(DESTDIR)$(bindir)" && rm -f fy-dump fy-filter fy-testsuite fy-join fy-ypath fy-compose)
