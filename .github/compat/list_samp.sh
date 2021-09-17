#!/bin/bash

# reset ENV first
export ZAP_LIBPATH=
export LDMSD_PLUGIN_LIBPATH=
export LD_LIBRARY_PATH=
export PYTHONPATH=
export PATH=/usr/local/bin:/bin:/usr/bin:/usr/local/sbin:/sbin:/usr/sbin

PREFIX=/opt/ovis-4

_add() {
	local NAME=$1
	local VAL=$2
	[[ *:${!NAME}:* == *:${VAL}:* ]] || \
		eval "export ${NAME}='${!NAME}:${VAL}'"
}

_add PATH ${PREFIX}/bin
_add PATH ${PREFIX}/sbin

export LDMS_AUTH_FILE=${PREFIX}/etc/ldms/ldmsauth.conf

export LDMSD_PLUGIN_LIBPATH=${PREFIX}/lib/ovis-ldms
export ZAP_LIBPATH=${PREFIX}/lib/ovis-ldms

# for uGNI transport
# export ZAP_UGNI_PTAG=<your ptag>
# export ZAP_UGNI_COOKIE=<your cookie>
#
# for Python
_add PYTHONPATH ${PREFIX}/lib/python3.6/site-packages

python3 list_samp.py
