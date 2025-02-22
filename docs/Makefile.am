EXTRA_DIST= \
	conf.py \
	index.rst \
	README.rst \
	requirements.txt \
	ldms_stream.rst

man7_MANS =

if HAVE_RST2MAN

%.man: %.rst
	rst2man $< $@

man7_MANS += ldms_stream.man

endif
