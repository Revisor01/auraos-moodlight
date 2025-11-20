#!/bin/bash
# Schnelles Update-Script (für lokale Entwicklung)
# Usage: ./update.sh "Commit message"

set -e

GREEN='\033[0;32m'
NC='\033[0m'

echo -e "${GREEN}[UPDATE]${NC} Git Add..."
git add .

echo -e "${GREEN}[UPDATE]${NC} Git Commit..."
if [ -z "$1" ]; then
    git commit -m "Update: $(date '+%Y-%m-%d %H:%M')"
else
    git commit -m "$1"
fi

echo -e "${GREEN}[UPDATE]${NC} Git Push..."
git push

echo ""
echo -e "${GREEN}[SUCCESS]${NC} Code gepusht!"
echo ""
echo "Auf dem Server deployen:"
echo "  ssh root@server.godsapp.de '/root/news/deploy.sh'"
echo ""
