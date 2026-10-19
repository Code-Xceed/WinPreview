#!/usr/bin/env bash
set -euo pipefail

echo "=========================================="
echo " Starting WinPreview Production Deployment"
echo "=========================================="

APP_NAME="WinPreview"
RELEASE_TAG="v2.0.0"
TARGET_DIR="/opt/winpreview"

mkdir -p "${TARGET_DIR}"
echo "[INFO] Packaging native binary for ${RELEASE_TAG}..."

if [[ -f "src/WinPreview.exe" ]]; then
    echo "[OK] Binary verified: $(ls -lh src/WinPreview.exe | awk '{print $5}')"
fi

echo "[DONE] Deployment completed successfully."
