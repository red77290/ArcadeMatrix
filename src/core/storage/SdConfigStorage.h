/**
 * @file SdConfigStorage.h
 * @brief SD Card implementation of IConfigStorage using unified SDUtils across classic ESP32 and S3 Waveshare.
 */
#pragma once

#include "IConfigStorage.h"
#include "../SdLockGuard.h"
#include "../SDUtils.h"

class SdConfigStorage : public IConfigStorage {
public:
    SdConfigStorage();
    virtual ~SdConfigStorage() = default;

    bool exists(const char* path) override;
    bool readString(const char* path, String& outStr) override;
    bool writeStringAtomic(const char* path, const String& content) override;
    bool remove(const char* path) override;
    bool mkdir(const char* path) override;
    bool listFiles(const char* dirPath, std::vector<String>& outFiles) override;
};
