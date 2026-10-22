#!/bin/sh
# Convenience wrapper: run the test suite.
cd "$(dirname "$0")"
export PATH=/usr/bin:$PATH
sh test/run_tests.sh
