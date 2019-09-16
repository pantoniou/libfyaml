AC_PREREQ(2.61)

AC_INIT([libfyaml],
        m4_esyscmd([build-aux/git-version-gen .tarball-version]),
	[pantelis.antoniou@konsulko.com])

AC_CONFIG_SRCDIR([src/lib/fy-parse.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([foreign 1.8.5 -Wall subdir-objects ])
AM_EXTRA_RECURSIVE_TARGETS([doc-help doc-html doc-latexpdf doc-man doc-clean doc-markdown])

m4_define(fyaml_major, `echo $VERSION | cut -d. -f1`)
m4_define(fyaml_minor, `echo $VERSION | cut -d. -f2`)
m4_define(fyaml_patch, `echo $VERSION | cut -d. -f3- | cut -d- -f1`)
m4_define(fyaml_extra, `echo $VERSION | cut -d. -f3- | cut -d- -f2-`)

AC_SUBST(MAJOR, fyaml_major)
AC_SUBST(MINOR, fyaml_minor)
AC_SUBST(PATCH, fyaml_patch)
AC_SUBST(EXTRA, fyaml_extra)

AX_CHECK_ENABLE_DEBUG()

AC_PROG_MKDIR_P
AC_PROG_CC
AC_PROG_CC_C99
AM_PROG_CC_C_O
AC_PROG_GCC_TRADITIONAL
AC_USE_SYSTEM_EXTENSIONS
AM_PROG_AR
AC_PROG_CXX
AC_PROG_AWK
AC_PROG_LN_S

AX_PTHREAD

AC_PATH_PROG([M4], [m4 gm4], [no])
if test "x$M4" = xno ; then
   AC_MSG_ERROR([m4 missing])
fi

AC_SUBST(ACLOCAL_AMFLAGS, "-I m4")

# pkg-config
PKG_PROG_PKG_CONFIG

AC_LTDL_ENABLE_INSTALL
AC_LIBLTDL_INSTALLABLE
AC_LIBTOOL_DLOPEN
AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL
AC_SUBST(LTDLINCL)
AC_SUBST(LIBLTDL)
AC_CONFIG_SUBDIRS(libltdl)

AC_HEADER_STDC
AC_C_CONST
AC_C_BIGENDIAN
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_TYPES(ssize_t, , [AC_DEFINE([ssize_t], [signed long],
    [Define ssize_t if it is not done by the standard libs.])])
AC_TYPE_OFF_T
AC_TYPE_SIGNAL
AC_TYPE_UID_T
AC_CHECK_DECLS(environ)

AX_APPEND_COMPILE_FLAGS(
    [-Wall -Wsign-compare],
    [], [-pedantic -Werror])

if test "x$ax_enable_debug" != "xyes" ; then
	AX_APPEND_COMPILE_FLAGS(
		[-O2],
		[], [])
fi

# include -lm in the link
AC_SEARCH_LIBS([llrintf], [m], [], [AC_MSG_ERROR([unable to find the llrintf() function])])

PKG_CHECK_MODULES(LIBYAML, [ yaml-0.1 ], HAVE_LIBYAML=1, HAVE_LIBYAML=0)

# update with pkg-config's flags
if test "x$HAVE_LIBYAML" != "x1" ; then
	AC_MSG_WARN([failed to find libyaml; compatibility disabled])
fi

AC_SUBST(HAVE_LIBYAML)
AC_SUBST(LIBYAML_CFLAGS)
AC_SUBST(LIBYAML_LIBS)
AC_DEFINE_UNQUOTED([HAVE_LIBYAML], [$HAVE_LIBYAML], [Define to 1 if you have libyaml available])
AM_CONDITIONAL([HAVE_LIBYAML], [ test x$HAVE_LIBYAML = x1 ])

PKG_CHECK_MODULES(CHECK, [ check ], HAVE_CHECK=1, HAVE_CHECK=0)

AC_SUBST(HAVE_CHECK)
AC_SUBST(CHECK_CFLAGS)
AC_SUBST(CHECK_LDFLAGS)
AC_SUBST(CHECK_LIBS)
AC_DEFINE_UNQUOTED([HAVE_CHECK], [$HAVE_CHECK], [Define to 1 if you have check available])
AM_CONDITIONAL([HAVE_CHECK], [ test x$HAVE_CHECK = x1 ])

# check for sphinx
AC_PATH_PROG([SPHINX], [sphinx-build])
if test "x$SPHINX"  != "x" ; then
	HAVE_SPHINX=1
else
	HAVE_SPHINX=0
fi
AC_DEFINE_UNQUOTED([HAVE_SPHINX], [$HAVE_SPHINX], [Define to 1 if you have sphinx available])
AM_CONDITIONAL([HAVE_SPHINX], [ test x$HAVE_SPHINX = x1 ])

# check for git
AC_PATH_PROG([GIT], [git])
if test "x$GIT"  != "x" ; then
	HAVE_GIT=1
else
	HAVE_GIT=0
fi
AC_DEFINE_UNQUOTED([HAVE_GIT], [$HAVE_GIT], [Define to 1 if you have git available])
AM_CONDITIONAL([HAVE_GIT], [ test x$HAVE_GIT = x1 ])

AC_ARG_VAR(TESTSUITEURL, [Testsuite git repo URL (default: https://github.com/yaml/yaml-test-suite)])

if test "x$TESTSUITEURL" == "x" ; then
	TESTSUITEURL="https://github.com/yaml/yaml-test-suite"
fi

# check for docker
AC_PATH_PROG([DOCKER], [docker])
if test "x$DOCKER"  != "x" ; then
	HAVE_DOCKER=1
else
	HAVE_DOCKER=0
fi
AM_CONDITIONAL([HAVE_DOCKER], [ test x$HAVE_DOCKER = x1 ])

# Shave by default
SHAVE_INIT([build-aux], [enable])

AC_CONFIG_FILES([
        build-aux/shave
        build-aux/shave-libtool
	Makefile
	src/Makefile
	test/Makefile
	doc/Makefile
	libfyaml.pc
])

AC_REQUIRE_AUX_FILE([tap-driver.sh])

AC_OUTPUT

echo "
 ---{ $PACKAGE_NAME $VERSION }---

    VERSION:              ${VERSION}
    MAJOR.MINOR:          ${MAJOR}.${MINOR}
    PATCH:                ${PATCH}
    EXTRA:                ${EXTRA}
    prefix:               ${prefix}
    C compiler:           ${CC}
    CFLAGS:               ${CFLAGS}
    Linker:               ${LD}
    LDFLAGS:              ${LDFLAGS}
    LIBS:                 ${LIBS}
    CHECK:                ${HAVE_CHECK}
    SPHINX:               $SPHINX
    GIT:                  $GIT
    DOCKER:               $DOCKER
    TESTSUITEURL:         $TESTSUITEURL

"
