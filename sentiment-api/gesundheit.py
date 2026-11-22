"""Frische der letzten Analyse für den Health-Check."""

from datetime import datetime, timezone
from typing import Any, Dict, Optional

# Drei verpasste Läufe, mindestens zwei Stunden: ein einzelner Feed-Ausfall soll nicht alarmieren.
INTERVALLE_TOLERANZ = 3
MINDEST_TOLERANZ_S = 2 * 3600


def analyse_frische(letzte: Optional[datetime], intervall_s: int, jetzt: Optional[datetime] = None) -> Dict[str, Any]:
    """Bewertet, ob die letzte gespeicherte Analyse jünger ist als die erlaubte Grenze."""
    jetzt = jetzt or datetime.now(timezone.utc)
    grenze_s = max(INTERVALLE_TOLERANZ * int(intervall_s), MINDEST_TOLERANZ_S)
    ergebnis: Dict[str, Any] = {
        "status": "degraded",
        "last_analysis": None,
        "age_minutes": None,
        "max_age_minutes": grenze_s // 60,
    }
    if letzte is None:
        return ergebnis
    if letzte.tzinfo is None:
        letzte = letzte.replace(tzinfo=timezone.utc)
    alter_s = (jetzt - letzte).total_seconds()
    ergebnis["last_analysis"] = letzte.isoformat()
    ergebnis["age_minutes"] = int(alter_s // 60)
    ergebnis["status"] = "healthy" if alter_s <= grenze_s else "unhealthy"
    return ergebnis
