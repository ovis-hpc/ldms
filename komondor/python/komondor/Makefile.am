pkgpython_PYTHON = komondor.py __init__.py

install-data-hook:
	chmod 755 $(DESTDIR)$(pkgpythondir)/komondor.py
	ln -f -s $(DESTDIR)$(pkgpythondir)/komondor.py $(DESTDIR)$(sbindir)/

uninstall-hook:
	rm $(DESTDIR)$(sbindir)/komondor.py
