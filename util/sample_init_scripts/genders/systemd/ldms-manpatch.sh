#!/bin/bash
# This script takes the first argument (full path man page alias) and tries
# to apply the rest of the arguments as sed to it.
# If the file is not found, $file.gz is checked and if present modified
# instead.
# This is handy to update sysconfdir, localstatedir, and prefix(/usr)
# in man pages.
src=$1
shift
case $src in
*.gz)
	if test -f ${src}; then
		out=`mktemp .XXXXXX`
		zcat $src | sed $* > $out && gzip $out && cp $out.gz $src && rm -f $out $out.gz
		exit 0
	fi
	;;
*)
	if test -f $src; then
		sed -i $* $src
		exit $?
	fi
	if test -f ${src}.gz; then
		out=`mktemp .XXXXXX`
		zcat $src.gz | sed $* > $out && gzip $out && cp $out.gz $src.gz && rm -f $out $out.gz
		exit $?
	fi
	;;
esac
echo "cannot patch $src: missing file or unexpected format"
exit 1
