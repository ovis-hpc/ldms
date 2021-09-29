#! /bin/sh

if ! test -f m4/options.m4; then
	echo "Missing file $i. You must use a repo clone and not a github zip download"
	exit 1
fi

# create config dir if not exist
test -d config || mkdir config
if test -d ldms ; then
	autoreconf --force --install -v -I m4
else
	echo "this script for regenerating the ovis ldmsd. Cannot find ldms/."
	exit 1
fi

if test -f .git/hooks/pre-commit.sample; then
	if ! test -f .git/hooks/pre-commit; then
		cp .git/hooks/pre-commit.sample .git/hooks/pre-commit
	fi
fi
