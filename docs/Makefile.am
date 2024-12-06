EXTRA_DIST= \
	conf.py \
	index.rst \
	README.rst \
	requirements.txt

man7_MANS =

if HAVE_RST2MAN

%.man: %.rst
	rst2man $< $@

endif
