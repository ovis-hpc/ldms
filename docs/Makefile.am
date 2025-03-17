EXTRA_DIST = conf.py index.rst README.rst requirements.txt

RST_SRC = $(top_srcdir)/ldms
RST_DST = $(srcdir)/rst_man

copy_rst:
	@mkdir -p $(RST_DST)
	@cd $(RST_SRC) && find . -name "*.rst" -exec cp --parents {} $(abspath $(RST_DST)) \;


all: copy_rst
