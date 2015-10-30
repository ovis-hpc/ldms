#! /bin/sh

# create config dir if not exist
test -d config || mkdir config

autoreconf --force --install -v -I m4 -W no-syntax

