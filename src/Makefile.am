AM_CPPFLAGS = \
        -I$(top_srcdir)/src \
        -I$(top_srcdir)/include
AM_CFLAGS =

lib_LTLIBRARIES = libfyaml.la
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
	util/fy-vlsize.h \
	util/fy-allocator.c util/fy-allocator.h \
		util/fy-allocator-linear.c util/fy-allocator-linear.h \
		util/fy-allocator-malloc.c util/fy-allocator-malloc.h \
		util/fy-allocator-mremap.c util/fy-allocator-mremap.h \
		util/fy-allocator-dedup.c util/fy-allocator-dedup.h \
		util/fy-allocator-auto.c util/fy-allocator-auto.h \
	util/fy-generic.c util/fy-generic.h \
	util/fy-generic-decoder.c util/fy-generic-decoder.h \
	util/fy-generic-encoder.c util/fy-generic-encoder.h \
	reflection/fy-reflection.c reflection/fy-reflection-private.h \
	reflection/fy-packed-backend.c reflection/fy-packed-backend.h \
	reflection/fy-registry.c

if HAVE_LIBCLANG
libfyaml_la_SOURCES += \
	reflection/fy-clang-backend.c reflection/fy-clang-backend.h
endif

libfyaml_la_CPPFLAGS = $(AM_CPPFLAGS) \
		       -I$(top_srcdir)/src/lib \
		       -I$(top_srcdir)/src/xxhash \
		       -I$(top_srcdir)/src/util
libfyaml_la_CFLAGS = $(AM_CFLAGS)
libfyaml_la_LDFLAGS = -no-undefined $(AM_LDFLAGS) $(AM_LIBLDFLAGS) \
		      -version $(LIBTOOL_VERSION)

if HAVE_LIBCLANG
libfyaml_la_CPPFLAGS += $(LIBCLANG_CPPFLAGS)
libfyaml_la_CFLAGS += $(LIBCLANG_CFLAGS)
libfyaml_la_LDFLAGS += $(LIBCLANG_LDFLAGS) $(LIBCLANG_LIBS)
endif

bin_PROGRAMS =
noinst_PROGRAMS =

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
			   -I$(top_srcdir)/src/util
libfyaml_parser_LDADD = $(AM_LDADD) $(LIBYAML_LIBS) libfyaml.la
libfyaml_parser_CFLAGS = $(AM_CFLAGS) $(LIBYAML_CFLAGS)

libfyaml_parser_LDFLAGS = $(AM_LDFLAGS) -static
endif
endif

bin_PROGRAMS += fy-tool

fy_tool_SOURCES = \
	tool/fy-tool.c \
	valgrind/fy-valgrind.h

fy_tool_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/valgrind
fy_tool_LDADD = $(AM_LDADD) libfyaml.la
fy_tool_CFLAGS = $(AM_CFLAGS)
fy_tool_LDFLAGS = $(AM_LDFLAGS)

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
