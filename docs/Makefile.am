EXTRA_DIST= \
	conf.py \
	index.rst \
	README.rst \
	requirements.txt

man7_MANS =

if HAVE_RST2MAN

%.man: %.rst
	rst2man $< $@

EXTRA_DIST += ldms_stream.rst ldmsd_decomposition.rst
man7_MANS += ldms_stream.man ldmsd_decomposition.man

endif
