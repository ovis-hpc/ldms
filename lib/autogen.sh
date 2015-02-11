#!/bin/sh

# create config dir if not exist
test -d config || mkdir config
test -d m4 || mkdir m4

set -x
autoreconf -v --force --install -I m4
