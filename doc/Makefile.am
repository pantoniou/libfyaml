BUILDDIR=_build

if HAVE_SPHINX

MANPAGES1 = $(BUILDDIR)/man/fy-tool.1

doc-%: 
	srctree=@top_srcdir@ @SPHINX@ -M `echo $@ | sed -s 's/^doc-//g'` "@srcdir@" "$(BUILDDIR)"

$(MANPAGES1): doc-man

install-data-hook: $(MANPAGES1)
	$(MKDIR_P) "$(DESTDIR)$(mandir)/man1"
	@for i in "$(MANPAGES1)"; do \
	  $(INSTALL_DATA) $$i "$(DESTDIR)$(mandir)/man1"; \
	done
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-dump.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-filter.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-testsuite.1)
	(cd "$(DESTDIR)$(mandir)/man1" && $(LN_S) -f fy-tool.1 fy-join.1)

uninstall-hook:
	@for i in "$(MANPAGES1)"; do \
	  rm -f "$(DESTDIR)$(mandir)/man1/`basename $$i`"; \
	done
	(cd "$(DESTDIR)$(mandir)/man1" && rm -f fy-dump.1 fy-filter.1 fy-testsuite.1 fy-join.1)

endif

clean-local:
	@rm -rf "$(BUILDDIR)"

maintainer-clean-local:
	@rm -rf Makefile.in

EXTRA_DIST = \
	conf.py \
	index.rst \
	intro.rst \
	libfyaml.rst \
	man/fy-tool.rst
