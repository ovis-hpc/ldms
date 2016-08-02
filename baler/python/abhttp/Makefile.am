abhttpdir = $(pythondir)/abhttp
abhttp_PYTHON = __init__.py abhttp.py bclient.py coll.py datatype.py net.py

install-data-hook:
	ln -sf $(abhttpdir)/bclient.py $(DESTDIR)$(bindir)/bclient
	chmod 755 $(DESTDIR)$(abhttpdir)/bclient.py

uninstall-local:
	rm -f $(DESTDIR)$(bindir)/bclient
