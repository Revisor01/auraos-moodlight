#ifndef CONFIG_H
#define CONFIG_H

// Versionierung
#define MOODLIGHT_VERSION "9.10"
#define MOODLIGHT_NAME "AuraOS"
#define MOODLIGHT_FULL_VERSION MOODLIGHT_VERSION " - " MOODLIGHT_NAME

// Standardwerte für Einstellungen (nur hier definieren)
#define DEFAULT_LED_PIN 26
#define DEFAULT_DHT_PIN 17
#define DEFAULT_NUM_LEDS 12
#define MAX_LEDS 64
#define DEFAULT_LED_BRIGHTNESS 255

#define DEFAULT_AP_NAME "Moodlight-Setup"
#define DEFAULT_AP_PASSWORD ""

#define DEFAULT_WAVE_DURATION 10000
#define DEFAULT_WAVE_MIN_BRIGHTNESS 20
#define DEFAULT_WAVE_MAX_BRIGHTNESS 255

#define DEFAULT_MOOD_UPDATE_INTERVAL 1800000 // 30 Minuten
#define DEFAULT_DHT_READ_INTERVAL 300000     // 5 Minuten

// API Endpoints
#define DEFAULT_NEWS_API_URL "http://analyse.godsapp.de/api/moodlight/current"
#define DEFAULT_STATS_API_URL "http://analyse.godsapp.de/api/moodlight/history"

// Netzwerk
#define DNS_PORT 53
#define LOG_BUFFER_SIZE 20                    // Groesse des Ringpuffers

// Timing: Startup & Boot
#define STARTUP_GRACE_PERIOD 15000            // 15s Grace Period nach Boot
#define SETUP_HARDWARE_DELAY 200              // 200ms Hardware-Stabilisierung

// Timing: WiFi & Verbindung
#define MAX_RECONNECT_DELAY 300000            // 5 Minuten max WiFi Reconnect
#define STATUS_LED_GRACE_MS 30000             // 30s bevor Status-LED aktiviert
#define AP_TIMEOUT 300000                     // 5 Minuten AP-Modus Timeout

// Timing: MQTT
#define MQTT_HEARTBEAT_INTERVAL 60000         // 1 Minute
#define MQTT_CONNECT_TIMEOUT 5000             // 5s MQTT Verbindungs-Timeout
#define MQTT_INIT_DELAY 500                   // 500ms vor MQTT-Init

// Timing: Sentiment
#define SENTIMENT_FALLBACK_TIMEOUT 3600000    // 1 Stunde ohne Update -> Fallback
#define MAX_SENTIMENT_FAILURES 5

// Timing: System
#define STATUS_LOG_INTERVAL 300000            // 5 Minuten
#define REBOOT_DELAY 5000                     // 5s bis Reboot
#define HEALTH_CHECK_INTERVAL 3600000         // 1 Stunde
#define HEALTH_CHECK_SHORT_INTERVAL 300000    // 5 Minuten (loop sysHealth.update)

// Timing: Loop
#define LOOP_SERVER_HANDLE_MS 20              // Webserver-Handle Intervall
#define LOOP_CONNECTION_CHECK_MS 2000         // WiFi/MQTT Verbindungscheck
#define LOOP_MQTT_INTERVAL_MS 100             // MQTT loop Intervall
#define LOOP_DELAY_MS 20                      // Loop-Ende Pause
#define LOOP_PULSE_UPDATE_MS 30               // Pulsier-Update Intervall
#define SETTINGS_SAVE_DEBOUNCE_MS 2000        // Einstellungen Speicher-Debounce

// System Health
#define SYSSTAT_FILE_ROTATION 24              // Anzahl rotierender Statistikdateien
#define STORAGE_WARNING_PERCENT 85            // Dateisystem-Warnschwelle
#define NIGHT_REBOOT_HOUR_START 2             // Nachtstunden-Neustart ab
#define NIGHT_REBOOT_HOUR_END 4              // Nachtstunden-Neustart bis
#define SCHEDULED_REBOOT_HOUR_START 3        // Geplanter Neustart ab
#define SCHEDULED_REBOOT_HOUR_END 4          // Geplanter Neustart bis
#define NIGHT_REBOOT_DELAY 60000             // 1 Minute Verzögerung für Sofort-Neustart
#define SCHEDULED_REBOOT_DELAY 30000         // 30s Verzögerung für geplanten Neustart

// NTP
#define NTP_SERVER "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC 3600
#define NTP_DAYLIGHT_OFFSET_SEC 3600

#endif // CONFIG_H
