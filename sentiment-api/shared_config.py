# sentiment-api/shared_config.py
# RSS_FEEDS wurde in Phase 9 entfernt — Feed-Liste liegt jetzt in PostgreSQL (feeds-Tabelle).
# get_sentiment_category() wird noch von app.py und background_worker.py genutzt — NICHT entfernen.


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
