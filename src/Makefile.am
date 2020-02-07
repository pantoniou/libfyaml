AM_CPPFLAGS = \
        -I$(top_srcdir)/src \
        -I$(top_srcdir)/include
AM_CFLAGS =

lib_LTLIBRARIES = libfyaml-@MAJOR@.@MINOR@.la
libfyaml_@MAJOR@_@MINOR@_la_SOURCES = \
        lib/fy-parse.c lib/fy-parse.h \
	lib/fy-utf8.c lib/fy-utf8.h \
	lib/fy-types.c lib/fy-types.h \
	lib/fy-list.h \
	lib/fy-typelist.h \
	lib/fy-diag.c lib/fy-diag.h \
	lib/fy-dump.c lib/fy-dump.h \
	lib/fy-atom.c lib/fy-atom.h \
	lib/fy-ctype.c lib/fy-ctype.h \
	lib/fy-token.c lib/fy-token.h \
	lib/fy-input.c lib/fy-input.h \
	lib/fy-docstate.c lib/fy-docstate.h \
	lib/fy-doc.c lib/fy-doc.h \
	lib/fy-emit.c lib/fy-emit.h \
	lib/fy-utils.c lib/fy-utils.h \
	lib/fy-event.h \
	xxhash/xxhash.c xxhash/xxhash.h \
	lib/fy-accel.c lib/fy-accel.h

libfyaml_@MAJOR@_@MINOR@_la_CPPFLAGS = $(AM_CPPFLAGS) \
				       -I$(top_srcdir)/src/lib \
				       -I$(top_srcdir)/src/xxhash
libfyaml_@MAJOR@_@MINOR@_la_CFLAGS = $(AM_CFLAGS)
libfyaml_@MAJOR@_@MINOR@_la_LDFLAGS = $(AM_LDFLAGS) $(AM_LIBLDFLAGS) \
		      $(VERSIONING_LDFLAGS) \
		      -version-info 0:0:0

bin_PROGRAMS =
noinst_PROGRAMS =

if HAVE_LIBYAML

noinst_PROGRAMS += libfyaml-parser

libfyaml_parser_SOURCES = \
	internal/libfyaml-parser.c \
	valgrind/fy-valgrind.h

libfyaml_parser_CPPFLAGS = $(AM_CPPFLAGS) \
			   -I$(top_srcdir)/src/valgrind \
			   -I$(top_srcdir)/src/lib \
			   -I$(top_srcdir)/src/xxhash
libfyaml_parser_LDADD = $(AM_LDADD) $(LIBYAML_LIBS) libfyaml-@MAJOR@.@MINOR@.la
libfyaml_parser_CFLAGS = $(AM_CFLAGS) $(LIBYAML_CFLAGS)

libfyaml_parser_LDFLAGS = $(AM_LDFLAGS)
endif

bin_PROGRAMS += fy-tool

fy_tool_SOURCES = \
	tool/fy-tool.c \
	valgrind/fy-valgrind.h

fy_tool_CPPFLAGS = $(AM_CPPFLAGS) -I$(top_srcdir)/src/valgrind
fy_tool_LDADD = $(AM_LDADD) libfyaml-@MAJOR@.@MINOR@.la
fy_tool_CFLAGS = $(AM_CFLAGS)
fy_tool_LDFLAGS = $(AM_LDFLAGS)

include_HEADERS = \
        $(top_srcdir)/include/libfyaml.h

install-exec-hook:
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-dump)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-filter)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-testsuite)
	(cd "$(DESTDIR)$(bindir)" && $(LN_S) -f fy-tool fy-join)

uninstall-hook:
	(cd "$(DESTDIR)$(bindir)" && rm -f fy-dump fy-filter fy-testsuite fy-join)
