#!/bin/sh
set -e

# ---- config ---------------------------------------------------------
PREFIX="${PREFIX:-/usr/local}"
BIN_DIR="${PREFIX}/bin"
APP_DIR="${APP_DIR:-/Applications}"
APP_EXEC="${APP_DIR}/BareMRI.app/Contents/MacOS/baremri"

# ---- build ----------------------------------------------------------
echo "=== BareMRI install ==="
echo "→ Building..."
make clean >/dev/null 2>&1
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" VERBOSE=l0 EXPERIMENT="${EXPERIMENT:-}"

# ---- .app bundle ----------------------------------------------------
if [ ! -d "build/BareMRI.app" ]; then
    echo "→ Building .app bundle ..."
    make app VERBOSE=l0 >/dev/null 2>&1
fi

echo "→ Installing .app to ${APP_DIR} ..."
rm -rf "${APP_DIR}/BareMRI.app"
cp -R build/BareMRI.app "${APP_DIR}/"

# ---- CLI wrapper (launches through .app so Dock icon shows) ---------
echo "→ Installing CLI wrapper to ${BIN_DIR} ..."
mkdir -p "${BIN_DIR}"
cat > "${BIN_DIR}/baremri" << EOF
#!/bin/sh
exec "${APP_EXEC}" "\$@"
EOF
chmod 755 "${BIN_DIR}/baremri"

# ---- done -----------------------------------------------------------
echo ""
echo "Installed:"
echo "  CLI  → ${BIN_DIR}/baremri"
echo "  App  → ${APP_DIR}/BareMRI.app"
echo ""
echo "Run from terminal:  baremri <file.nii.gz>"
echo "                    (launches through .app — Dock icon works!)"
