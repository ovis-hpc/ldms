#!/bin/bash
echo "[>>] Clean source tree"
make uninstall
make distclean
make clean
make maintainer-clean
echo "[>>] Clean isn't clean. Remove any file with an .in file"
find ./ -name "*.in" |(while read FOO; do base="$(echo $FOO |sed 's/\.in$//g')"; echo "Remove $base"; rm -rf  "$base"; done; )
find ./ -name "*.cache" -delete
