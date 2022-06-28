export TEST_NAME=$(basename ${TEST_FILE} | sed 's|.sh||')
TEST_CONF=${TEST_DIR}/${TEST_NAME}.conf
TEST_LOG_OUT=${TEST_DIR}/${TEST_NAME}.out
TEST_LOG_ERR=${TEST_DIR}/${TEST_NAME}.err
TEST_LOG_EXPECT=${TEST_DIR}/${TEST_NAME}.expect
TEST_BIN=${TEST_DIR}/ldms_geopm_sampler_test


ldms_geopm_sampler_test_run()
{
    ${TEST_WRAPPER} ${TEST_BIN} ${TEST_CONF} \
        1> ${TEST_LOG_OUT} \
        2> ${TEST_LOG_ERR}
    TEST_EXIT=$?
}

ldms_geopm_sampler_test_check_err()
{
    sed -e 's|\(ldms_geopm_sampler\.c\:\)[0-9]*|\1XXXX|g' \
        -e 's|\(PlatformIO\.cpp\:\)[0-9]*|\1XXXX|g' \
        -e "s|\(Error in opening file '\).*\(ldms_geopm_sampler_test_noent.conf'\)|\1\2|" \
        -i ${TEST_LOG_ERR}
    if ! diff ${TEST_LOG_EXPECT} ${TEST_LOG_ERR}; then
        cat ${TEST_LOG_ERR} 1>&2
        echo FAIL 1>&2
        exit -1
    fi
    if [[ $TEST_EXIT -eq 0 ]]; then
	echo "Error: Expected non-zero return code for negative test" 1>&2
        echo FAIL 1>&2
        exit -1
    fi
}

ldms_geopm_sampler_test_check_out()
{
    sed -e 's|\(geopm_sampler: metric_[0-9] = \)0\.[0-9]*|\1XXXX|' \
        -e "s|\(Success opening configuration file: '\).*\(ldms_geopm_sampler_test.conf'\)|\1\2|" \
        -i ${TEST_LOG_OUT}
    if [ -s ${TEST_LOG_ERR} ]; then
        cat ${TEST_LOG_ERR} 1>&2
        echo FAIL 1>&2
        exit -1
    fi
    if ! diff ${TEST_LOG_EXPECT} ${TEST_LOG_OUT}; then
        cat ${TEST_LOG_OUT} 1>&2
        exit -1
    fi
    if [[ $TEST_EXIT -ne 0 ]]; then
	echo "Error: Expected zero return code" 1>&2
        echo FAIL 1>&2
        exit -1
    fi
}

ldms_geopm_sampler_test_pass()
{
    rm ${TEST_LOG_OUT} ${TEST_LOG_ERR}
    echo SUCCESS
    exit 0
}
