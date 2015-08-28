#!/bin/bash

# Modify your ENV VARs so that baler programs can run
# Don't forget to add dependencies (like sos3) into the library path

PREFIX=/opt/ovis
export PATH="$PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:/opt/sos3/lib:$LD_LIBRARY_PATH"
export ZAP_LIBPATH=$PREFIX/lib
export PYTHONPATH=$PREFIX/lib/python2.7/site-packages
