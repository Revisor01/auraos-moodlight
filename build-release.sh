#!/bin/bash
# AuraOS Moodlight - Release Build Script
# Erzeugt versionierte UI-TGZ + Firmware-BIN fuer OTA-Upload
# Verwendung: ./build-release.sh [major|minor]

set -e

# --- Argument-Validierung ---
BUMP_TYPE="${1:-}"
if [[ -z "$BUMP_TYPE" ]] || [[ ! "$BUMP_TYPE" =~ ^(major|minor)$ ]]; then
    echo "Verwendung: ./build-release.sh [major|minor]"
    echo ""
    echo "  major  - Erhoehe Major-Version (z.B. 9.4 -> 10.0)"
    echo "  minor  - Erhoehe Minor-Version (z.B. 9.4 -> 9.5)"
    echo ""
    echo "Erzeugt: releases/vX.Y/UI-X.Y-AuraOS.tgz + Firmware-X.Y-AuraOS.bin"
    exit 1
fi

# --- PlatformIO finden ---
if command -v pio &> /dev/null; then
    PIO="pio"
elif [[ -f "$HOME/.platformio/penv/bin/pio" ]]; then
    PIO="$HOME/.platformio/penv/bin/pio"
else
    echo "FEHLER: PlatformIO nicht gefunden"
    echo "Installiere: https://platformio.org/install/cli"
    exit 1
fi

# --- Version lesen ---
CONFIG_FILE="firmware/src/config.h"
VERSION=$(grep '^#define MOODLIGHT_VERSION' "$CONFIG_FILE" | cut -d'"' -f2)
if [[ -z "$VERSION" ]]; then
    echo "FEHLER: MOODLIGHT_VERSION nicht in $CONFIG_FILE gefunden"
    exit 1
fi

# --- Version bumpen ---
bump_version() {
    local current="$1"
    local type="$2"
    local major minor
    IFS='.' read -r major minor <<< "$current"
    case "$type" in
        major) echo "$((major + 1)).0" ;;
        minor) echo "${major}.$((minor + 1))" ;;
    esac
}

NEW_VERSION=$(bump_version "$VERSION" "$BUMP_TYPE")

echo "========================================"
echo "  AuraOS Release Builder"
echo "========================================"
echo ""
echo "Version: ${VERSION} -> ${NEW_VERSION} (${BUMP_TYPE})"
echo ""

# --- config.h aktualisieren ---
sed -i '' "s/#define MOODLIGHT_VERSION \"${VERSION}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" "$CONFIG_FILE"

# --- Trap: Bei Fehler Version zuruecksetzen ---
rollback() {
    echo ""
    echo "FEHLER: Build fehlgeschlagen — Version wird zurueckgesetzt auf ${VERSION}"
    sed -i '' "s/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/#define MOODLIGHT_VERSION \"${VERSION}\"/" "$CONFIG_FILE"
}
trap rollback ERR

# --- Firmware kompilieren ---
echo "[1/4] Kompiliere Firmware..."
cd firmware
$PIO run
cd ..
echo "   -> Firmware kompiliert"

if [[ ! -f "firmware/.pio/build/esp32dev/firmware.bin" ]]; then
    echo "FEHLER: firmware.bin nicht gefunden nach Build"
    exit 1
fi

# --- Release-Dateien erstellen ---
echo "[2/4] Erstelle Release-Dateien..."
RELEASE_DIR="releases/v${NEW_VERSION}"
mkdir -p "$RELEASE_DIR"

# UI-TGZ (nur Web-Dateien, kein Firmware-Binary)
(cd firmware/data && tar -czf "../../${RELEASE_DIR}/UI-${NEW_VERSION}-AuraOS.tgz" \
    --exclude="*.tmp.*" --exclude="*.tgz" --exclude="*.tar" --exclude=".DS_Store" \
    index.html setup.html mood.html diagnostics.html js/ css/)
echo "   -> UI-${NEW_VERSION}-AuraOS.tgz: $(ls -lh "${RELEASE_DIR}/UI-${NEW_VERSION}-AuraOS.tgz" | awk '{print $5}')"

# Firmware-BIN
cp firmware/.pio/build/esp32dev/firmware.bin "${RELEASE_DIR}/Firmware-${NEW_VERSION}-AuraOS.bin"
echo "   -> Firmware-${NEW_VERSION}-AuraOS.bin: $(ls -lh "${RELEASE_DIR}/Firmware-${NEW_VERSION}-AuraOS.bin" | awk '{print $5}')"

# --- Verifizieren ---
echo "[3/4] Verifiziere..."
UI_CONTENTS=$(tar -tzf "${RELEASE_DIR}/UI-${NEW_VERSION}-AuraOS.tgz")
if ! echo "$UI_CONTENTS" | grep -q "index.html"; then
    echo "FEHLER: index.html fehlt im UI-TGZ"
    exit 1
fi
if ! echo "$UI_CONTENTS" | grep -q "setup.html"; then
    echo "FEHLER: setup.html fehlt im UI-TGZ"
    exit 1
fi
echo "   -> UI-TGZ verifiziert"
echo "   -> Firmware-BIN verifiziert ($(ls -lh "${RELEASE_DIR}/Firmware-${NEW_VERSION}-AuraOS.bin" | awk '{print $5}'))"

# --- Git Commit (nur config.h, nicht die Binaries) ---
echo "[4/4] Committe Version-Bump..."
git add "$CONFIG_FILE"
git commit -m "Release: AuraOS v${NEW_VERSION} (${BUMP_TYPE})"

echo ""
echo "========================================"
echo "  Build erfolgreich!"
echo "========================================"
echo ""
echo "Version:  ${NEW_VERSION}"
echo "Dateien:  ${RELEASE_DIR}/"
ls -lh "$RELEASE_DIR"
echo ""
echo "Upload auf http://192.168.0.140/setup -> Update Tab:"
echo "  1. UI-Datei:       ${RELEASE_DIR}/UI-${NEW_VERSION}-AuraOS.tgz"
echo "  2. Firmware-Datei: ${RELEASE_DIR}/Firmware-${NEW_VERSION}-AuraOS.bin"
echo "  3. 'Update starten' klicken"
echo ""
