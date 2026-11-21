// Dayspeck — the energy monitor's JSON document (docs/energy.md) into EnergyData.
// No Arduino dependencies beyond ArduinoJson: unit tested natively (test/test_energy).

#ifndef ENERGYJSON_H
#define ENERGYJSON_H

#include <ArduinoJson.h>
#include "motologic.h"

// The fields the display uses, to parse the document with (DeserializationOption::Filter): the rest is skipped
void energyFilter(JsonDocument& filter);

// Fill `out` from the parsed document; what is missing (or not a number) is ENERGY_NONE. False when the
// document is not an object.
bool energyParse(JsonVariantConst root, EnergyData& out);

#endif // ENERGYJSON_H
