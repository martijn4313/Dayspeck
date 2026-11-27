// The energy monitor's JSON document (firmware/lib/energyjson), parsed as on the device: with the filter
#include <unity.h>
#include <string.h>
#include "energyjson.h"

void setUp() {}
void tearDown() {}

static bool parse(const char* json, EnergyData& e) {
    JsonDocument filter, doc;
    energyFilter(filter);
    if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return false;
    return energyParse(doc.as<JsonVariantConst>(), e);
}

static void test_full_document() {
    EnergyData e;
    TEST_ASSERT_TRUE(parse(R"({
        "heatpump": {"mode": "heating", "powerW": 850, "cop": 3.8, "flowC": 35.2, "returnC": 30.1,
                     "outsideC": -1.5, "hotWaterC": 48.2, "compressorHz": 42},
        "solar": {"powerW": 3240.4, "todayWh": 14800,
                  "hourlyWh": [0,0,0,0,0,0,80,420,1100,1900,2600,3100,3350,1080,0,0,0,0,0,0,0,0,0,0]},
        "grid": {"powerW": -2410},
        "house": {"powerW": 830},
        "inverter": {"serial": "skipped by the filter"}
    })", e));
    TEST_ASSERT_EQUAL(ENERGY_MODE_HEATING, e.mode);
    TEST_ASSERT_EQUAL(850, e.hpPowerW);
    TEST_ASSERT_EQUAL(38, e.hpCop10);
    TEST_ASSERT_EQUAL(352, e.flowC10);
    TEST_ASSERT_EQUAL(301, e.returnC10);
    TEST_ASSERT_EQUAL(-15, e.outsideC10);
    TEST_ASSERT_EQUAL(482, e.hotWaterC10);
    TEST_ASSERT_EQUAL(3240, e.pvPowerW);                      // decimals are rounded
    TEST_ASSERT_EQUAL(14800, e.pvTodayWh);
    TEST_ASSERT_EQUAL(-2410, e.gridW);
    TEST_ASSERT_EQUAL(830, e.houseW);
    TEST_ASSERT_TRUE(e.hasHours);
    TEST_ASSERT_EQUAL(3350, e.pvHourWh[12]);
    TEST_ASSERT_EQUAL(0, e.pvHourWh[23]);
}

static void test_missing_and_odd_fields() {
    EnergyData e;
    // Only solar, a mode of its own, a text where a number belongs, and too few hours
    TEST_ASSERT_TRUE(parse(R"({"heatpump": {"mode": "legionella-run", "powerW": "high"},
                               "solar": {"powerW": 0, "hourlyWh": [1, 2, 3]}})", e));
    TEST_ASSERT_EQUAL(ENERGY_MODE_OTHER, e.mode);
    TEST_ASSERT_EQUAL_STRING("legionella-", e.modeText);      // cut to fit
    TEST_ASSERT_EQUAL(ENERGY_NONE, e.hpPowerW);
    TEST_ASSERT_EQUAL(ENERGY_NONE, e.hpCop10);
    TEST_ASSERT_EQUAL(0, e.pvPowerW);
    TEST_ASSERT_EQUAL(ENERGY_NONE, e.gridW);
    TEST_ASSERT_FALSE(e.hasHours);

    TEST_ASSERT_TRUE(parse("{}", e));                         // nothing at all is still a document
    TEST_ASSERT_EQUAL(ENERGY_MODE_NONE, e.mode);
    TEST_ASSERT_FALSE(parse("[1, 2]", e));                    // not an object
    TEST_ASSERT_FALSE(parse("{\"heatpump\":", e));            // cut off
}

static void test_hours_are_clamped() {
    EnergyData e;
    TEST_ASSERT_TRUE(parse(R"({"solar": {"hourlyWh":
        [-5, 70000, 1.6, null, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]}})", e));
    TEST_ASSERT_TRUE(e.hasHours);
    TEST_ASSERT_EQUAL(0, e.pvHourWh[0]);
    TEST_ASSERT_EQUAL(65535, e.pvHourWh[1]);
    TEST_ASSERT_EQUAL(2, e.pvHourWh[2]);
    TEST_ASSERT_EQUAL(0, e.pvHourWh[3]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_full_document);
    RUN_TEST(test_missing_and_odd_fields);
    RUN_TEST(test_hours_are_clamped);
    return UNITY_END();
}
