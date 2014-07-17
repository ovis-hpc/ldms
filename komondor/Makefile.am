ACLOCAL_AMFLAGS = -I m4
SUBDIRS = src python
if ENABLE_ETC
SUBDIRS += etc
endif

install-exec-local:
	COMMIT=`git rev-parse HEAD`; \
	DIRTY=`git status -uno -s | wc -l`; \
	if [[ $$DIRTY != 0 ]]; then \
		COMMIT="$$COMMIT-dirty" ; \
	fi ; \
	echo '#!/bin/bash' > $(DESTDIR)/$(bindir)/komondor-commit-id ; \
	echo echo $$COMMIT >> $(DESTDIR)/$(bindir)/komondor-commit-id ; \
	chmod 555 $(DESTDIR)/$(bindir)/komondor-commit-id ;

