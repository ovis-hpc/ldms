#!/bin/bash

#CRAY_CFLAGS_ = $(AM_CFLAGS) @RCA_INCDIR_FLAG@ @KRCA_INCDIR_FLAG@ \
#                        @CRAY_HSS_DEVEL_INCDIR_FLAG@ \
#                        -I@CRAY_HSS_DEVEL_INCDIR@/rsms
#CRAY_LDFLAGS_ = $(AM_LDFLAGS) @RCA_LIBDIR_FLAG@ @RCA_LIB64DIR_FLAG@ \
#                        @KRCA_LIBDIR_FLAG@ @KRCA_LIB64DIR_FLAG@ \
#                        @CRAY_HSS_DEVEL_LIBDIR_FLAG@ \
#                        @CRAY_HSS_DEVEL_LIB64DIR_FLAG@ \
#                        -lrca


gcc  -I/opt/cray/rca/default/include -I/opt/cray/krca/default/include -I/opt/cray-hss-devel/default/include -I/opt/cray-hss-devel/default/include/rsms  -L/opt/cray/rca/default/lib64 -L/opt/cray/krca/default/lib64 -L/opt/cray-hss-devel/default/lib -lrca -o rca_test rca_test.c

./rca_test
