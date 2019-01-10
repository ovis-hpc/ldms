#! /bin/sh

for i in ldms/m4/ac_pkg_swig.m4  lib/m4/ac_pkg_swig.m4 m4/ac_pkg_swig.m4; do
	if ! test -f $i; then
		echo "Missing file $i. You must use a repo clone and not a github zip download"
		exit 1
	fi
done

# create config dir if not exist
if test -d ldms ; then
 autoreconf --force --install -v -I m4
else
echo "this script for regenerating build system across all ovis packages. can't find ldms/."
 exit 1
fi
