#!/bin/bash
if test -d $1 ; then
	exit
fi
if test -z $2; then
	top=/etc
else
	top=$2
fi
if ! test -d DELTA; then
	mkdir DELTA
fi
cp --parents /etc/$1 DELTA
