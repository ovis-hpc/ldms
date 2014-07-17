ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src scripts swig
if ENABLE_ETC
SUBDIRS += etc
endif

install-exec-local:
	COMMIT=`git rev-parse HEAD`; \
	DIRTY=`git status -uno -s | wc -l`; \
	if [[ $$DIRTY != 0 ]]; then \
		COMMIT="$$COMMIT-dirty" ; \
	fi ; \
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/baler-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/baler-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/baler-commit-id ;
