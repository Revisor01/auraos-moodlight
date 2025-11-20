#!/bin/bash
# AuraOS Moodlight - Release Build Script

set -e

VERSION=$(grep 'MOODLIGHT_VERSION' src/config.h | cut -d'"' -f2)
if [ -z "$VERSION" ]; then
    if [ -z "$1" ]; then
        echo "Fehler: Version nicht gefunden"
        echo "Verwendung: ./build-release.sh [VERSION]"
        exit 1
    fi
    VERSION=$1
fi

RELEASE_DIR="releases/v${VERSION}"

echo "========================================"
echo "  AuraOS Moodlight Release Builder"
echo "========================================"
echo ""
echo "Version: ${VERSION}"
echo ""

# Release-Ordner erstellen
echo "[1/5] Erstelle Release-Ordner..."
mkdir -p "$RELEASE_DIR"

# UI-Package erstellen
echo "[2/5] Erstelle UI-Package..."
cd data
tar -czf "UI-${VERSION}-AuraOS.tgz" \
    setup.html \
    mood.html \
    index.html \
    diagnostics.html \
    js/ \
    css/ \
    --exclude="*.tmp.*" \
    --exclude="*.tgz" 2>/dev/null || true

mv "UI-${VERSION}-AuraOS.tgz" "../${RELEASE_DIR}/"
cd ..
echo "   -> UI-Package: $(ls -lh ${RELEASE_DIR}/UI-${VERSION}-AuraOS.tgz | awk '{print $5}')"

# Firmware kompilieren
echo "[3/5] Kompiliere Firmware..."
if command -v pio &> /dev/null; then
    pio run -s 2>&1 | grep -E "(SUCCESS|error|Error)" || true
else
    echo "   -> PlatformIO nicht gefunden, überspringe Build"
    echo "   -> Verwende existierende firmware.bin"
fi

if [ ! -f ".pio/build/esp32dev/firmware.bin" ]; then
    echo "   Fehler: Firmware nicht gefunden!"
    exit 1
fi

# Firmware kopieren
echo "[4/5] Kopiere Firmware..."
cp .pio/build/esp32dev/firmware.bin "${RELEASE_DIR}/firmware-v${VERSION}.bin"
echo "   -> Firmware: $(ls -lh ${RELEASE_DIR}/firmware-v${VERSION}.bin | awk '{print $5}')"

# Checksums erstellen
echo "[5/5] Erstelle Checksums..."
cd "$RELEASE_DIR"
shasum -a 256 *.bin *.tgz > checksums.txt
cd ../..

# README erstellen
cat > "${RELEASE_DIR}/README.md" << 'EOF'
# AuraOS Moodlight vVERSION

## Dateien

- `UI-VERSION-AuraOS.tgz` - Web-Interface Update
- `firmware-vVERSION.bin` - Firmware Binary
- `checksums.txt` - SHA256 Checksums

## Installation (OTA)

### 1. UI Update (ZUERST!)
- Öffne: `http://<moodlight-ip>/diagnostics.html`
- UI Update Sektion → Wähle `UI-VERSION-AuraOS.tgz`
- Upload & warten

### 2. Firmware Update (DANACH!)
- Firmware Update Sektion → Wähle `firmware-vVERSION.bin`
- Upload → Gerät startet neu

## Checksums verifizieren

```bash
shasum -a 256 -c checksums.txt
```
EOF

# VERSION im README ersetzen
sed -i '' "s/VERSION/${VERSION}/g" "${RELEASE_DIR}/README.md" 2>/dev/null || \
    sed -i "s/VERSION/${VERSION}/g" "${RELEASE_DIR}/README.md"

echo ""
echo "========================================"
echo "        Build erfolgreich!"
echo "========================================"
echo ""
echo "Release-Ordner: ${RELEASE_DIR}"
echo ""
ls -lh "$RELEASE_DIR"
echo ""
echo "SHA256 Checksums:"
cat "${RELEASE_DIR}/checksums.txt"
echo ""
echo "Bereit zum Deployment!"
echo ""
