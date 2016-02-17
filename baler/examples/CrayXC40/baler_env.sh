#!/bin/bash

# Modify your ENV VARs so that baler programs can run

PREFIX=/XXX/Build/ovis
export PATH="$PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:$LD_LIBRARY_PATH"
export ZAP_LIBPATH=$PREFIX/lib/ovis-lib

