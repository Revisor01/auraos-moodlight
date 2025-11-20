# AuraOS Sentiment API

Backend-Service für die AuraOS Moodlight Sentiment-Analyse.

## Überblick

Dieser Service analysiert deutsche Nachrichtenquellen (RSS-Feeds) mit OpenAI GPT-4o-mini und berechnet einen gewichteten "World Mood Score". Die Daten werden in PostgreSQL gespeichert und über Redis gecacht.

## Features

- ✅ **Sentiment-Analyse** mit OpenAI GPT-4o-mini
- ✅ **PostgreSQL** für unbegrenzte Datenspeicherung
- ✅ **Redis Cache** (5-Minuten TTL) für performante API-Calls
- ✅ **Background Worker** (30-Minuten Updates)
- ✅ **Moodlight-optimierte Endpoints**
- ✅ **Docker Compose** Setup

## Architektur

```
┌─────────────────┐
│  Moodlight ESP32│ ← Holt gecachte Daten alle 30 Min
└────────┬────────┘
         │
    ┌────▼────────────┐
    │  Flask API      │
    │  (Port 5000)    │
    └────┬────────────┘
         │
    ┌────▼─────┬──────────┐
    │          │          │
┌───▼───┐ ┌───▼───┐ ┌───▼────────┐
│ Redis │ │PostgreSQL│ Background │
│ Cache │ │   DB   │  Worker    │
└───────┘ └────────┘ └────────────┘
```

## Quick Start

### 1. Environment Setup

```bash
cp .env.example .env
# Bearbeite .env und setze OPENAI_API_KEY
```

### 2. Docker Compose starten

```bash
docker-compose up -d
```

### 3. API testen

```bash
curl http://localhost:5000/api/moodlight/current
```

## API Endpoints

### Moodlight-Optimiert (v9.0)

**`GET /api/moodlight/current`**
```json
{
  "status": "success",
  "sentiment": -0.42,
  "headlines_analyzed": 24,
  "timestamp": "2025-11-20T10:30:00Z"
}
```

**`GET /api/moodlight/history?hours=168`**
```json
{
  "status": "success",
  "data": [
    {
      "timestamp": 1700456789,
      "sentiment_score": -0.42,
      "headlines_analyzed": 24
    }
  ]
}
```

**`GET /api/moodlight/trend`**
```json
{
  "status": "success",
  "trend": "improving",
  "change_7d": 0.15
}
```

### Legacy Endpoints

- `GET /api/news` - Vollständige Analyse mit Details
- `GET /api/news/total_sentiment` - Nur Sentiment-Score (gecacht)

## Konfiguration

### docker-compose.yaml

```yaml
services:
  app:
    build: .
    ports:
      - "5000:5000"
    environment:
      - OPENAI_API_KEY=${OPENAI_API_KEY}
      - POSTGRES_HOST=postgres
      - REDIS_HOST=redis
```

### RSS Quellen

Konfiguriert in `app.py` (Zeile 55-68):
- Tagesschau
- ZDF
- Spiegel Online
- Zeit Online
- FAZ
- Süddeutsche
- ARD
- Handelsblatt
- ntv
- Focus
- Welt
- Tagesspiegel

## Deployment

### Produktionsserver

```bash
# SSH zum Server
ssh root@server.godsapp.de

# Zum Projekt navigieren
cd /home/users/revisor/www/simon/news

# Aktualisieren
git pull
docker-compose build
docker-compose up -d
```

### Deployment-Skript

```bash
./deploy.sh
```

## Entwicklung

### Lokal ohne Docker

```bash
# Virtual Environment
python -m venv venv
source venv/bin/activate

# Dependencies
pip install -r requirements.txt

# Umgebungsvariablen
export OPENAI_API_KEY="your-key"
export POSTGRES_HOST="localhost"
export REDIS_HOST="localhost"

# App starten
python app.py
```

## Monitoring

### Logs ansehen

```bash
docker-compose logs -f app
docker-compose logs -f worker
```

### Datenbank prüfen

```bash
docker-compose exec postgres psql -U postgres -d moodlight
```

```sql
-- Letzte Einträge
SELECT * FROM sentiment_data ORDER BY timestamp DESC LIMIT 10;

-- Durchschnitt letzte 24h
SELECT AVG(sentiment_score) FROM sentiment_data
WHERE timestamp > NOW() - INTERVAL '24 hours';
```

### Redis Cache prüfen

```bash
docker-compose exec redis redis-cli

# Cache Key prüfen
GET moodlight:current_sentiment
TTL moodlight:current_sentiment
```

## Performance

**Kosten-Optimierung:**
- Vorher: $150/Monat (100 Geräte × 48 API-Calls/Tag)
- Nachher: $5/Monat (Background Worker + Cache)
- **Ersparnis: 97%** 🎯

**Response Times:**
- Gecachter Request: < 10ms
- Uncached Request: ~2s (OpenAI API)
- Background Update: ~5s

## Troubleshooting

### Container startet nicht

```bash
docker-compose logs app
# Prüfe OPENAI_API_KEY in .env
```

### Datenbank-Fehler

```bash
# Reset Database
docker-compose down -v
docker-compose up -d
```

### OpenAI API Fehler

```bash
# Rate Limit? Prüfe Background Worker Interval
# Siehe background_worker.py:35 (SLEEP_INTERVAL)
```

## Technologie-Stack

- **Python 3.11**
- **Flask 3.1.0** - Web Framework
- **OpenAI 1.70.0** - GPT-4o-mini Sentiment-Analyse
- **PostgreSQL 16** - Datenspeicherung
- **Redis 7** - Caching Layer
- **feedparser 6.0.11** - RSS Feed Parser
- **Docker & Docker Compose** - Containerisierung

## Lizenz

MIT License - Siehe Haupt-README
