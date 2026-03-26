---
phase: 13-authentifizierung
plan: "01"
subsystem: backend
tags: [auth, flask, session, login]
dependency_graph:
  requires: []
  provides: [login_required Decorator, /login Route, /logout Route, Session-Cookie-Auth]
  affects: [sentiment-api/app.py, sentiment-api/docker-compose.yaml]
tech_stack:
  added: [werkzeug.security, functools.wraps, flask.session, flask.redirect, flask.url_for, datetime.timedelta]
  patterns: [session-cookie auth, password hash on startup, decorator-based route protection]
key_files:
  created: [sentiment-api/templates/login.html]
  modified: [sentiment-api/app.py, sentiment-api/docker-compose.yaml]
decisions:
  - werkzeug.security statt eigenem Hash (Flask-integriert, keine neue Abhängigkeit)
  - Passwort-Hash einmalig beim App-Start berechnen (nicht bei jedem Request)
  - 24h Session-Timeout (timedelta + session.permanent = True)
  - Fallback-secret_key im Code für lokale Entwicklung, Warnung bei fehlendem ADMIN_PASSWORD
metrics:
  duration_seconds: 108
  completed_date: "2026-03-26"
  tasks_completed: 2
  tasks_total: 2
  files_modified: 3
---

# Phase 13 Plan 01: Auth-Infrastruktur Summary

**One-liner:** Flask-Session-Auth mit werkzeug.security Passwort-Hash, login_required Decorator und deutschem Login-Template.

## Was wurde implementiert

### Auth-Infrastruktur in app.py

- **Imports erweitert:** `session`, `redirect`, `url_for` aus Flask; `generate_password_hash`, `check_password_hash` aus `werkzeug.security`; `wraps` aus `functools`; `timedelta` aus `datetime`
- **app.secret_key** aus `SECRET_KEY` Umgebungsvariable gesetzt (Fallback: dev-String mit Warnung)
- **app.permanent_session_lifetime** auf 24 Stunden konfiguriert
- **_admin_password_hash** wird einmalig beim App-Start aus `ADMIN_PASSWORD` Umgebungsvariable berechnet; Warnung wenn nicht gesetzt
- **login_required Decorator** mit `@wraps(f)` — leitet auf `/login` weiter wenn `session['authenticated']` nicht gesetzt
- **GET/POST /login** — GET gibt Login-Formular zurück; POST prüft Passwort mit `check_password_hash`, setzt `session['authenticated'] = True`, leitet auf `/feeds` oder `?next=` weiter; falsches Passwort → 401 + Fehlermeldung
- **GET /logout** — löscht Session mit `session.clear()`, leitet auf `/login` weiter

### Login-Template (templates/login.html)

- CSS-Variablen aus `feeds.html` übernommen (`--primary: #8A2BE2`, `--secondary: #1E90FF` etc.)
- Zentriertes Card-Layout, kein JavaScript
- Deutsches UI: "Passwort", "Anmelden"-Button
- AuraOS-Branding in der Überschrift
- Fehlermeldung mit rotem Hinweis-Block wenn `error` gesetzt (Jinja2 `{% if error %}`)
- `autofocus` auf Passwort-Input für schnellen Zugriff

### docker-compose.yaml

- `ADMIN_PASSWORD=${ADMIN_PASSWORD}` im `news-analyzer` Service ergänzt
- `SECRET_KEY=${SECRET_KEY}` im `news-analyzer` Service ergänzt
- Konsistentes `${VAR}` Pattern wie bestehende Variablen

## Entscheidungen

| Entscheidung | Begründung |
|---|---|
| werkzeug.security statt eigenem Hash | Flask 3.1.0 nutzt Werkzeug — keine neue Abhängigkeit, battle-tested |
| Hash beim App-Start berechnen | Verhindert unnötige CPU-Last bei jedem Login-Request |
| 24h Session-Timeout | Angemessen für Privat-Backend ohne kritische Daten |
| `session.permanent = True` | Kombiniert mit `permanent_session_lifetime` für expiring cookies |
| `?next=` Redirect-Parameter | Standard-Flask-Muster für Login-Redirects |
| Kein `.env.example` Update | User setzt Werte selbst laut Plan-Constraint |

## Commits

| Task | Hash | Beschreibung |
|---|---|---|
| Task 1 | 2b2898e | feat(13-01): Auth-Infrastruktur — login_required, /login, /logout, login.html |
| Task 2 | 5b3ee9b | chore(13-01): ADMIN_PASSWORD und SECRET_KEY zu docker-compose.yaml |

## Deviations from Plan

None - Plan exactly as written executed.

## Known Stubs

None. Alle Login-Routen sind vollständig implementiert. Der `login_required` Decorator ist funktionsfähig. Die `/feeds`-Route ist noch ungeschützt — das ist Aufgabe von Plan 02 (Routen mit Decorator schützen).

## Self-Check: PASSED

- sentiment-api/app.py: vorhanden und modifiziert ✓
- sentiment-api/templates/login.html: erstellt ✓
- sentiment-api/docker-compose.yaml: vorhanden und modifiziert ✓
- Commit 2b2898e: vorhanden ✓
- Commit 5b3ee9b: vorhanden ✓
