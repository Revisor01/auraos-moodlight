# Gemeinsame Konstanten und Kategorie-Zuordnung der Sentiment-API.

# Redis-Keys zentral, damit Worker und Extensions nicht auseinanderdriften.
CACHE_KEY_CURRENT = 'moodlight:current:v2'
# Alter/Legacy-Key — wird beim Invalidieren mitgeloescht, falls noch irgendwo gesetzt
CACHE_KEY_CURRENT_LEGACY = 'moodlight:current'


def get_sentiment_category(score: float) -> str:
    """Bestimmt Sentiment-Kategorie aus Score. Einzige Definition im gesamten Projekt."""
    if score >= 0.30:
        return "sehr positiv"
    elif score >= 0.10:
        return "positiv"
    elif score >= -0.20:
        return "neutral"
    elif score >= -0.50:
        return "negativ"
    else:
        return "sehr negativ"
