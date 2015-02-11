#!/bin/bash

cc -o getmyresinfo getmyresinfo.c -g -Wall -I/opt/cray-hss-devel/default/include/ -I/opt/cray/gni-headers/default/include/ -I/opt/cray/ugni/5.0-1.0402.7551.1.10.gem/include/ -L/opt/cray/ugni/5.0-1.0402.7551.1.10.gem/lib64 -lugni