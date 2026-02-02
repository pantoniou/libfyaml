BUILT_SOURCES = .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
tarball-version:
	echo $(VERSION) > .tarball-version

# files to keep in the distribution (in case you want to boostrap)
EXTRA_DIST = \
	.dockerignore \
	.github/workflows/ci.yaml \
	.gitignore \
	.libtool-version \
	AUTHORS \
	CHANGELOG.md \
	CMakeLists.txt \
	Dockerfile \
	Dockerfile-build-deb \
	Dockerfile.alpine \
	Dockerfile.centos \
	LICENSE \
	Makefile.am \
	README.md \
	bootstrap.sh \
	build-aux/git-version-gen \
	build-aux/shave-libtool.in \
	build-aux/shave.in \
	cmake/CheckClangBlocks.cmake \
	cmake/CheckLibClang.cmake \
	cmake/MSVCCrossCompile.cmake \
	cmake/add-tap-subtests.cmake \
	cmake/clang-windows-toolchain.cmake \
	cmake/config.h.in \
	cmake/fyaml-config.cmake.in \
	cmake/generate-def.cmake \
	cmake/generate-jsontestsuite-list.sh \
	cmake/generate-testsuite-list.sh \
	cmake/libfyaml-config.cmake.in \
	cmake/libfyaml.pc.in \
	cmake/run-single-tap-test.sh \
	cmake/tags.cmake \
	examples/CMakeLists.txt \
	examples/README.md \
	examples/basic-parsing.c \
	examples/build-from-scratch.c \
	examples/config.yaml \
	examples/document-manipulation.c \
	examples/event-streaming.c \
	examples/invoice.yaml \
	examples/path-queries.c \
	examples/quick-start.c \
	libfyaml.pc.in \
	m4/ax_check_compile_flag.m4 \
	m4/ax_check_define.m4 \
	m4/ax_check_enable_debug.m4 \
	m4/ax_check_flag.m4 \
	m4/ax_cxx_compile_stdcxx.m4 \
	m4/ax_cxx_compile_stdcxx_11.m4 \
	m4/ax_define_dir.m4 \
	m4/ax_is_release.m4 \
	m4/ax_pthread.m4 \
	m4/ax_tls.m4 \
	m4/shave.m4

MAINTAINERCLEANFILES = \
        Makefile.in src/Makefile.in config.h.in configure \
        install-sh ltmain.sh missing mkinstalldirs \
        config.log config.status config.guess config.sub config.h \
        build-stamp compile depcomp acinclude.m4 aclocal.m4 \
	stamp-h1 \
	ar-lib m4/libtool.m4 m4/ltoptions.m4 m4/ltsugar.m4 m4/ltversion.m4 \
	m4/lt~obsolete.m4 src/mock/.dirstamp src/mock/Makefile.in \
	config.h.in~ \
	test-driver test/Makefile.in \
	build-aux/ar-lib build-aux/compile build-aux/config.guess \
	build-aux/config.sub build-aux/depcomp build-aux/install-sh \
	build-aux/ltmain.sh build-aux/missing build-aux/tap-driver.sh

DISTCLEANFILES = \
	.version

clean-local:

SUBDIRS = src test doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfyaml.pc

maintainer-clean-local:
	@rm -rf install sphinx
	@rm -rf install artifacts

# extra file to put in the distribution
EXTRA_DIST += \
	scripts/create-virtual-env \
	scripts/get-gpg-key-id-by-email.sh \
	scripts/install-linuxdoc.sh \
	scripts/run-check-errors.sh \
	scripts/run-compare-dump.sh \
	scripts/run-compare-examples.sh \
	scripts/run-compare-parse.sh \
	scripts/run-compare-scan.sh \
	scripts/run-compare-testsuite.sh \
	scripts/run-emit-check.sh \
	scripts/run-kcachegrind.sh \
	scripts/run-list-testsuite.sh \
	scripts/run-massif.sh \
	scripts/run-test.sh \
	scripts/run-valgrind.sh \
	scripts/show-desc.sh

if HAVE_DOCKER
docker: Dockerfile
	@DOCKER@ build -t libfyaml:$(VERSION) $(top_srcdir)
endif
