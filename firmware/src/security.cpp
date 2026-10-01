// MotoWeather Bedside Display — Device credentials

#include "security.h"

String adminPassword = "";

String deviceDefaultPassword() {
    char buf[16];
    snprintf(buf, sizeof(buf), "moto%06x", (unsigned int)(ESP.getChipId() & 0xFFFFFF));
    return String(buf);
}

String effectivePassword() {
    return adminPassword.length() > 0 ? adminPassword : deviceDefaultPassword();
}

bool adminPasswordIsDefault() {
    return adminPassword.length() == 0;
}
