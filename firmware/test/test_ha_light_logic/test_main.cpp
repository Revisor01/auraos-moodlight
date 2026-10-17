// Native Tests fuer die HA-Lichtlogik: pio test -e native
#include <unity.h>
#include "../../src/ha_light_logic.h"

static const uint32_t COLORS[5] = {0xFF0000, 0xFFA500, 0x1E90FF, 0x545DF0, 0x8A2BE2};
static const uint32_t WARMWEISS = 0xFFD6AB;  // 255,214,171 — live gemessen in HA

void setUp() {}
void tearDown() {}

// --- computeShownLight ---

void test_auto_zeigt_farbe_der_weltlage_nicht_manualColor() {
    // Gemessener Fehler: Auto + "sehr negativ" (Index 0), HA zeigte die Warmweiss-manualColor
    ShownLight s = computeShownLight(true, 0, COLORS, WARMWEISS, 120, 255);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, s.color);
}

void test_auto_jeder_index_liefert_seine_farbe() {
    for (int i = 0; i < 5; i++) {
        ShownLight s = computeShownLight(true, i, COLORS, WARMWEISS, 120, 255);
        TEST_ASSERT_EQUAL_HEX32(COLORS[i], s.color);
    }
}

void test_auto_zeigt_auto_helligkeit_nicht_manualBrightness() {
    ShownLight s = computeShownLight(true, 2, COLORS, WARMWEISS, 120, 255);
    TEST_ASSERT_EQUAL_UINT8(255, s.brightness);
}

void test_auto_index_ausserhalb_wird_begrenzt() {
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, computeShownLight(true, -3, COLORS, WARMWEISS, 120, 255).color);
    TEST_ASSERT_EQUAL_HEX32(0x8A2BE2, computeShownLight(true, 9, COLORS, WARMWEISS, 120, 255).color);
}

void test_manual_zeigt_manualColor_und_manualBrightness() {
    ShownLight s = computeShownLight(false, 0, COLORS, WARMWEISS, 120, 255);
    TEST_ASSERT_EQUAL_HEX32(WARMWEISS, s.color);
    TEST_ASSERT_EQUAL_UINT8(120, s.brightness);
}

void test_farbe_wird_auf_24_bit_maskiert() {
    uint32_t colors[5] = {0xAAFF0000, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, computeShownLight(true, 0, colors, 0, 1, 255).color);
    TEST_ASSERT_EQUAL_HEX32(0x123456, computeShownLight(false, 0, colors, 0xFF123456, 1, 255).color);
}

// --- decideColorCommand ---

void test_farbwahl_im_auto_modus_schaltet_auf_manual() {
    TEST_ASSERT_EQUAL(ColorCommandAction::SwitchToManualAndApply,
                      decideColorCommand(true, false, 0x00FF00, WARMWEISS));
}

void test_farbwahl_gleich_manualColor_im_auto_modus_schaltet_trotzdem() {
    // Darf nicht vom "gleiche Farbe -> return" verschluckt werden
    TEST_ASSERT_EQUAL(ColorCommandAction::SwitchToManualAndApply,
                      decideColorCommand(true, false, WARMWEISS, WARMWEISS));
}

void test_farbwahl_im_manual_modus_wird_angewendet() {
    TEST_ASSERT_EQUAL(ColorCommandAction::Apply,
                      decideColorCommand(false, false, 0x00FF00, WARMWEISS));
}

void test_gleiche_farbe_im_manual_modus_wird_ignoriert() {
    TEST_ASSERT_EQUAL(ColorCommandAction::Ignore,
                      decideColorCommand(false, false, WARMWEISS, WARMWEISS));
}

void test_startphase_auto_modus_schaltet_nicht_um() {
    // Wie onModeCommand: in der Startphase keine Moduswechsel (Schutz vor veralteten Befehlen)
    TEST_ASSERT_EQUAL(ColorCommandAction::StoreOnly,
                      decideColorCommand(true, true, 0x00FF00, WARMWEISS));
    TEST_ASSERT_EQUAL(ColorCommandAction::Ignore,
                      decideColorCommand(true, true, WARMWEISS, WARMWEISS));
}

void test_startphase_manual_modus_wendet_an() {
    TEST_ASSERT_EQUAL(ColorCommandAction::Apply,
                      decideColorCommand(false, true, 0x00FF00, WARMWEISS));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_auto_zeigt_farbe_der_weltlage_nicht_manualColor);
    RUN_TEST(test_auto_jeder_index_liefert_seine_farbe);
    RUN_TEST(test_auto_zeigt_auto_helligkeit_nicht_manualBrightness);
    RUN_TEST(test_auto_index_ausserhalb_wird_begrenzt);
    RUN_TEST(test_manual_zeigt_manualColor_und_manualBrightness);
    RUN_TEST(test_farbe_wird_auf_24_bit_maskiert);
    RUN_TEST(test_farbwahl_im_auto_modus_schaltet_auf_manual);
    RUN_TEST(test_farbwahl_gleich_manualColor_im_auto_modus_schaltet_trotzdem);
    RUN_TEST(test_farbwahl_im_manual_modus_wird_angewendet);
    RUN_TEST(test_gleiche_farbe_im_manual_modus_wird_ignoriert);
    RUN_TEST(test_startphase_auto_modus_schaltet_nicht_um);
    RUN_TEST(test_startphase_manual_modus_wendet_an);
    return UNITY_END();
}
