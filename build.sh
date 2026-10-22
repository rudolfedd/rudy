#!/bin/sh
# Convenience wrapper: build rudy (works from any shell).
set -e
cd "$(dirname "$0")"
export PATH=/usr/bin:$PATH
make clean >/dev/null 2>&1 || true
make
