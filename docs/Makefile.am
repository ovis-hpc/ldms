EXTRA_DIST= \
	conf.py \
	index.rst \
	README.rst \
	requirements.txt

man7_MANS =

if HAVE_RST2MAN

# Find all .rst files under docs/man, excluding index.rst in any subdirectory
RST_FILES = $(shell find $(srcdir)/man -type f -name '*.rst' ! -name 'index.rst')

# Automatically generate corresponding .man filenames
MAN_FILES = $(RST_FILES:$(srcdir)/man/%.rst=$(builddir)/man/%.man)

# Rule to convert .rst to .man
$(builddir)/man/%.man: man/%.rst
	mkdir -p $(dir $@)
	rst2man $< $@

# Add the .rst files to EXTRA_DIST and the generated .man files to man7_MANS
EXTRA_DIST += $(RST_FILES)
man7_MANS += $(MAN_FILES)

endif
