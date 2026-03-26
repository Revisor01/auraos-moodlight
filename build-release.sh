#!/bin/bash
# AuraOS Moodlight - Combined Release Build Script
# Erzeugt ein versioniertes Combined-TGZ (UI + Firmware)
# Verwendung: ./build-release.sh [major|minor]

set -e

# --- Argument-Validierung (BUILD-01, BUILD-04) ---
BUMP_TYPE="${1:-}"
if [[ -z "$BUMP_TYPE" ]] || [[ ! "$BUMP_TYPE" =~ ^(major|minor)$ ]]; then
    echo "Verwendung: ./build-release.sh [major|minor]"
    echo ""
    echo "  major  - Erhoehe Major-Version (z.B. 9.1 -> 10.0)"
    echo "  minor  - Erhoehe Minor-Version (z.B. 9.1 -> 9.2)"
    echo ""
    echo "AuraOS verwendet X.Y Versionierung (kein Patch)."
    exit 1
fi

# --- Version lesen (mit Empty-Guard) ---
CONFIG_FILE="firmware/src/config.h"
VERSION=$(grep '^#define MOODLIGHT_VERSION' "$CONFIG_FILE" | cut -d'"' -f2)
if [[ -z "$VERSION" ]]; then
    echo "FEHLER: MOODLIGHT_VERSION nicht in $CONFIG_FILE gefunden"
    exit 1
fi

# --- Version bumpen (BUILD-01) ---
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
echo "  AuraOS Combined Release Builder"
echo "========================================"
echo ""
echo "Version: ${VERSION} -> ${NEW_VERSION} (${BUMP_TYPE})"
echo ""

# --- config.h aktualisieren ---
sed -i '' "s/#define MOODLIGHT_VERSION \"${VERSION}\"/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/" "$CONFIG_FILE"

# --- Trap: Bei Fehler Version zuruecksetzen + tmp-Dateien aufraeumen ---
cleanup() {
    rm -f /tmp/auraos_combined.tar /tmp/firmware.ino.bin /tmp/VERSION.txt
}
rollback() {
    echo ""
    echo "FEHLER: Build fehlgeschlagen — Version wird zurueckgesetzt auf ${VERSION}"
    sed -i '' "s/#define MOODLIGHT_VERSION \"${NEW_VERSION}\"/#define MOODLIGHT_VERSION \"${VERSION}\"/" "$CONFIG_FILE"
    cleanup
}
trap rollback ERR
trap cleanup EXIT

# --- Firmware kompilieren (BUILD-04: set -e statt Fehler-Unterdrückung, direkter pio run) ---
echo "[1/4] Kompiliere Firmware..."
cd firmware
pio run
cd ..
echo "   -> Firmware kompiliert"

# Verify firmware.bin exists
if [[ ! -f "firmware/.pio/build/esp32dev/firmware.bin" ]]; then
    echo "FEHLER: firmware.bin nicht gefunden nach Build"
    exit 1
fi

# --- Combined-TGZ erstellen (BUILD-02) ---
echo "[2/4] Erstelle Combined-TGZ..."
RELEASE_DIR="releases/v${NEW_VERSION}"
mkdir -p "$RELEASE_DIR"

# Schritt 1: UI-Dateien als unkomprimiertes TAR (macOS BSD tar: --exclude vor Dateien)
(cd firmware/data && tar -cf /tmp/auraos_combined.tar \
    --exclude="*.tmp.*" --exclude="*.tgz" --exclude="*.tar" \
    index.html setup.html mood.html diagnostics.html js/ css/)

# Schritt 2: firmware.bin als firmware.ino.bin anhaengen (ESP32-targz Naming-Convention)
cp firmware/.pio/build/esp32dev/firmware.bin /tmp/firmware.ino.bin
tar -rf /tmp/auraos_combined.tar -C /tmp firmware.ino.bin

# Schritt 2b: VERSION.txt fuer ESP32-Handler erzeugen und ins Archiv aufnehmen (OTA-03)
echo "${NEW_VERSION}" > /tmp/VERSION.txt
tar -rf /tmp/auraos_combined.tar -C /tmp VERSION.txt

# Schritt 3: Gzippen
TGZ_NAME="AuraOS-${NEW_VERSION}.tgz"
gzip -c /tmp/auraos_combined.tar > "${RELEASE_DIR}/${TGZ_NAME}"
echo "   -> ${TGZ_NAME}: $(ls -lh "${RELEASE_DIR}/${TGZ_NAME}" | awk '{print $5}')"

# --- TGZ verifizieren ---
echo "[3/4] Verifiziere TGZ-Inhalt..."
TAR_CONTENTS=$(tar -tzf "${RELEASE_DIR}/${TGZ_NAME}")
if ! echo "$TAR_CONTENTS" | grep -q "firmware.ino.bin"; then
    echo "FEHLER: firmware.ino.bin fehlt im TGZ"
    exit 1
fi
if ! echo "$TAR_CONTENTS" | grep -q "index.html"; then
    echo "FEHLER: index.html fehlt im TGZ"
    exit 1
fi
if ! echo "$TAR_CONTENTS" | grep -q "VERSION.txt"; then
    echo "FEHLER: VERSION.txt fehlt im TGZ"
    exit 1
fi
echo "   -> TGZ verifiziert (UI-Dateien + firmware.ino.bin + VERSION.txt vorhanden)"

# --- Git Commit (BUILD-03: nur nach erfolgreichem Build) ---
echo "[4/4] Committe Version-Bump + Release-Artefakt..."
git add "$CONFIG_FILE" "${RELEASE_DIR}/${TGZ_NAME}"
git commit -m "Release: AuraOS v${NEW_VERSION}"

echo ""
echo "========================================"
echo "  Build erfolgreich!"
echo "========================================"
echo ""
echo "Version:  ${NEW_VERSION}"
echo "Artefakt: ${RELEASE_DIR}/${TGZ_NAME}"
echo "Commit:   $(git log --oneline -1)"
echo ""
echo "TGZ-Inhalt:"
tar -tzf "${RELEASE_DIR}/${TGZ_NAME}"
echo ""
