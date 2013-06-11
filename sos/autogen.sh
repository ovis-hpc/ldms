#! /bin/sh
[ -d config ] || mkdir config
autoreconf --force --install -I config -I m4
