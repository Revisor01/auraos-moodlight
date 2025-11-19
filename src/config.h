#ifndef CONFIG_H
#define CONFIG_H

// Versionierung
#define MOODLIGHT_VERSION "9.0"
#define MOODLIGHT_NAME "AuraOS"
#define MOODLIGHT_FULL_VERSION MOODLIGHT_VERSION " - " MOODLIGHT_NAME

// Standardwerte für Einstellungen (nur hier definieren)
#define DEFAULT_LED_PIN 26
#define DEFAULT_DHT_PIN 17
#define DEFAULT_NUM_LEDS 12
#define DEFAULT_LED_BRIGHTNESS 255

#define DEFAULT_AP_NAME "Moodlight-Setup"
#define DEFAULT_AP_PASSWORD ""

#define DEFAULT_WAVE_DURATION 10000
#define DEFAULT_WAVE_MIN_BRIGHTNESS 20
#define DEFAULT_WAVE_MAX_BRIGHTNESS 255

#define DEFAULT_MOOD_UPDATE_INTERVAL 1800000 // 30 Minuten
#define DEFAULT_DHT_READ_INTERVAL 300000     // 5 Minuten

// API Endpoints (v9.0: Optimized cached endpoints)
#define DEFAULT_NEWS_API_URL "https://analyse.godsapp.de/api/moodlight/current"
#define DEFAULT_STATS_API_URL "https://analyse.godsapp.de/api/moodlight/history"

#endif // CONFIG_H