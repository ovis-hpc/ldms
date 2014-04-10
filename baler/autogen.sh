#!/bin/bash

set -x;
[[ -d m4 ]] || mkdir m4 &&
[[ -d config ]] || mkdir config &&
#autoreconf --force --install -v
autoreconf --force --install
