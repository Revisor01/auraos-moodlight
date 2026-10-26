<h1 align="center">AuraOS Moodlight</h1>

<p align="center">
  Ein Licht, das die Stimmung der Nachrichtenlage zeigt —<br>
  ein ESP32 mit LED-Ring und ein kleiner Dienst, der Schlagzeilen auswertet.
</p>

<p align="center">
  <b>Deutsch</b> · <a href="README.en.md">English</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Plattform-ESP32-green" alt="Plattform">
  <img src="https://img.shields.io/badge/Backend-Python%203.12-yellow" alt="Backend">
  <img src="https://img.shields.io/github/v/release/Revisor01/auraos-moodlight?label=Firmware" alt="Firmware-Version">
  <img src="https://img.shields.io/github/license/Revisor01/auraos-moodlight" alt="Lizenz">
</p>

## Worum es geht

Nachrichten erzählen fast immer von dem, was schiefgeht. Ob die Lage heute
tatsächlich schlechter ist als letzte Woche, sagt einem niemand.

Das Moodlight beantwortet genau das, ohne ein Wort: Ein Dienst liest
regelmäßig die Schlagzeilen deutscher Nachrichtenquellen, lässt sie
bewerten und ordnet das Ergebnis gegen die letzten sieben Tage ein. Die
Lampe zeigt diese Einordnung als Farbe, von Rot (viel schlechter als die
Woche) bis Violett (viel besser). Was gut oder schlecht war, muss man nicht
lesen; man sieht es.

Mehr zur Idee: **[revisor01.github.io/auraos-moodlight](https://revisor01.github.io/auraos-moodlight)**

Die erste Fassung von 2022 steht in
[Zustand-der-Welt](https://github.com/Revisor01/Zustand-der-Welt).

## Funktionen

**Die Lampe (ESP32)**
- **Fünf Farbstufen** — die Weltlage im Vergleich zur letzten Woche
- **Weboberfläche** — Startseite mit aktueller Lage und Verlauf,
  Einstellungen für WLAN, Dienst-Adresse, MQTT, Pins und Farben
- **Home Assistant** — per MQTT mit automatischer Erkennung (siehe unten)
- **Temperatur und Luftfeuchte** — mit einem DHT22, wenn angeschlossen
- **Updates auf Klick** — die Lampe meldet freigegebene Firmware und
  installiert sie erst, wenn man es bestätigt

**Der Dienst (Sentiment-API)**
- **Bewertung der Schlagzeilen** — mit Anthropic Claude Haiku, alle
  30 Minuten
- **Quellen frei wählbar** — elf deutsche Nachrichtenquellen sind
  voreingestellt, weitere lassen sich im Admin-Bereich anlegen
- **Verlauf ohne Ende** — alle Messungen in PostgreSQL, die Antworten für die
  Lampen aus einem Redis-Zwischenspeicher
- **Firmware-Spiegel** — neue Releases kommen erst nach Freigabe im
  Admin-Bereich bei den Lampen an

## Installation

**Lampe bauen.** Nötig sind ein ESP32-Entwicklungsboard, ein WS2812B-LED-Ring
oder -Streifen (Standard: 12 LEDs an Pin 26), optional ein DHT22 (Pin 18) und
ein 5-V-Netzteil mit mindestens 2 A.

**Firmware aufspielen.** Fertige Dateien liegen an jedem
[Release](https://github.com/Revisor01/auraos-moodlight/releases). Für den
ersten Start per USB mit [PlatformIO](https://platformio.org):

```bash
git clone https://github.com/Revisor01/auraos-moodlight.git
cd auraos-moodlight/firmware
pio run -e esp32dev --target uploadfs   # Weboberfläche
pio run -e esp32dev --target upload     # Firmware
```

**Einrichten.** Beim ersten Start öffnet die Lampe das WLAN
„Moodlight-Setup“. Damit verbinden, die Einrichtungsseite öffnet sich (sonst
`http://192.168.4.1/setup`), dort das eigene WLAN eintragen. Danach ist die
Lampe unter `http://moodlight.local/setup` erreichbar.

Als Dienst-Adresse lässt sich der öffentliche Dienst
`https://analyse.godsapp.de` eintragen, oder ein eigener (siehe unten).

**Updates.** Die Lampe fragt stündlich nach freigegebenen Versionen und
zeigt dann auf der Startseite einen Hinweis; installiert wird nur auf Klick.
Von Hand geht es im Tab „Update“ der Einstellungen: erst die UI-Datei
(`.tgz`), dann die Firmware (`.bin`) vom Release hochladen.

## Home Assistant

Die Lampe meldet sich per MQTT mit automatischer Erkennung an. Angelegt
werden:

| Entität | Bedeutung |
|---|---|
| Moodlight (Licht) | an/aus, Farbe, Helligkeit |
| Weltlage Score | Bewertung von −1,0 bis +1,0 |
| Weltlage Kategorie | sehr negativ … sehr positiv |
| Weltlage Perzentil | Einordnung in den letzten 7 Tagen (0–100 %) |
| Weltlage relativ | die Farbstufe in Worten („viel schlechter als die Woche“ … „viel besser als die Woche“) |
| Weltlage Stufe | dieselbe Stufe als Zahl 0–4 |
| Temperatur, Luftfeuchtigkeit | Werte des DHT22 |
| Betriebsmodus | Auto oder Manuell |
| Update-Intervalle | Abfragetakt in Sekunden |
| Weltlage aktualisieren | Knopf für einen sofortigen Abruf |
| Laufzeit, WLAN-Signal, Status | Diagnose |

## Versionen

Was sich wann geändert hat, steht in [CHANGELOG.md](CHANGELOG.md) — nach
[Keep a Changelog](https://keepachangelog.com/de/1.1.0/), aus Sicht der
Nutzer:innen geschrieben. Die
[Releases](https://github.com/Revisor01/auraos-moodlight/releases) tragen je
Version die fertige Firmware und Weboberfläche.

## Selbst betreiben

Der Dienst läuft als Docker-Stack aus API, PostgreSQL und Redis. Nötig sind
Docker und ein API-Schlüssel von Anthropic.

```bash
git clone https://github.com/Revisor01/auraos-moodlight.git
cd auraos-moodlight/sentiment-api
cp .env.example .env    # Werte eintragen, siehe Tabelle
docker compose up -d
# hört auf Port 6237
```

| Variable | Pflicht | Bedeutung |
|----------|---------|-----------|
| `ANTHROPIC_API_KEY` | ja | Schlüssel für die Bewertung der Schlagzeilen |
| `POSTGRES_PASSWORD` | ja | Passwort der Datenbank |
| `ADMIN_PASSWORD` | ja | Anmeldung im Admin-Bereich |
| `SECRET_KEY` | ja | Sitzungsschlüssel, z. B. `openssl rand -hex 32` |
| `FIRMWARE_SYNC_TOKEN` | nein | erlaubt einem Release-Workflow, Firmware in den Spiegel zu laden; ohne Wert bleibt das gesperrt |

Das Datenbankschema samt voreingestellten Quellen legt `init.sql` beim ersten
Start an. In den Einstellungen der Lampe dann die Adresse des eigenen
Dienstes eintragen. Mehr zu Endpunkten und Admin-Bereich steht in
[sentiment-api/README.md](sentiment-api/README.md), zur Firmware in
[firmware/README.md](firmware/README.md).

**Tests.**

```bash
cd sentiment-api && pip install -r requirements.txt && python -m unittest discover -s tests
cd firmware && pio test -e native
```

## Aufbau

```
auraos-moodlight
├── firmware/        — ESP32 (C++/Arduino, PlatformIO)
│   ├── src/         — Lampe, WLAN, MQTT, Updates
│   ├── data/        — Weboberfläche der Lampe
│   └── test/        — Tests, laufen auch ohne Gerät
├── sentiment-api/   — Dienst (Python, Flask, PostgreSQL, Redis)
│   ├── app.py               — API und Admin-Bereich
│   ├── background_worker.py — Abruf und Bewertung alle 30 Minuten
│   └── firmware_mirror.py   — Spiegel und Freigabe der Firmware
├── docs/            — Projektseite (GitHub Pages)
└── build-release.sh — lokaler Firmware-Build, Upload an eine Lampe
```

**Worauf es beim Bauen ankommt:**
- **Die Lampe ist ein dünner Client.** Sie holt einen fertig berechneten Wert
  und zeigt ihn; Abruf, Bewertung und Verlauf liegen im Dienst.
- **Relativ statt absolut.** Die Farbe ergibt sich aus dem Vergleich mit den
  letzten sieben Tagen, nicht aus dem Rohwert.
- **Kein Update ohne Klick.** Ein Update, das sich selbst einspielt, könnte
  das Licht am Abend neu starten oder ein Gerät unerreichbar zurücklassen.
- **Ein Worker-Prozess.** Der Dienst läuft mit genau einem Prozess, damit die
  Hintergrundauswertung nur einmal läuft.

## Mitmachen

Fehlermeldungen und Vorschläge sind willkommen — gern als
[Issue](https://github.com/Revisor01/auraos-moodlight/issues); die Vorlage
fragt nach Firmware-Version und Hardware. Sicherheitslücken bitte vertraulich
melden, wie in [SECURITY.md](SECURITY.md) beschrieben. Pull Requests laufen
durch Tests und einen Secret-Scan; die Grundsätze für Beiträge stehen in
[CONTRIBUTING.md](CONTRIBUTING.md).

## Datenschutz

Die Lampe schickt keine Daten über ihre Nutzer:innen. Sie fragt beim
eingetragenen Dienst den aktuellen Wert und freigegebene Updates ab und
spricht sonst nur mit dem eigenen MQTT-Broker. Der Dienst verarbeitet
öffentliche Schlagzeilen, keine personenbezogenen Daten.

## Lizenz

AuraOS Moodlight steht unter der
[GNU Affero General Public License v3.0](LICENSE). Wer AuraOS Moodlight
verändert und anderen zur Verfügung stellt, auch als Webdienst, legt seinen
Quelltext unter derselben Lizenz offen. Für Beiträge gilt
[CONTRIBUTING.md](CONTRIBUTING.md).

## Kontakt

Pastor Simon Luthe · [mail@simonluthe.de](mailto:mail@simonluthe.de) ·
[simonluthe.de](https://simonluthe.de)
