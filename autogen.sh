#! /bin/sh

# create config dir if not exist
if test -d ldms ; then
 autoreconf --force --install -v -I m4
else
echo "this script for regenerating build system across all ovis packages. can't find ldms/."
 exit 1
fi
