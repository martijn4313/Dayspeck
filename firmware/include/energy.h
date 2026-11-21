/**
 * Energy monitor: the heat pump, solar panels and grid, read over HTTP from a JSON document on the local
 * network (docs/energy.md), for the heatpump, solar and energy screens
 */
#ifndef ENERGY_H
#define ENERGY_H

#include <Arduino.h>
#include "motologic.h"

extern String energyUrl;   // config.json energyUrl; empty = not set up

// Read the document when it is due: only while `showing` (an energy screen is on), every ENERGY_REFRESH_MS
// (ENERGY_RETRY_MS after a failure). Blocks for up to ENERGY_TIMEOUT_MS. True when new data arrived.
bool energyFetchIfDue(bool showing);
void energyForget();                        // the address changed: drop the data, read again when shown

const EnergyData& energyData();
bool          energyFresh();                // data that is at most ENERGY_STALE_MS old
unsigned long energyAgeMs();                // since the last good read; ULONG_MAX without one
const char*   energyLastError();            // the last failed read in words, "" after a good one

#endif // ENERGY_H
