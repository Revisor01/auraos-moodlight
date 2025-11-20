#!/bin/bash
# Auto-Deploy Script für News Sentiment Backend
# Wird auf dem Server ausgeführt: /root/news/deploy.sh

set -e  # Stoppe bei Fehlern

echo "=========================================="
echo "News Sentiment Backend - Auto Deploy"
echo "=========================================="
echo ""

# Farben für Output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Funktion für farbigen Output
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 1. In Projekt-Verzeichnis wechseln
cd /root/news || { log_error "Verzeichnis /root/news nicht gefunden!"; exit 1; }

log_info "Aktuelles Verzeichnis: $(pwd)"

# 2. Git Repository Status
if [ ! -d ".git" ]; then
    log_warn "Kein Git Repository gefunden. Klone von GitHub..."
    cd /root
    rm -rf news_backup
    mv news news_backup
    git clone https://github.com/Revisor01/news-sentiment-backend.git news
    cd news
    log_info "Repository geklont."
else
    log_info "Git Repository gefunden. Pulling latest changes..."

    # Stash local changes (falls vorhanden)
    if ! git diff-index --quiet HEAD --; then
        log_warn "Lokale Änderungen gefunden - stashing..."
        git stash
    fi

    # Pull latest changes
    git pull origin main || git pull origin master
    log_info "Latest changes pulled."
fi

# 3. Check ob .env existiert
if [ ! -f ".env" ]; then
    log_error ".env Datei nicht gefunden!"
    log_warn "Bitte .env erstellen basierend auf .env.example"
    log_warn "cp .env.example .env && nano .env"
    exit 1
fi

log_info ".env Datei gefunden ✓"

# 4. Docker Compose File vorbereiten
if [ -f "docker-compose-NEW.yaml" ] && [ ! -f "docker-compose.yaml.deployed ]; then
    log_info "Erste Deployment: docker-compose.yaml ersetzen..."
    cp docker-compose.yaml docker-compose.yaml.old 2>/dev/null || true
    cp docker-compose-NEW.yaml docker-compose.yaml
    touch docker-compose.yaml.deployed
fi

# 5. Docker Container stoppen
log_info "Stoppe alte Container..."
docker-compose down || true

# 6. Docker Container neu bauen und starten
log_info "Baue und starte neue Container..."
docker-compose up -d --build

# 7. Warte auf Container-Start
log_info "Warte 10 Sekunden auf Container-Start..."
sleep 10

# 8. Container-Status prüfen
log_info "Container-Status:"
docker-compose ps

# 9. Logs anzeigen (letzte 20 Zeilen)
echo ""
log_info "Letzte Logs:"
docker-compose logs --tail=20

# 10. Health-Check
echo ""
log_info "Health-Check..."
if curl -f http://localhost:6237/api/moodlight/stats &>/dev/null; then
    log_info "✓ API ist erreichbar!"
else
    log_warn "⚠ API antwortet nicht. Prüfe Logs mit: docker-compose logs -f"
fi

echo ""
log_info "=========================================="
log_info "Deployment abgeschlossen!"
log_info "=========================================="
echo ""
log_info "Nützliche Befehle:"
echo "  docker-compose logs -f          # Logs live verfolgen"
echo "  docker-compose ps               # Container-Status"
echo "  docker-compose restart          # Container neu starten"
echo "  curl http://localhost:6237/api/moodlight/current  # API testen"
echo ""
