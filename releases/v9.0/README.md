# AuraOS Moodlight v9.0

## Dateien

- `UI-9.0-AuraOS.tgz` - Web-Interface Update
- `Firmware-9.0-AuraOS.bin` - Firmware Binary
- `checksums.txt` - SHA256 Checksums

## Installation (OTA)

### 1. UI Update (ZUERST!)
- Öffne: `http://<moodlight-ip>/diagnostics.html`
- UI Update Sektion → Wähle `UI-9.0-AuraOS.tgz`
- Upload & warten

### 2. Firmware Update (DANACH!)
- Firmware Update Sektion → Wähle `Firmware-9.0-AuraOS.bin`
- Upload → Gerät startet neu

**WICHTIG:** Firmware muss `Firmware-X.X-AuraOS.bin` heißen!

## Checksums verifizieren

```bash
shasum -a 256 -c checksums.txt
```
