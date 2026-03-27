---
phase: 13-authentifizierung
plan: "02"
subsystem: backend
tags: [auth, flask, session, decorator, api-protection]
dependency_graph:
  requires: [13-01 — login_required Decorator, /login Route, /logout Route]
  provides: [api_login_required Decorator, geschützte POST/DELETE Endpoints, Abmelden-Link in feeds.html]
  affects: [sentiment-api/moodlight_extensions.py, sentiment-api/app.py, sentiment-api/templates/feeds.html]
tech_stack:
  added: [functools.wraps in moodlight_extensions, flask.session in moodlight_extensions]
  patterns: [api_login_required gibt 401 JSON statt Redirect, GET-Endpoints öffentlich, POST/DELETE geschützt]
key_files:
  created: []
  modified:
    - sentiment-api/app.py
    - sentiment-api/moodlight_extensions.py
    - sentiment-api/templates/feeds.html
decisions:
  - api_login_required statt login_required für API-Endpoints — REST-Clients erwarten 401 JSON, kein HTML-Redirect
  - GET /api/moodlight/feeds bleibt öffentlich — Lesezugriff für ESP32 und Monitoring
  - Abmelden-Link mit float:right für minimale HTML-Änderung ohne Layout-Umbau
metrics:
  duration_seconds: 185
  completed_date: "2026-03-26"
  tasks_completed: 3
  tasks_total: 3
  files_modified: 3
---

# Phase 13 Plan 02: Auth-Decorator auf geschützte Routen Summary

**One-liner:** @login_required auf /feeds Route, api_login_required (401 JSON) auf POST/DELETE API-Endpunkte, Abmelden-Link in feeds.html Header.

## Was wurde implementiert

### Task 1: @login_required auf /feeds Route (app.py)

Die `/feeds`-Route wurde mit `@login_required` geschützt:

```python
@app.route('/feeds')
@login_required
def feed_management():
    """Feed-Verwaltungsseite — UI fuer /api/moodlight/feeds CRUD"""
    return render_template('feeds.html')
```

Unauthentifizierte GET-Anfragen an `/feeds` werden nun mit HTTP 302 auf `/login` weitergeleitet. Der `login_required` Decorator war bereits in Plan 01 in `app.py` definiert und war direkt verfügbar.

### Task 2: api_login_required Decorator in moodlight_extensions.py

**Imports erweitert:** `session` aus Flask und `wraps` aus functools hinzugefügt.

**Neuer Decorator** (vor `register_moodlight_endpoints`):

```python
def api_login_required(f):
    """Decorator für API-Endpoints: gibt 401 JSON zurück statt Redirect."""
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('authenticated'):
            return jsonify({"status": "error", "message": "Nicht authentifiziert"}), 401
        return f(*args, **kwargs)
    return decorated_function
```

**Geschützte Endpunkte (POST/DELETE):**
- `POST /api/moodlight/feeds` → `@api_login_required` → 401 ohne Session
- `DELETE /api/moodlight/feeds/<id>` → `@api_login_required` → 401 ohne Session
- `POST /api/moodlight/cache/clear` → `@api_login_required` → 401 ohne Session

**Öffentliche Endpunkte (unverändert, AUTH-03):**
- `GET /api/moodlight/current` — ESP32 Hauptendpunkt
- `GET /api/moodlight/history` — Verlaufsdaten
- `GET /api/moodlight/feeds` — Lesezugriff öffentlich
- `GET /api/moodlight/trend` — Trend-Analyse
- `GET /api/moodlight/stats` — Statistiken
- `GET /api/moodlight/devices` — Geräteliste

### Task 3: Abmelden-Link in feeds.html

CSS-Klasse `.logout-link` im `<style>`-Block ergänzt (nach `.back-link:hover`-Regel):
```css
.logout-link { float: right; /* + Stil konsistent mit back-link */ }
```

Link im `<header>`-Element eingefügt:
```html
<a href="/logout" class="logout-link">Abmelden</a>
```

## Entscheidungen

| Entscheidung | Begründung |
|---|---|
| Separater `api_login_required` statt `login_required` | REST-Clients (JavaScript fetch, curl) erwarten 401 JSON, kein HTML-Redirect |
| GET /api/moodlight/feeds öffentlich | ESP32-Geräte und Monitoring-Tools lesen Feeds ohne Session |
| `float: right` statt Flex-Container im Header | Minimale Änderung — kein Layout-Umbau, kein Risiko für bestehende Darstellung |

## Commits

| Task | Hash | Beschreibung |
|---|---|---|
| Task 1 | e93b19c | feat(13-02): @login_required auf /feeds Route anwenden |
| Task 2 | ad4c3d0 | feat(13-02): api_login_required Decorator — POST/DELETE Endpunkte schützen |
| Task 3 | 3522d48 | feat(13-02): Abmelden-Link im feeds.html Header |

## Deviations from Plan

None - Plan exactly as written executed.

## Known Stubs

None. Alle Auth-Decorator sind vollständig implementiert. Die Session-Validierung greift korrekt auf `session.get('authenticated')` zurück, das in Plan 01 gesetzt wird.

## Self-Check: PASSED

- sentiment-api/app.py enthält `@login_required` vor `feed_management` ✓
- sentiment-api/moodlight_extensions.py enthält `api_login_required` Decorator ✓
- sentiment-api/moodlight_extensions.py: POST + DELETE Endpoints geschützt ✓
- sentiment-api/moodlight_extensions.py: GET-Endpoints nicht geschützt ✓
- sentiment-api/templates/feeds.html enthält `/logout` und `Abmelden` ✓
- Commit e93b19c: vorhanden ✓
- Commit ad4c3d0: vorhanden ✓
- Commit 3522d48: vorhanden ✓
