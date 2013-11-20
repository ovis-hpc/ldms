#! /bin/sh

# create config dir if not exist
test -d config || mkdir config
test -d m4 || mkdir m4

echo "Running autoreconf"
echo "------------------"
autoreconf --force --install
