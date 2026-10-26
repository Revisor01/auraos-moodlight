<h1 align="center">AuraOS Moodlight</h1>

<p align="center">
  A light that shows the mood of the news —<br>
  an ESP32 with an LED ring and a small service that rates headlines.
</p>

<p align="center">
  <a href="README.md">Deutsch</a> · <b>English</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Platform-ESP32-green" alt="Platform">
  <img src="https://img.shields.io/badge/Backend-Python%203.12-yellow" alt="Backend">
  <img src="https://img.shields.io/github/v/release/Revisor01/auraos-moodlight?label=Firmware" alt="Firmware version">
  <img src="https://img.shields.io/github/license/Revisor01/auraos-moodlight" alt="License">
</p>

## What it is about

The news almost always tells you what is going wrong. Whether things are
actually worse today than last week, nobody tells you.

The Moodlight answers exactly that, without a word: a service regularly reads
the headlines of German news sources, has them rated and compares the result
with the last seven days. The lamp shows this comparison as a colour, from red
(much worse than the week) to violet (much better). You do not have to read
what was good or bad; you see it.

More about the idea: **[revisor01.github.io/auraos-moodlight](https://revisor01.github.io/auraos-moodlight)**

The first version from 2022 is in
[Zustand-der-Welt](https://github.com/Revisor01/Zustand-der-Welt).

## Features

**The lamp (ESP32)**
- **Five colour levels** — the state of the world compared with last week
- **Web interface** — start page with the current state and history, settings
  for Wi-Fi, service address, MQTT, pins and colours (in German)
- **Home Assistant** — via MQTT with automatic discovery (see below)
- **Temperature and humidity** — with a DHT22, if connected
- **Updates on click** — the lamp reports approved firmware and only installs
  it once you confirm

**The service (sentiment API)**
- **Rating the headlines** — with Anthropic Claude Haiku, every 30 minutes
- **Sources of your choice** — eleven German news sources are preset, more can
  be added in the admin area
- **Unlimited history** — all measurements in PostgreSQL, the answers for the
  lamps from a Redis cache
- **Firmware mirror** — new releases only reach the lamps after approval in the
  admin area

## Installation

**Building the lamp.** You need an ESP32 development board, a WS2812B LED ring
or strip (default: 12 LEDs on pin 26), optionally a DHT22 (pin 18) and a 5 V
power supply with at least 2 A.

**Flashing the firmware.** Ready-made files are attached to every
[release](https://github.com/Revisor01/auraos-moodlight/releases). For the
first start via USB with [PlatformIO](https://platformio.org):

```bash
git clone https://github.com/Revisor01/auraos-moodlight.git
cd auraos-moodlight/firmware
pio run -e esp32dev --target uploadfs   # web interface
pio run -e esp32dev --target upload     # firmware
```

**Setup.** On first start the lamp opens the Wi-Fi network “Moodlight-Setup”.
Connect to it and the setup page opens (otherwise `http://192.168.4.1/setup`);
enter your own Wi-Fi there. Afterwards the lamp is reachable at
`http://moodlight.local/setup`.

As the service address you can enter the public service
`https://analyse.godsapp.de`, or your own (see below).

**Updates.** The lamp asks for approved versions every hour and then shows a
notice on its start page; it only installs on click. To update by hand, use the
“Update” tab in the settings: first upload the UI file (`.tgz`), then the
firmware (`.bin`) from the release.

## Home Assistant

The lamp registers via MQTT with automatic discovery. It creates (entity names
are German):

| Entity | Meaning |
|---|---|
| Moodlight (light) | on/off, colour, brightness |
| Weltlage Score | rating from −1.0 to +1.0 |
| Weltlage Kategorie | very negative … very positive |
| Weltlage Perzentil | position within the last 7 days (0–100 %) |
| Weltlage relativ | the colour level in words (“much worse than the week” … “much better than the week”) |
| Weltlage Stufe | the same level as a number 0–4 |
| Temperatur, Luftfeuchtigkeit | DHT22 readings |
| Betriebsmodus | auto or manual |
| Update-Intervalle | polling interval in seconds |
| Weltlage aktualisieren | button for an immediate fetch |
| Laufzeit, WLAN-Signal, Status | diagnostics |

## Versions

What changed when is in [CHANGELOG.md](CHANGELOG.md) (in German, following
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/)). The
[releases](https://github.com/Revisor01/auraos-moodlight/releases) carry the
ready-made firmware and web interface for each version.

## Running it yourself

The service runs as a Docker stack of API, PostgreSQL and Redis. You need
Docker and an API key from Anthropic.

```bash
git clone https://github.com/Revisor01/auraos-moodlight.git
cd auraos-moodlight/sentiment-api
cp .env.example .env    # enter values, see table
docker compose up -d
# listens on port 6237
```

| Variable | Required | Meaning |
|----------|----------|---------|
| `ANTHROPIC_API_KEY` | yes | key for rating the headlines |
| `POSTGRES_PASSWORD` | yes | database password |
| `ADMIN_PASSWORD` | yes | login to the admin area |
| `SECRET_KEY` | yes | session key, e.g. `openssl rand -hex 32` |
| `FIRMWARE_SYNC_TOKEN` | no | allows a release workflow to upload firmware into the mirror; without a value this stays locked |

`init.sql` creates the database schema including the preset sources on first
start. Then enter the address of your own service in the lamp's settings. More
on endpoints and the admin area in
[sentiment-api/README.md](sentiment-api/README.md), on the firmware in
[firmware/README.md](firmware/README.md) (both German).

**Tests.**

```bash
cd sentiment-api && pip install -r requirements.txt && python -m unittest discover -s tests
cd firmware && pio test -e native
```

## Structure

```
auraos-moodlight
├── firmware/        — ESP32 (C++/Arduino, PlatformIO)
│   ├── src/         — lamp, Wi-Fi, MQTT, updates
│   ├── data/        — the lamp's web interface
│   └── test/        — tests, also run without a device
├── sentiment-api/   — service (Python, Flask, PostgreSQL, Redis)
│   ├── app.py               — API and admin area
│   ├── background_worker.py — fetching and rating every 30 minutes
│   └── firmware_mirror.py   — firmware mirror and approval
├── docs/            — project page (GitHub Pages)
└── build-release.sh — local firmware build, upload to a lamp
```

**What matters when building it:**
- **The lamp is a thin client.** It fetches a ready-calculated value and shows
  it; fetching, rating and history live in the service.
- **Relative, not absolute.** The colour comes from the comparison with the
  last seven days, not from the raw score.
- **No update without a click.** An update that installs itself could restart
  the light in the evening or leave a device unreachable.
- **One worker process.** The service runs with exactly one process so that the
  background evaluation only runs once.

## Contributing

Bug reports and suggestions are welcome as an
[issue](https://github.com/Revisor01/auraos-moodlight/issues); the template
asks for firmware version and hardware. Please report security issues privately
as described in [SECURITY.md](SECURITY.md). Pull requests go through tests and
a secret scan; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Privacy

The lamp sends no data about its users. It asks the configured service for the
current value and approved updates and otherwise only talks to your own MQTT
broker. The service processes public headlines, no personal data.

## License

AuraOS Moodlight is licensed under the
[GNU Affero General Public License v3.0](LICENSE). If you modify it and make
it available to others, including as a web service, you publish your source
code under the same license. The [LICENSE](LICENSE) file is authoritative;
contributions follow [CONTRIBUTING.md](CONTRIBUTING.md).

## Contact

Pastor Simon Luthe · [mail@simonluthe.de](mailto:mail@simonluthe.de) ·
[simonluthe.de](https://simonluthe.de)
