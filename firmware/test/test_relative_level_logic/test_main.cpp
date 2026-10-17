// Native Tests fuer den HA-Sensor "Weltlage relativ": pio test -e native
#include <unity.h>
#include <string.h>
#include "../../src/relative_level_logic.h"

void setUp() {}
void tearDown() {}

// --- relativeLevelFromResponse ---

void test_led_index_vom_backend_wird_zur_stufe() {
    for (int i = 0; i <= 4; i++) {
        TEST_ASSERT_EQUAL_INT(i, relativeLevelFromResponse(true, i, false));
    }
}

void test_gemessener_fall_index_3_ist_stufe_3() {
    // Live 17.10.2026: led_index 3 (Blau-Lila), Kategorie "negativ"
    TEST_ASSERT_EQUAL_INT(3, relativeLevelFromResponse(true, 3, false));
}

void test_ohne_led_index_ist_stufe_unbekannt() {
    // Altes Backend: Farbe kommt aus dem lokalen mapSentimentToLED() — absolut, nicht relativ
    TEST_ASSERT_EQUAL_INT(RELATIVE_LEVEL_UNKNOWN, relativeLevelFromResponse(false, 2, false));
}

void test_fallback_schwellwerte_sind_nicht_relativ() {
    // Backend mit zu wenig Historie nutzt feste Schwellwerte — kein Wochenvergleich
    TEST_ASSERT_EQUAL_INT(RELATIVE_LEVEL_UNKNOWN, relativeLevelFromResponse(true, 3, true));
}

void test_index_ausserhalb_wird_wie_die_lampe_begrenzt() {
    // Die Lampe zeigt customColors[clamp(index)] — der Sensor muss dieselbe Stufe nennen
    TEST_ASSERT_EQUAL_INT(0, relativeLevelFromResponse(true, -2, false));
    TEST_ASSERT_EQUAL_INT(4, relativeLevelFromResponse(true, 7, false));
}

// --- relativeLevelText ---

void test_jede_stufe_hat_ihr_wort() {
    TEST_ASSERT_EQUAL_STRING("viel schlechter als die Woche", relativeLevelText(0));
    TEST_ASSERT_EQUAL_STRING("schlechter als die Woche", relativeLevelText(1));
    TEST_ASSERT_EQUAL_STRING("wie die Woche", relativeLevelText(2));
    TEST_ASSERT_EQUAL_STRING("besser als die Woche", relativeLevelText(3));
    TEST_ASSERT_EQUAL_STRING("viel besser als die Woche", relativeLevelText(4));
}

void test_unbekannte_stufe_behauptet_kein_wort() {
    TEST_ASSERT_EQUAL_STRING("unbekannt", relativeLevelText(RELATIVE_LEVEL_UNKNOWN));
    TEST_ASSERT_EQUAL_STRING("unbekannt", relativeLevelText(5));
    TEST_ASSERT_EQUAL_STRING("unbekannt", relativeLevelText(-7));
}

void test_vor_dem_ersten_abruf_unbekannt_nicht_default_index_2() {
    // AppState startet mit currentLedIndex = 2 — das darf nicht als "wie die Woche" erscheinen
    TEST_ASSERT_EQUAL_STRING("unbekannt", relativeLevelText(RELATIVE_LEVEL_INITIAL));
    TEST_ASSERT_EQUAL_INT(RELATIVE_LEVEL_UNKNOWN, RELATIVE_LEVEL_INITIAL);
}

// --- relativeLevelNumber (numerischer Sensor) ---

void test_numerischer_wert_je_stufe() {
    char buf[8];
    for (int i = 0; i <= 4; i++) {
        TEST_ASSERT_TRUE(relativeLevelNumber(i, buf, sizeof(buf)));
        char expected[2] = {(char)('0' + i), 0};
        TEST_ASSERT_EQUAL_STRING(expected, buf);
    }
}

void test_numerischer_wert_unbekannt_liefert_false() {
    // false -> HASensor::setValue(nullptr) -> "None" -> HA zeigt "unbekannt"
    char buf[8] = "x";
    TEST_ASSERT_FALSE(relativeLevelNumber(RELATIVE_LEVEL_UNKNOWN, buf, sizeof(buf)));
    TEST_ASSERT_FALSE(relativeLevelNumber(5, buf, sizeof(buf)));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_led_index_vom_backend_wird_zur_stufe);
    RUN_TEST(test_gemessener_fall_index_3_ist_stufe_3);
    RUN_TEST(test_ohne_led_index_ist_stufe_unbekannt);
    RUN_TEST(test_fallback_schwellwerte_sind_nicht_relativ);
    RUN_TEST(test_index_ausserhalb_wird_wie_die_lampe_begrenzt);
    RUN_TEST(test_jede_stufe_hat_ihr_wort);
    RUN_TEST(test_unbekannte_stufe_behauptet_kein_wort);
    RUN_TEST(test_vor_dem_ersten_abruf_unbekannt_nicht_default_index_2);
    RUN_TEST(test_numerischer_wert_je_stufe);
    RUN_TEST(test_numerischer_wert_unbekannt_liefert_false);
    return UNITY_END();
}
