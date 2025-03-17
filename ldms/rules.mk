# 1. Fix duplicate titles when converting to man
# 	- Remove "NAME" and "=====" underneath
# 	- Add :title: to first sentence after :Date:
# 2. Remove :ref:`<>` links in .rst to avoid rs2man "Unkown interpretd text role "ref"" errors
# 	Ex: :ref:`ldms_quickstart(7) <ldms_quickstart>` --> ldms_quickstart(7).
# 3. Remove generated "/" after every special character.
# 	Ex: \fB\-a,\fP\fI\-\-default_auth\fP --> \fB-a,\fP\fI--default_auth\fP
# 4. Remove lines with unnecessary rst2man output.
# 	Ex: .de1 rstReportMargin, \\$1 \\n[an-margin], level \\n[rst2man-indent-level], .de1 INDENT,. RS \\$1, .de UNINDENT
%.man: %.rst
	@echo "Generating $@..."
	@mkdir -p $(dir $@)
	@sed -e '/^NAME$$/ {N; d;}' $< | \
	sed -e '/^:Date:/ {n; :loop; /^[[:space:]]*$$/ {n; b loop}; s/^/:title: /; :join; N; s/\n/ /g;}' | \
	sed -e 's/:ref:`\([^`]*\)<[^`]*>`/\1/g' | \
	rst2man | \
	sed -e 's/\\\([`'\''\-]\)/\1/g' \
		-e '/rst2man/d' \
		-e '/rstR/d' \
		-e '/. RS/d' \
		-e '/. RE/d' \
		-e '/^\s*\.\.\s*$$/d' \
		-e '/an-margin/d' \
		-e '/INDENT/d' > $@
