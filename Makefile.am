BUILT_SOURCES = .version
.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

# files to keep in the distribution (in case you want to boostrap)
EXTRA_DIST=bootstrap.sh \
	   build-aux/shave.in build-aux/shave-libtool.in \
	   build-aux/git-version-gen \
	   README.md \
	   debian \
	   Dockerfile Dockerfile-build-deb \
	   Dockerfile.alpine Dockerfile.centos

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
	.version .tarball-version \
	debian/changelog debian/control debian/copyright \
	debian/libfyaml-dev.install debian/libfyaml-utils.install debian/libfyaml-$(DEBVERSION).install

clean-local:

SUBDIRS = src test doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libfyaml.pc

if HAVE_DPKG
DEB_ARTIFACTS= \
	$(PACKAGE_NAME)-$(DEBVERSION)_$(DEBVERSION)_$(DPKG_ARCH).deb \
	$(PACKAGE_NAME)-dev_$(DEBVERSION)_$(DPKG_ARCH).deb \
	$(PACKAGE_NAME)-utils_$(DEBVERSION)_$(DPKG_ARCH).deb \
	$(PACKAGE_NAME)_$(DEBVERSION).dsc \
	$(PACKAGE_NAME)_$(DEBVERSION).tar.xz \
	$(PACKAGE_NAME)_$(DEBVERSION)_$(DPKG_ARCH).build \
	$(PACKAGE_NAME)_$(DEBVERSION)_$(DPKG_ARCH).changes
endif

maintainer-clean-local:
	@rm -rf install sphinx
if HAVE_DPKG
	@rm -rf $(DEB_ARTIFACTS)
	@rm -rf $(PACKAGE_NAME)_$(DEBVERSION).orig.tar.gz
	@rm -rf $(PACKAGE_NAME)-$(VERSION).tar.gz
endif
	@rm -rf install artifacts

# extra file to put in the distribution
EXTRA_DIST += \
	scripts/create-virtual-env \
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

if HAVE_DPKG

# note the package will not be signed
# deb: dist
#	@cp $(PACKAGE_NAME)-$(VERSION).tar.gz $(PACKAGE_NAME)_$(DEBVERSION).orig.tar.gz
#	@rm -rf $(PACKAGE_NAME)-$(VERSION)
#	@tar zxf $(PACKAGE_NAME)-$(VERSION).tar.gz
#	@(cd $(PACKAGE_NAME)-$(VERSION) && debuild -us -uc)

deb: $(DEB_ARTIFACTS)
	@mkdir -p artifacts && mv $(DEB_ARTIFACTS) artifacts/
	@rm -rf $(PACKAGE_NAME)-$(VERSION).tar.gz $(PACKAGE_NAME)-$(VERSION)/

$(PACKAGE_NAME)-$(VERSION).tar.gz: dist

$(PACKAGE_NAME)_$(DEBVERSION).orig.tar.gz: $(PACKAGE_NAME)-$(VERSION).tar.gz
	@cp $< $@

$(PACKAGE_NAME)-$(VERSION)/: $(PACKAGE_NAME)-$(VERSION).tar.gz
	@tar zxf $<

$(DEB_ARTIFACTS): $(PACKAGE_NAME)-$(VERSION)/
	@(cd $< && debuild -us -uc)

endif
