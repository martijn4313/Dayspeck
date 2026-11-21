#include "energyjson.h"
#include <string.h>

void energyFilter(JsonDocument& filter) {
    JsonObject hp = filter["heatpump"].to<JsonObject>();
    for (const char* key : { "mode", "powerW", "cop", "flowC", "returnC", "outsideC", "hotWaterC" }) hp[key] = true;
    JsonObject pv = filter["solar"].to<JsonObject>();
    for (const char* key : { "powerW", "todayWh", "hourlyWh" }) pv[key] = true;
    filter["grid"]["powerW"] = true;
    filter["house"]["powerW"] = true;
}

// A number (whole or with decimals), rounded; ENERGY_NONE for anything else
static int32_t whole(JsonVariantConst v) {
    return v.is<float>() ? (int32_t)lroundf(v.as<float>()) : ENERGY_NONE;
}

static int32_t tenths(JsonVariantConst v) {
    return v.is<float>() ? energyTenths(v.as<float>()) : ENERGY_NONE;
}

bool energyParse(JsonVariantConst root, EnergyData& out) {
    energyClear(out);
    if (!root.is<JsonObjectConst>()) return false;
    JsonObjectConst hp = root["heatpump"], pv = root["solar"];
    const char* mode = hp["mode"];
    out.mode = (uint8_t)energyModeFromName(mode);
    if (out.mode == ENERGY_MODE_OTHER) {
        strncpy(out.modeText, mode, ENERGY_MODE_TEXT_LEN - 1);
        out.modeText[ENERGY_MODE_TEXT_LEN - 1] = '\0';
    }
    out.hpPowerW = whole(hp["powerW"]);
    out.hpCop10 = tenths(hp["cop"]);
    out.flowC10 = tenths(hp["flowC"]);
    out.returnC10 = tenths(hp["returnC"]);
    out.outsideC10 = tenths(hp["outsideC"]);
    out.hotWaterC10 = tenths(hp["hotWaterC"]);
    out.pvPowerW = whole(pv["powerW"]);
    out.pvTodayWh = whole(pv["todayWh"]);
    out.gridW = whole(root["grid"]["powerW"]);
    out.houseW = whole(root["house"]["powerW"]);
    JsonArrayConst hours = pv["hourlyWh"];
    if (hours.size() == 24) {
        out.hasHours = true;
        for (size_t i = 0; i < 24; i++) {
            int32_t wh = whole(hours[i]);
            out.pvHourWh[i] = wh == ENERGY_NONE || wh < 0 ? 0 : (wh > 65535 ? 65535 : (uint16_t)wh);
        }
    }
    return true;
}
