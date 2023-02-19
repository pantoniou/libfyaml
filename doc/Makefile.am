BUILDDIR=_build

if HAVE_SPHINX

MANPAGES1 = $(BUILDDIR)/man/fy-tool.1
MANPAGES3 = \
	$(BUILDDIR)/man/libfyaml.3 \
	$(BUILDDIR)/man/libfyaml-core.3 \
	$(BUILDDIR)/man/libfyaml-misc.3 \
	$(BUILDDIR)/man/libfyaml-generics.3 \
	$(BUILDDIR)/man/libfyaml-reflection.3

doc-%: 
	srctree=@top_srcdir@ @SPHINX@ -M `echo $@ | sed -s 's/^doc-//g'` "@srcdir@" "$(BUILDDIR)"

# if we have sphinx generate the manpages
$(MANPAGES1) $(MANPAGES3): doc-man

else

# otherwise, install the canned ones
MANPAGES1 = $(srcdir)/canned-man/fy-tool.1
MANPAGES3 = \
	$(srcdir)/canned-man/libfyaml.3 \
	$(srcdir)/canned-man/libfyaml-core.3 \
	$(srcdir)/canned-man/libfyaml-misc.3 \
	$(srcdir)/canned-man/libfyaml-generics.3 \
	$(srcdir)/canned-man/libfyaml-reflection.3

endif

install-data-hook: $(MANPAGES1) $(MANPAGES3)
	$(MKDIR_P) "$(DESTDIR)$(mandir)/man1"
	$(MKDIR_P) "$(DESTDIR)$(mandir)/man3"
	@for i in "$(MANPAGES1)"; do \
	  $(INSTALL_DATA) $$i "$(DESTDIR)$(mandir)/man1"; \
	done
	@for i in $(MANPAGES3); do \
	  $(INSTALL_DATA) $$i "$(DESTDIR)$(mandir)/man3"; \
	done
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-dump.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-filter.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-testsuite.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-join.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-ypath.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-compose.1)

uninstall-hook:
	@for i in "$(MANPAGES1)"; do \
	  rm -f "$(DESTDIR)$(mandir)/man1/`basename $$i`"; \
	done
	@for i in $(MANPAGES3); do \
	  rm -f "$(DESTDIR)$(mandir)/man3/`basename $$i`"; \
	done
	(cd "$(DESTDIR)$(mandir)/man1" && \
		rm -f fy-dump.1 fy-filter.1 fy-testsuite.1 fy-join.1 fy-ypath.1 fy-compose.1)

clean-local:
	@rm -rf "$(BUILDDIR)"

maintainer-clean-local:
	@rm -rf Makefile.in

EXTRA_DIST = \
	canned-man/libfyaml.3 \
	canned-man/libfyaml-core.3 \
	canned-man/libfyaml-misc.3 \
	canned-man/libfyaml-generics.3 \
	canned-man/libfyaml-reflection.3 \
	canned-man/fy-tool.1 \
	conf.py \
	generics-guide.rst \
	index.rst \
	intro.rst \
	libfyaml-core.rst \
	libfyaml-generics.rst \
	libfyaml-misc.rst \
	libfyaml-reflection.rst \
	libfyaml.rst \
	reflection-guide.rst \
	man/fy-tool.rst \
	windows-support.txt
