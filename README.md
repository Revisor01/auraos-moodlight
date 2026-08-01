# AuraOS Moodlight - Monorepo

<div align="center">

![AuraOS Version](https://img.shields.io/badge/AuraOS-v9.15-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-green)
![Backend](https://img.shields.io/badge/Backend-Python%203.12-yellow)
![License](https://img.shields.io/badge/License-MIT-orange)

*Ein intelligentes IoT-Stimmungslicht, das die Weltlage durch KI-gestützte Sentiment-Analyse visualisiert*

[Features](#features) • [Quick Start](#quick-start) • [Architektur](#architektur) • [Deployment](#deployment)

</div>

---

## 📁 Repository-Struktur

```
auraos-moodlight/
├── firmware/              # ESP32 Firmware (C++/Arduino)
│   ├── src/              # Hauptcode
│   ├── data/             # Web-Interface (HTML/CSS/JS)
│   └── platformio.ini    # Build-Konfiguration
│
├── sentiment-api/        # Python Backend Service
│   ├── app.py           # Flask API
│   ├── database.py      # PostgreSQL Interface
│   ├── background_worker.py
│   └── docker-compose.yaml
│
├── docs/                 # GitHub Pages Website
│   └── index.html
│
├── build-release.sh      # Release Build Script
├── CHANGELOG.md          # Versionshistorie (Keep a Changelog)
└── README.md            # Diese Datei
```

---

## 🎯 Überblick

AuraOS ist ein **selbst-hostbares** Smart Home System bestehend aus:

1. **ESP32 Moodlight** - Visualisiert Weltstimmung durch LEDs
2. **Sentiment API** - Analysiert News mit Anthropic Claude Haiku
3. **Home Assistant Integration** - MQTT & Auto-Discovery

Der ESP32 ist ein dünner Client: Er pollt den vom Backend vorberechneten
Sentiment-Score und stellt ihn als LED-Farbe dar. Die gesamte Analyse
(RSS-Abruf, Claude-Bewertung, Historie) läuft im Backend.

### Architektur in Kürze

- **Backend-First** - Alle Daten zentral in PostgreSQL, keine CSV-Speicherung auf dem ESP32
- **RSS-Konfiguration** im Backend, nicht auf dem Gerät
- **Redis-Cache** vor den Geräte-Endpoints (`/api/moodlight/current`)
- **Background Worker** analysiert alle 30 Minuten; das Gerät synchronisiert
  seinen Poll seit v9.14 auf diesen Analyse-Takt

Die vollständige Versionshistorie steht in [CHANGELOG.md](CHANGELOG.md).

---

## ⚡ Quick Start

### Option A: Komplettes Self-Hosting

```bash
# 1. Repository klonen
git clone https://github.com/revisor01/auraos-moodlight.git
cd auraos-moodlight

# 2. Backend starten
cd sentiment-api
cp .env.example .env
# Bearbeite .env und setze ANTHROPIC_API_KEY und POSTGRES_PASSWORD
docker compose up -d

# 3. Firmware flashen
cd ../firmware
pio run --target uploadfs  # Web-UI
pio run --target upload    # Firmware
```

### Option B: Nur Firmware (mit Public API)

```bash
cd firmware
pio run --target upload
# Konfiguriere: https://analyse.godsapp.de als API-URL
```

---

## 🎨 Features

### Moodlight (ESP32)

- ✅ **5-stufige Farbskala** - Rot (negativ) bis Violett (positiv)
- ✅ **Web-Interface** - Dashboard, Weltlage-Statistiken, Einstellungen (inkl. Update- und Info-Tab)
- ✅ **MQTT Integration** - Home Assistant Auto-Discovery
- ✅ **DHT22 Support** - Temperatur & Luftfeuchtigkeit
- ✅ **OTA Updates** - Over-The-Air Firmware/UI Updates über den Update-Tab

### Sentiment API

- ✅ **Sentiment-Analyse** - Anthropic Claude Haiku
- ✅ **12 deutsche Nachrichtenquellen**
- ✅ **PostgreSQL** - Unbegrenzte Historie
- ✅ **Redis Cache** - 5-Min TTL
- ✅ **Background Worker** - 30-Min Updates

---

## 🚀 Deployment

### Backend

Push nach `main` genügt — es gibt keinen SSH-Pull-Workflow:

1. Änderungen in `sentiment-api/` committen und nach `main` pushen
2. GitHub Actions baut das Docker-Image und pusht es nach GHCR
3. Ein Portainer-Webhook deployt den Container `moodlight-analyzer` neu

Produktiv läuft das Backend unter `https://analyse.godsapp.de` mit
gunicorn (`-w 1 --threads 4`) — ein Worker-Prozess, damit der Background
Worker genau einmal läuft.

### Firmware

```bash
./build-release.sh minor    # bumpt config.h, baut lokal, committet den Bump
git push origin main
git tag v9.15 && git push origin v9.15
```

Der Tag-Push startet den Workflow `release-firmware.yml`: Er baut Firmware
und UI in der CI, prüft das Ergebnis (UI-Archiv vollständig, ESP32-Magic-Byte
im Binary) und legt beide Dateien ans GitHub-Release. Die Release-Notes
kommen aus dem passenden CHANGELOG-Abschnitt. Der Tag muss zur
`MOODLIGHT_VERSION` in `firmware/src/config.h` passen, sonst bricht der
Workflow ab.

Installation am Gerät über `http://<geraete-ip>/setup` → Tab „Update":
erst die UI-`.tgz`, dann die Firmware-`.bin` hochladen — beide liegen am
Release. Alternativ per USB mit `pio run -t upload` und `pio run -t uploadfs`.

### Regel für neue Versionen

Jeder Push, der eine neue Version darstellt, bekommt im selben Arbeitsgang:
CHANGELOG-Eintrag, Tag `vX.Y` und GitHub-Release mit dem CHANGELOG-Abschnitt
als Notes. Zwischen-Commits ohne Versionssprung sammeln sich unter
`[Unreleased]`.

Weitere Details: `firmware/README.md` und `sentiment-api/README.md`.

---

## 🏠 Home Assistant Integration

Auto-Discovery über MQTT. Angelegte Entitäten:

| Entität | Beschreibung |
|---|---|
| Moodlight (Light) | LED-Steuerung: an/aus, Farbe, Helligkeit |
| Weltlage Score | Sentiment-Score (−1.0 bis +1.0) |
| Weltlage Kategorie | sehr negativ … sehr positiv |
| Weltlage Perzentil | Einordnung im 7-Tage-Fenster (0–100 %) |
| Temperatur / Luftfeuchtigkeit | DHT22-Sensorwerte |
| Betriebsmodus | Auto oder Manuell |
| Stimmung/Sensor Update Intervall | Poll-Intervalle in Sekunden |
| Weltlage aktualisieren | Button für sofortigen Abruf |
| Uptime / WiFi Signal / Status | Diagnose-Sensoren |

---

## 📊 Eigenschaften

- **Response Time:** <10 ms für gecachte Geräte-Endpoints (Redis, 5 min TTL)
- **Analyse-Takt:** alle 30 Minuten durch den Background Worker
- **Geräte-Poll:** synchronisiert auf den Analyse-Takt (seit v9.14),
  Anzeige maximal ~2 Minuten hinter der aktuellen Analyse
- **Skalierung:** Der Cache entkoppelt die Gerätezahl von der Analyse-Last

---

## 📖 Dokumentation

- **[Firmware Details](firmware/)** - ESP32 Code & Web-UI
- **[Backend API](sentiment-api/README.md)** - Sentiment Service
- **[CHANGELOG](CHANGELOG.md)** - Versionshistorie
- **[Website](https://revisor01.github.io/auraos-moodlight)** - Live Demo & Docs
- **[Releases](https://github.com/Revisor01/auraos-moodlight/releases)** - Fertige Binaries

---

## 📄 Lizenz

MIT License - Copyright (c) 2025 AuraOS Contributors

---

<div align="center">

**Gebaut mit ❤️ für eine bessere Welt**

[Website](https://revisor01.github.io/auraos-moodlight) • [Issues](https://github.com/revisor01/auraos-moodlight/issues)

</div>
