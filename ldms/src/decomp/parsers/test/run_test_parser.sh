#!/bin/sh
# Run test_parser once for each decomposition config parser that was built,
# selecting the parser with LDMSD_DECOMP_PARSER.  Also checks that the
# default parser and an unknown parser name behave as expected.

TEST_PARSER=${TEST_PARSER:-./test_parser}
srcdir=${srcdir:-.}

parsers=$($TEST_PARSER --list) || { echo "FAIL: $TEST_PARSER --list"; exit 1; }
if [ -z "$parsers" ]; then
        echo "FAIL: no parsers were built"
        exit 1
fi

rc=0
for p in $parsers; do
        echo "=== Parser: $p ==="
        if ! LDMSD_DECOMP_PARSER=$p $TEST_PARSER -d "$srcdir" -e "$p"; then
                echo "FAIL: parser $p"
                rc=1
        fi
        echo
done

# With LDMSD_DECOMP_PARSER unset, the first parser listed (in order of
# preference libfyaml, libyaml, jansson) must be used.
first=$(echo $parsers | cut -d' ' -f1)
echo "=== Default parser (expect $first) ==="
if ! (unset LDMSD_DECOMP_PARSER; $TEST_PARSER -d "$srcdir" -e "$first"); then
        echo "FAIL: default parser"
        rc=1
fi
echo

echo "=== Unknown parser ==="
if ! LDMSD_DECOMP_PARSER=no_such_parser $TEST_PARSER -d "$srcdir" -u; then
        echo "FAIL: unknown parser"
        rc=1
fi

exit $rc
