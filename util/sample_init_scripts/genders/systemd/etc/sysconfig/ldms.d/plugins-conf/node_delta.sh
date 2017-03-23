#!/bin/bash
echo "trnsfp_add name=meminfo-delta plugin=trnsf_delta mode=published input_schema=meminfo output_schema=meminfo-delta test=1"
echo "trnsfp_start name=meminfo-delta"
echo "trnsfp_add name=vmstat-delta plugin=trnsf_delta mode=published input_schema=vmstat output_schema=vmstat-delta test=1"
echo "trnsfp_start name=vmstat-delta"

