#!/bin/sh
set -e

PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="${PREFIX}/bin"
APP_DIR="${APP_DIR:-/Applications}"

echo "=== BareMRI uninstall ==="

if [ -f "${BIN_DIR}/baremri" ]; then
    rm -f "${BIN_DIR}/baremri"
    echo "Removed  ${BIN_DIR}/baremri"
else
    echo "Skip     ${BIN_DIR}/baremri  (not found)"
fi

if [ -d "${APP_DIR}/BareMRI.app" ]; then
    rm -rf "${APP_DIR}/BareMRI.app"
    echo "Removed  ${APP_DIR}/BareMRI.app"
else
    echo "Skip     ${APP_DIR}/BareMRI.app  (not found)"
fi

echo "Done."
