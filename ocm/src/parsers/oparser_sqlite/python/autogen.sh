#!/bin/sh

test -d config || mkdir config
test -d m4 || mkdir m4

set -x
autoreconf --install
