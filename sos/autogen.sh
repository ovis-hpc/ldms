#! /bin/sh
[ -d config ] || mkdir config
autoreconf -v --force --install -I config -I m4
