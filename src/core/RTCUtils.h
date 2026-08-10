#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <time.h>

#if defined(USE_RTC) && USE_RTC

#define PCF85063_ADDRESS 0x51
#define PCF85063_CTRL1_REG 0x00
#define PCF85063_SEC_REG 0x04

inline uint8_t decToBcd(uint8_t val) { return ((val / 10 * 16) + (val % 10)); }
inline uint8_t bcdToDec(uint8_t val) { return ((val / 16 * 10) + (val % 16)); }

inline bool initRTC() {
    Wire.beginTransmission(PCF85063_ADDRESS);
    Wire.write(PCF85063_CTRL1_REG);
    Wire.write(0x00); // Normal operation, 12.5pF
    Wire.write(0x00); // CTRL2 default
    return (Wire.endTransmission() == 0);
}

inline bool readRTC(struct tm& timeinfo) {
    Wire.beginTransmission(PCF85063_ADDRESS);
    Wire.write(PCF85063_SEC_REG);
    if (Wire.endTransmission() != 0) return false;

    if (Wire.requestFrom((uint16_t)PCF85063_ADDRESS, (uint8_t)7) != 7) {
        return false;
    }

    uint8_t sec = Wire.read() & 0x7F; // mask out OS bit
    uint8_t min = Wire.read() & 0x7F;
    uint8_t hr  = Wire.read() & 0x3F;
    uint8_t day = Wire.read() & 0x3F;
    uint8_t wd  = Wire.read() & 0x07;
    uint8_t mon = Wire.read() & 0x1F;
    uint8_t yr  = Wire.read();

    timeinfo.tm_sec  = bcdToDec(sec);
    timeinfo.tm_min  = bcdToDec(min);
    timeinfo.tm_hour = bcdToDec(hr);
    timeinfo.tm_mday = bcdToDec(day);
    timeinfo.tm_wday = wd;
    timeinfo.tm_mon  = bcdToDec(mon) - 1; // tm_mon is 0-11
    timeinfo.tm_year = bcdToDec(yr) + 100; // tm_year is years since 1900

    return true;
}

inline bool writeRTC(const struct tm& timeinfo) {
    Wire.beginTransmission(PCF85063_ADDRESS);
    Wire.write(PCF85063_SEC_REG);
    
    Wire.write(decToBcd(timeinfo.tm_sec));
    Wire.write(decToBcd(timeinfo.tm_min));
    Wire.write(decToBcd(timeinfo.tm_hour));
    Wire.write(decToBcd(timeinfo.tm_mday));
    Wire.write(timeinfo.tm_wday);
    Wire.write(decToBcd(timeinfo.tm_mon + 1));
    Wire.write(decToBcd(timeinfo.tm_year % 100)); // assumes 20xx

    return (Wire.endTransmission() == 0);
}

#endif
