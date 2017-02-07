#!/bin/bash
generate () {
	local PLUGIN=$1
	local PRODUCER=$2
	local HOSTNAME=$3
	local SAMPLE_INTERVAL=$4
	local SAMPLE_OFFSET=$5
	echo "prdcr_add name=${HOSTNAME} type=local"
	echo "prdcr_start name=${HOSTNAME}"

	echo "updtr_add name=local_meminfo interval=$SAMPLE_INTERVAL offset=10000"
	echo "updtr_prdcr_add name=local_meminfo regex=${HOSTNAME}"
	echo "updtr_match_add name=local_meminfo regex=^meminfo$ match=schema"
	echo "updtr_start name=local_meminfo"

	echo "updtr_add name=local_vmstat interval=$SAMPLE_INTERVAL offset=10000"
	echo "updtr_prdcr_add name=local_vmstat regex=${HOSTNAME}"
	echo "updtr_match_add name=local_vmstat regex=^vmstat$ match=schema"
	echo "updtr_start name=local_vmstat"

	echo "updtr_add name=local_interlagos interval=$SAMPLE_INTERVAL offset=10000"
	echo "updtr_prdcr_add name=local_interlagos regex=${HOSTNAME}"
	echo "updtr_match_add name=local_interlagos regex=^msr_interlagos$ match=schema"
	echo "updtr_start name=local_interlagos"

	echo "updtr_add name=local_cray interval=$SAMPLE_INTERVAL offset=10000"
	echo "updtr_prdcr_add name=local_cray regex=${HOSTNAME}"
	echo "updtr_match_add name=local_cray regex=^cray_gemini_r_sampler$ match=schema"
	echo "updtr_start name=local_cray"
}
generate $*
