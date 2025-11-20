# AuraOS Moodlight - Monorepo

<div align="center">

![AuraOS Version](https://img.shields.io/badge/AuraOS-v9.0-blue)
![Platform](https://img.shields.io/badge/Platform-ESP32-green)
![Backend](https://img.shields.io/badge/Backend-Python%203.11-yellow)
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
└── README.md            # Diese Datei
```

---

## 🎯 Überblick

AuraOS ist ein **selbst-hostbares** Smart Home System bestehend aus:

1. **ESP32 Moodlight** - Visualisiert Weltstimmung durch LEDs
2. **Sentiment API** - Analysiert News mit OpenAI GPT-4o-mini
3. **Home Assistant Integration** - MQTT & Auto-Discovery

### Was ist neu in v9.0?

- ✅ **Backend-First Architektur** - Alle Daten zentral verwaltet
- ✅ **97% Kostensenkung** - Von $150/Monat auf $5/Monat
- ✅ **Keine lokale CSV-Speicherung** - PostgreSQL statt ESP32 Flash
- ✅ **Keine Device-seitige RSS-Config** - Zentral im Backend
- ✅ **Optimierte API-Endpoints** - `/api/moodlight/current` mit Cache

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
# Bearbeite .env und setze OPENAI_API_KEY
docker-compose up -d

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
- ✅ **Web-Interface** - Setup, Statistiken, Diagnostics
- ✅ **MQTT Integration** - Home Assistant Auto-Discovery
- ✅ **DHT22 Support** - Temperatur & Luftfeuchtigkeit
- ✅ **OTA Updates** - Over-The-Air Firmware/UI Updates

### Sentiment API

- ✅ **Sentiment-Analyse** - OpenAI GPT-4o-mini
- ✅ **12 deutsche Nachrichtenquellen**
- ✅ **PostgreSQL** - Unbegrenzte Historie
- ✅ **Redis Cache** - 5-Min TTL
- ✅ **Background Worker** - 30-Min Updates

---

## 🚀 Deployment

Siehe vollständige Dokumentation:
- **Firmware:** `firmware/README.md`
- **Backend:** `sentiment-api/README.md`
- **Release Building:** `./build-release.sh`

---

## 🏠 Home Assistant Integration

Auto-Discovery mit MQTT:
- `sensor.auraos_sentiment` - Weltlage Score
- `sensor.auraos_temperature` - Temperatur
- `light.auraos_moodlight` - LED Steuerung

---

## 📊 Performance (v9.0)

- **Kosten:** $5/Monat (war: $150/Monat)
- **Ersparnis:** 97%
- **Response Time:** <10ms (gecacht)
- **Skalierung:** 1000+ Geräte

---

## 📖 Dokumentation

- **[Firmware Details](firmware/)** - ESP32 Code & Web-UI
- **[Backend API](sentiment-api/README.md)** - Sentiment Service
- **[Website](https://revisor01.github.io/auraos-moodlight)** - Live Demo & Docs
- **[Releases](releases/)** - Fertige Binaries

---

## 📄 Lizenz

MIT License - Copyright (c) 2025 AuraOS Contributors

---

<div align="center">

**Gebaut mit ❤️ für eine bessere Welt**

[Website](https://revisor01.github.io/auraos-moodlight) • [Issues](https://github.com/revisor01/auraos-moodlight/issues)

</div>
