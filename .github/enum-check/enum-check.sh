#!/bin/bash
set -e
set -x
D=$(dirname $0)
pushd ${D}
for V in 4.3.{3..11}; do
  echo "getting enums from ${V}"
  ./enumsym.py /root/ldms-${V}/ -e ovis-${V}-enum.list > /test/${V}.enum
  ./enumsym.py ../../ -e ovis-${V}-enum.list > /test/HEAD-${V}.enum
done
for V in 4.3.{3..11}; do
  echo "comparing enums from ${V} with HEAD"
  diff -u /test/${V}.enum /test/HEAD-${V}.enum > /test/enum-${V}.diff
done
echo "DONE!"
