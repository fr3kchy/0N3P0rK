#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

namespace SDLog {
inline void log(const char* tag, const char* fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    Serial.printf("[%s] %s\n", tag ? tag : "LOG", buf);
}
}
