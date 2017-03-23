#!/bin/bash
echo "trnsfp_add name=meminfo-rate plugin=trnsf_rate mode=published input_schema=meminfo output_schema=meminfo-rate test=1"
echo "trnsfp_start name=meminfo-rate"
echo "trnsfp_add name=vmstat-rate plugin=trnsf_rate mode=published input_schema=vmstat output_schema=vmstat-rate test=1"
echo "trnsfp_start name=vmstat-rate"
echo "trnsfp_add name=msr_interlagos-rate plugin=trnsf_rate mode=published input_schema=msr_interlagos_delta_sum_vector metrics=Ctr4_c,Ctr5_c test=1"
echo "trnsfp_start name=msr_interlagos-rate"

