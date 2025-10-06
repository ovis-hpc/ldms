#!/bin/bash
#
# Usage: ./rst2man.sh RST_FILE

based_on_mk() {
	sed -e 's/:ref:`\([^`]*\)<[^`]*>`/\1/g' $@ |
		rst2man |
		sed -e 's/\\\([`'\''\-]\)/\1/g' \
		    -e '/rst2man/d' \
		    -e '/rstR/d' \
		    -e '/. RS/d' \
		    -e '/. RE/d' \
		    -e '/^\s*\.\.\s*$$/d' \
		    -e '/an-margin/d' \
		    -e '/INDENT/d' | man -l -
}

just_no_ref() {
	sed -e 's/:ref:`\([^`]*\)<[^`]*>`/\1/g' $@ |
		rst2man | man -l -
}

just_no_ref $@
