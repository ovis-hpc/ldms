#!/bin/bash

HOSTFILE=/tmp/opt/ovis/trivial/sampler_hosts

test -s $HOSTFILE || exit 1

while read hostname
do
	echo "prdcr_add name=$hostname host=$hostname port=411 xprt=ugni type=active interval=20000000" 
	echo "prdcr_start name=$hostname" 
done < $HOSTFILE
	echo "updtr_add name=foo interval=1000000 offset=100000" 
	echo "updtr_prdcr_add name=foo regex=nid*" 
	echo "updtr_start name=foo" 

