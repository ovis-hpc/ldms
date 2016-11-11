#!/bin/sh
# script to create exit code list man page from source code.
# arguments are list of files calling cleanup().
cat << EOF
.\" Manpage for ldmsd exit codes
.\" Contact ovis-help@ca.sandia.gov to correct errors or typos.
.TH man 8 "15 Nov 2016" "v3" "ldmsd exit codes"

.SH Exit codes

.PP
Non-zero exit codes from ldmsd are as listed below.
This list is machine generated from source code and may vary with
any release.
.RS
EOF
grep cleanup.[0-9] $* | sed -e 's/.*://g' -e 's/^$//g' -e 's/\t*//g' -e 's/.*NULL.*//' -e 's/cleanup.//g' -e 's/,//g' -e 's/".;//' -e 's/"//g' | grep '[0-9]' |sort -n | sed -e 's/^/.TP\n&/'
cat << EOF
.RE
.SH SEE ALSO
ldmsd(8)
EOF
