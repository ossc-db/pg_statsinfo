#
# pg_statsinfo: Makefile
#
#    Copyright (c) 2010-2012, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
#
SUBDIRS = bin lib

all install installdirs uninstall distprep clean distclean maintainer-clean:
	@for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || exit; \
	done

# We'd like check operations to run all the subtests before failing.
check installcheck:
	@CHECKERR=0; for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@ || CHECKERR=$$?; \
	done; \
	exit $$CHECKERR
