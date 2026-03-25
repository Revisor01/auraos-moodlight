# sentiment-api/shared_config.py
# Einzige Quelle fuer RSS-Feed-Liste und Sentiment-Kategorisierung

RSS_FEEDS = {
    "Zeit": "https://newsfeed.zeit.de/index",
    "Tagesschau": "https://www.tagesschau.de/xml/rss2",
    "Sueddeutsche": "https://rss.sueddeutsche.de/rss/Alles",
    "FAZ": "https://www.faz.net/rss/aktuell/",
    "Die Welt": "https://www.welt.de/feeds/latest.rss",
    "Handelsblatt": "https://www.handelsblatt.com/contentexport/feed/schlagzeilen",
    "n-tv": "https://www.n-tv.de/rss",
    "Focus": "https://rss.focus.de/fol/XML/rss_folnews.xml",
    "Stern": "https://www.stern.de/feed/standard/alle-nachrichten/",
    "Telekom": "https://www.t-online.de/feed.rss",
    "TAZ": "https://taz.de/!p4608;rss/",
    "Deutschlandfunk": "https://www.deutschlandfunk.de/nachrichten-100.rss"
}


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
