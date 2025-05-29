# 1. Remove :ref:`<>` links in .rst to avoid rs2man "Unkown interpretd text role "ref"" errors
# 	Ex: :ref:`ldms_quickstart(7) <ldms_quickstart>` --> ldms_quickstart(7).
# 2. Remove generated "/" after every special character.
# 	Ex: \fB\-a,\fP\fI\-\-default_auth\fP --> \fB-a,\fP\fI--default_auth\fP
# 3. Remove lines with unnecessary rst2man output.
# 	Ex: .de1 rstReportMargin, \\$1 \\n[an-margin], level \\n[rst2man-indent-level], .de1 INDENT,. RS \\$1, .de UNINDENT
# The AM_V_P logic allows the output to be quiet when using "configure --enable-silent-rules"
# or "make V=0" (default behavior), and be verbose when using "configure --disable-silent-rules"
# or "make V=1".
%.man: %.rst
	@set -e; \
	if $(AM_V_P); then set -x; else echo "  RST2MAN  $@"; fi; \
	mkdir -p $(dir $@); \
	sed -e 's/:ref:`\([^`]*\)<[^`]*>`/\1/g' $< | @RST2MAN@ | sed -e 's/\\\([`'\''\-]\)/\1/g' \
		-e '/rst2man/d' \
		-e '/rstR/d' \
		-e '/. RS/d' \
		-e '/. RE/d' \
		-e '/^\s*\.\.\s*$$/d' \
		-e '/an-margin/d' \
		-e '/INDENT/d' > $@
