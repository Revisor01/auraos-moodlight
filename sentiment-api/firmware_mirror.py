# -*- coding: utf-8 -*-
"""
Firmware-Spiegel für OTA-Updates

Die Lampe kommt nicht an GitHub heran: dort liegt alles hinter HTTPS mit zwei
Redirects über wechselnde Hosts und signierten URLs von mehreren hundert Zeichen.
Ein ESP32 mit 320 KB RAM kann das zwar prinzipiell, aber jeder TLS-Handshake und
jede Zertifikatskette ist eine Fehlerquelle in genau dem Pfad, der ein kaputtes
Gerät retten soll.

Deshalb dieser Spiegel: Die CI meldet ein neues Release, das Backend lädt Binary
und UI-Archiv einmal herunter und legt sie ab. Die Lampe fragt per einfachem HTTP
nach und lädt von hier — ohne TLS, ohne Redirects, ohne Zertifikate.

Zwischen Spiegel und Gerät steht die Freigabe: Ein gespiegeltes Release ist
zunächst nur vorhanden, nicht ausgeliefert. Erst wenn es im Admin freigegeben
wird, sehen die Geräte es überhaupt. So lässt sich eine Version erst an einem
Gerät testen, bevor sie auf alle geht.

Integration in app.py:
    from firmware_mirror import register_firmware_endpoints
    register_firmware_endpoints(app)
"""

import hashlib
import json
import logging
import os
import re
import secrets
import shutil
import tempfile
from datetime import datetime, timezone
from pathlib import Path

import requests as http_requests
from flask import Response, jsonify, request, send_file

from database import get_database

logger = logging.getLogger(__name__)

# Wo die gespiegelten Dateien liegen. Als Volume gemountet, damit ein
# Container-Neustart die Binaries nicht verliert.
MIRROR_DIR = Path(os.getenv('FIRMWARE_MIRROR_DIR', '/data/firmware'))

# Das Repository, aus dem gespiegelt wird.
GITHUB_REPO = os.getenv('FIRMWARE_GITHUB_REPO', 'Revisor01/auraos-moodlight')

# Token, mit dem sich die CI beim Sync-Endpoint ausweist. Ohne gesetzten Token
# ist der Endpoint gesperrt — sonst könnte jeder einen Spiegel-Lauf auslösen.
SYNC_TOKEN = os.getenv('FIRMWARE_SYNC_TOKEN', '').strip()

# Obergrenze pro Datei. Ein AuraOS-Binary liegt bei ~1,3 MB; alles jenseits
# davon deutet auf ein falsches Asset hin und würde nur Plattenplatz fressen.
MAX_ASSET_BYTES = 8 * 1024 * 1024

# Settings-Keys in der Datenbank
KEY_RELEASED = 'firmware_released_version'   # Was die Geräte bekommen dürfen
KEY_LATEST_MIRRORED = 'firmware_latest_mirrored'  # Was zuletzt gespiegelt wurde

# Version im Format 9.16 — bewusst eng, weil daraus Dateinamen gebaut werden
VERSION_RE = re.compile(r'^\d+\.\d+$')


def _parse_version(value: str):
    """
    Zerlegt "9.16" in (9, 16) für den Größenvergleich.

    String-Vergleich reicht hier nicht: "9.9" > "9.16" wäre lexikografisch wahr,
    numerisch aber falsch. Genau dieser Fall tritt in diesem Projekt auf, weil
    der Nummernkreis bei 9.9 vorbei an 9.10 weiterlief.
    """
    if not value or not VERSION_RE.match(value.strip()):
        return None
    major, minor = value.strip().split('.')
    return int(major), int(minor)


def _is_newer(candidate: str, current: str) -> bool:
    """Ist candidate eine höhere Version als current?"""
    c = _parse_version(candidate)
    n = _parse_version(current)
    if c is None:
        return False
    if n is None:
        # Gerät meldet keine oder eine unlesbare Version — dann gilt jedes
        # gespiegelte Release als neuer, sonst käme so ein Gerät nie an ein Update
        return True
    return c > n


def _mirror_paths(version: str):
    """Dateipfade eines gespiegelten Release."""
    base = MIRROR_DIR / version
    return {
        'dir': base,
        'firmware': base / f'Firmware-{version}-AuraOS.bin',
        'ui': base / f'UI-{version}-AuraOS.tgz',
        'meta': base / 'release.json',
    }


def _read_meta(version: str):
    """Metadaten eines gespiegelten Release lesen, None wenn nicht vorhanden."""
    meta_path = _mirror_paths(version)['meta']
    if not meta_path.is_file():
        return None
    try:
        with open(meta_path, 'r', encoding='utf-8') as fh:
            return json.load(fh)
    except (OSError, ValueError) as exc:
        logger.error(f"Firmware-Metadaten fuer {version} unlesbar: {exc}")
        return None


def _download_asset(url: str, target: Path) -> tuple:
    """
    Lädt ein Release-Asset von GitHub und legt es unter target ab.

    Erst in eine temporäre Datei, dann verschieben: Bricht der Download ab,
    bleibt keine halbe Datei liegen, die später als gültige Firmware
    ausgeliefert würde.

    Returns:
        (sha256, groesse) bei Erfolg, (None, Fehlermeldung) sonst.
    """
    tmp_fd, tmp_name = tempfile.mkstemp(dir=str(target.parent), suffix='.part')
    os.close(tmp_fd)
    tmp_path = Path(tmp_name)

    try:
        with http_requests.get(url, timeout=60, stream=True,
                               headers={'Accept': 'application/octet-stream'}) as resp:
            if resp.status_code != 200:
                return None, f"GitHub antwortete mit HTTP {resp.status_code}"

            digest = hashlib.sha256()
            written = 0
            with open(tmp_path, 'wb') as fh:
                for chunk in resp.iter_content(chunk_size=64 * 1024):
                    if not chunk:
                        continue
                    written += len(chunk)
                    if written > MAX_ASSET_BYTES:
                        return None, (f"Asset groesser als erlaubt "
                                      f"({MAX_ASSET_BYTES // 1024 // 1024} MB)")
                    digest.update(chunk)
                    fh.write(chunk)

        if written == 0:
            return None, "Asset ist leer"

        tmp_path.replace(target)
        return digest.hexdigest(), written

    except http_requests.RequestException as exc:
        return None, f"Download fehlgeschlagen: {exc}"
    finally:
        if tmp_path.exists():
            try:
                tmp_path.unlink()
            except OSError:
                pass


def _verify_firmware_magic(path: Path) -> bool:
    """
    Prüft das ESP32-Magic-Byte 0xE9 am Dateianfang.

    Dieselbe Prüfung macht die Firmware vor dem Flashen noch einmal. Hier
    verhindert sie, dass ein falsches Asset überhaupt in den Spiegel gelangt.
    """
    try:
        with open(path, 'rb') as fh:
            return fh.read(1) == b'\xe9'
    except OSError:
        return False


def register_firmware_endpoints(app):
    """Registriert die Firmware-Endpoints an der Flask-App."""

    # api_login_required lebt in moodlight_extensions; hier importiert, um einen
    # Zirkelbezug beim Modulimport zu vermeiden
    from moodlight_extensions import api_login_required

    MIRROR_DIR.mkdir(parents=True, exist_ok=True)

    @app.route('/api/firmware/sync', methods=['POST'])
    def firmware_sync():
        """
        Spiegelt ein GitHub-Release. Wird von der CI nach einem Build aufgerufen.

        Geschützt über FIRMWARE_SYNC_TOKEN im X-Sync-Token-Header. Ein
        gespiegeltes Release ist danach vorhanden, aber noch nicht freigegeben —
        die Geräte sehen es erst nach /api/firmware/release.

        Body (optional): {"version": "9.16"} — ohne Angabe wird das
        aktuellste Release des Repos gespiegelt.
        """
        if not SYNC_TOKEN:
            logger.error("Firmware-Sync angefragt, aber FIRMWARE_SYNC_TOKEN ist nicht gesetzt")
            return jsonify({
                "status": "error",
                "message": "Sync ist nicht konfiguriert"
            }), 503

        provided = request.headers.get('X-Sync-Token', '')
        # compare_digest statt == : verhindert, dass die Antwortzeit verrät,
        # wie viele Zeichen des Tokens stimmen
        if not secrets.compare_digest(provided, SYNC_TOKEN):
            logger.warning(f"Firmware-Sync mit falschem Token von {request.remote_addr}")
            return jsonify({"status": "error", "message": "Nicht autorisiert"}), 401

        data = request.get_json(silent=True) or {}
        requested = str(data.get('version', '')).strip()

        if requested and not VERSION_RE.match(requested):
            return jsonify({
                "status": "error",
                "message": "version muss die Form 9.16 haben"
            }), 400

        # Release-Metadaten bei GitHub holen
        if requested:
            api_url = f'https://api.github.com/repos/{GITHUB_REPO}/releases/tags/v{requested}'
        else:
            api_url = f'https://api.github.com/repos/{GITHUB_REPO}/releases/latest'

        try:
            resp = http_requests.get(
                api_url, timeout=20,
                headers={'Accept': 'application/vnd.github+json'}
            )
        except http_requests.RequestException as exc:
            logger.error(f"GitHub-API nicht erreichbar: {exc}")
            return jsonify({"status": "error", "message": "GitHub nicht erreichbar"}), 502

        if resp.status_code != 200:
            logger.error(f"GitHub-API antwortete mit HTTP {resp.status_code}")
            return jsonify({
                "status": "error",
                "message": f"Release nicht gefunden (HTTP {resp.status_code})"
            }), 404

        release = resp.json()
        tag = str(release.get('tag_name', '')).strip()
        version = tag[1:] if tag.startswith('v') else tag

        if not VERSION_RE.match(version):
            # site-2026-08-01 und ähnliche Tags sind keine Firmware-Releases
            return jsonify({
                "status": "skipped",
                "message": f"Tag {tag} ist kein Firmware-Release"
            }), 200

        paths = _mirror_paths(version)
        paths['dir'].mkdir(parents=True, exist_ok=True)

        # Passende Assets im Release suchen
        assets = {a.get('name', ''): a for a in release.get('assets', [])}
        fw_name = f'Firmware-{version}-AuraOS.bin'
        ui_name = f'UI-{version}-AuraOS.tgz'

        if fw_name not in assets:
            return jsonify({
                "status": "error",
                "message": f"{fw_name} fehlt im Release"
            }), 404

        # Firmware laden und prüfen
        fw_sha, fw_size = _download_asset(
            assets[fw_name].get('browser_download_url', ''), paths['firmware']
        )
        if fw_sha is None:
            logger.error(f"Firmware {version} nicht gespiegelt: {fw_size}")
            return jsonify({"status": "error", "message": str(fw_size)}), 502

        if not _verify_firmware_magic(paths['firmware']):
            paths['firmware'].unlink(missing_ok=True)
            logger.error(f"Firmware {version} hat kein ESP32-Magic-Byte — verworfen")
            return jsonify({
                "status": "error",
                "message": "Datei ist keine ESP32-Firmware (Magic-Byte fehlt)"
            }), 400

        # UI-Archiv ist optional — ein reines Firmware-Release ist zulässig
        ui_sha, ui_size = None, 0
        if ui_name in assets:
            ui_sha, ui_size = _download_asset(
                assets[ui_name].get('browser_download_url', ''), paths['ui']
            )
            if ui_sha is None:
                logger.warning(f"UI-Archiv {version} nicht gespiegelt: {ui_size}")
                ui_sha, ui_size = None, 0

        meta = {
            'version': version,
            'tag': tag,
            'release_url': release.get('html_url', ''),
            'published_at': release.get('published_at', ''),
            'mirrored_at': datetime.now(timezone.utc).isoformat(),
            'firmware': {'name': fw_name, 'size': fw_size, 'sha256': fw_sha},
            'ui': ({'name': ui_name, 'size': ui_size, 'sha256': ui_sha}
                   if ui_sha else None),
        }

        try:
            with open(paths['meta'], 'w', encoding='utf-8') as fh:
                json.dump(meta, fh, ensure_ascii=False, indent=2)
        except OSError as exc:
            logger.error(f"Firmware-Metadaten {version} nicht schreibbar: {exc}")
            return jsonify({"status": "error", "message": "Spiegel nicht schreibbar"}), 500

        db = get_database()
        db.set_setting(KEY_LATEST_MIRRORED, version)

        logger.info(f"Firmware {version} gespiegelt ({fw_size} Bytes) — noch nicht freigegeben")
        return jsonify({
            "status": "success",
            "version": version,
            "released": False,
            "message": f"Version {version} gespiegelt. Freigabe im Admin noetig."
        }), 200

    @app.route('/api/firmware/latest', methods=['GET'])
    def firmware_latest():
        """
        Was die Lampe stündlich fragt.

        Antwortet nur mit freigegebenen Versionen — ein gespiegeltes, aber nicht
        freigegebenes Release ist hier unsichtbar.

        Query: ?current=9.16 (aktuelle Version des Geräts)
        """
        current = str(request.args.get('current', '')).strip()

        db = get_database()
        released = (db.get_setting(KEY_RELEASED) or '').strip()

        if not released:
            return jsonify({
                "update_available": False,
                "current": current,
                "message": "Keine Version freigegeben"
            }), 200

        meta = _read_meta(released)
        if meta is None:
            logger.error(f"Freigegebene Version {released} fehlt im Spiegel")
            return jsonify({
                "update_available": False,
                "current": current,
                "message": "Freigegebene Version nicht im Spiegel"
            }), 200

        available = _is_newer(released, current)

        payload = {
            "update_available": available,
            "current": current,
            "latest": released,
            # Die Lampe zeigt keine Release-Notes an, sondern verlinkt hierhin —
            # spart RAM auf dem Gerät und die Notes bleiben vollständig lesbar
            "release_url": meta.get('release_url', ''),
        }

        if available:
            fw = meta.get('firmware') or {}
            payload["firmware_url"] = f"/api/firmware/download/{released}/firmware"
            payload["firmware_size"] = fw.get('size', 0)
            payload["firmware_sha256"] = fw.get('sha256', '')
            if meta.get('ui'):
                payload["ui_url"] = f"/api/firmware/download/{released}/ui"
                payload["ui_size"] = (meta.get('ui') or {}).get('size', 0)

        return jsonify(payload), 200

    @app.route('/api/firmware/download/<version>/<kind>', methods=['GET'])
    def firmware_download(version, kind):
        """
        Liefert Binary oder UI-Archiv per HTTP aus — das lädt die Lampe.

        Nur die freigegebene Version ist abrufbar. Sonst könnte ein Gerät eine
        zurückgezogene Version nachladen, weil es die alte URL noch kennt.
        """
        if not VERSION_RE.match(version or ''):
            return jsonify({"status": "error", "message": "Ungueltige Version"}), 400

        if kind not in ('firmware', 'ui'):
            return jsonify({"status": "error", "message": "Unbekannter Typ"}), 400

        db = get_database()
        released = (db.get_setting(KEY_RELEASED) or '').strip()
        if version != released:
            return jsonify({
                "status": "error",
                "message": "Diese Version ist nicht freigegeben"
            }), 403

        paths = _mirror_paths(version)
        target = paths['firmware'] if kind == 'firmware' else paths['ui']

        if not target.is_file():
            return jsonify({"status": "error", "message": "Datei nicht im Spiegel"}), 404

        return send_file(
            str(target),
            mimetype='application/octet-stream',
            as_attachment=True,
            download_name=target.name,
            conditional=True,
        )

    @app.route('/api/firmware/mirrored', methods=['GET'])
    @api_login_required
    def firmware_mirrored():
        """Alle gespiegelten Versionen — für die Admin-Oberfläche."""
        db = get_database()
        released = (db.get_setting(KEY_RELEASED) or '').strip()

        versions = []
        if MIRROR_DIR.is_dir():
            for entry in sorted(MIRROR_DIR.iterdir()):
                if not entry.is_dir() or not VERSION_RE.match(entry.name):
                    continue
                meta = _read_meta(entry.name)
                if meta is None:
                    continue
                versions.append({
                    "version": entry.name,
                    "released": entry.name == released,
                    "mirrored_at": meta.get('mirrored_at', ''),
                    "release_url": meta.get('release_url', ''),
                    "firmware_size": (meta.get('firmware') or {}).get('size', 0),
                    "has_ui": bool(meta.get('ui')),
                })

        versions.sort(key=lambda v: _parse_version(v['version']) or (0, 0), reverse=True)

        return jsonify({
            "status": "success",
            "released": released or None,
            "versions": versions,
        }), 200

    @app.route('/api/firmware/release', methods=['POST'])
    @api_login_required
    def firmware_release():
        """
        Gibt eine gespiegelte Version für die Geräte frei — oder zieht sie zurück.

        Das ist der Punkt, an dem du entscheidest: erst am eigenen Gerät testen,
        dann freigeben. Vorher sieht keine Lampe die neue Version.

        Body: {"version": "9.16"} zum Freigeben,
              {"version": null} zum Zurückziehen.
        """
        data = request.get_json(silent=True) or {}
        raw = data.get('version')

        db = get_database()

        # Zurückziehen
        if raw is None or str(raw).strip() == '':
            db.set_setting(KEY_RELEASED, '')
            logger.info("Firmware-Freigabe zurueckgezogen")
            return jsonify({
                "status": "success",
                "released": None,
                "message": "Freigabe zurueckgezogen — Geraete erhalten keine Updates mehr"
            }), 200

        version = str(raw).strip()
        if not VERSION_RE.match(version):
            return jsonify({
                "status": "error",
                "message": "version muss die Form 9.16 haben"
            }), 400

        paths = _mirror_paths(version)
        if not paths['firmware'].is_file():
            return jsonify({
                "status": "error",
                "message": f"Version {version} ist nicht gespiegelt"
            }), 404

        # Vor der Freigabe noch einmal prüfen — die Datei könnte seit dem
        # Spiegeln beschädigt worden sein
        if not _verify_firmware_magic(paths['firmware']):
            logger.error(f"Freigabe {version} abgelehnt: Magic-Byte fehlt")
            return jsonify({
                "status": "error",
                "message": "Gespiegelte Datei ist keine gueltige ESP32-Firmware"
            }), 400

        db.set_setting(KEY_RELEASED, version)
        logger.info(f"Firmware {version} fuer alle Geraete freigegeben")

        return jsonify({
            "status": "success",
            "released": version,
            "message": f"Version {version} ist freigegeben"
        }), 200

    logger.info(f"Firmware-Endpoints registriert (Spiegel: {MIRROR_DIR})")
