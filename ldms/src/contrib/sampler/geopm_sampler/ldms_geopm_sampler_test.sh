#!/bin/bash

TEST_FILE=$0
TEST_DIR=$(realpath $(dirname ${TEST_FILE}))
source ${TEST_DIR}/ldms_geopm_sampler_test_helper.sh

ldms_geopm_sampler_test_run
ldms_geopm_sampler_test_check_out
ldms_geopm_sampler_test_pass
