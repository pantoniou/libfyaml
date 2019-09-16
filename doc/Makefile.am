BUILDDIR=_build

if HAVE_SPHINX

doc-%: 
	srctree=@top_srcdir@ @SPHINX@ -M `echo $@ | sed -s 's/^doc-//g'` "@srcdir@" "$(BUILDDIR)"

endif

clean-local:
	@rm -rf "$(BUILDDIR)"

maintainer-clean-local:
	@rm -rf Makefile.in

EXTRA_DIST = \
	conf.py \
	index.rst
