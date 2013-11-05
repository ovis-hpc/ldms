if HAVE_DOXYGEN

.PHONY: doc docs
all: doc
doc: docs

docs:
	mkdir -p html latex man
	@DOXYGEN@

install-data-local:
	mkdir -p $(docdir) $(mandir) 
	cp -r html latex $(docdir)/
	cp -r man/* $(mandir)/

uninstall-local:
	rm -rf $(mandir)
	rm -rf $(docdir)

clean-local:
	rm -rf html latex man

else

doc:
	@echo "doxygen not found!!!"
	@exit -1

endif
