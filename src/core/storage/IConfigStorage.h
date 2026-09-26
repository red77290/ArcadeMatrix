/**
 * @file IConfigStorage.h
 * @brief Abstract persistence interface for configuration storage.
 *
 * Decouples configuration loading/saving from physical hardware (SdFat / SPI),
 * enabling deterministic native testing and atomic, crash-resilient file writes.
 */
#pragma once

#include <Arduino.h>
#include <vector>

class IConfigStorage {
public:
    virtual ~IConfigStorage() = default;

    virtual bool exists(const char* path) = 0;
    virtual bool readString(const char* path, String& outStr) = 0;
    virtual bool writeStringAtomic(const char* path, const String& content) = 0;
    virtual bool remove(const char* path) = 0;
    virtual bool mkdir(const char* path) = 0;
    virtual bool listFiles(const char* dirPath, std::vector<String>& outFiles) = 0;
};
