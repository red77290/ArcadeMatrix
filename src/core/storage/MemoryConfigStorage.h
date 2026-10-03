/**
 * @file MemoryConfigStorage.h
 * @brief In-memory mock implementation of IConfigStorage for unit testing.
 */
#pragma once

#include "IConfigStorage.h"
#include <map>

class MemoryConfigStorage : public IConfigStorage {
public:
    virtual ~MemoryConfigStorage() = default;

    bool exists(const char* path) override {
        return _files.find(String(path)) != _files.end();
    }

    bool readString(const char* path, String& outStr) override {
        auto it = _files.find(String(path));
        if (it == _files.end()) return false;
        outStr = it->second;
        return true;
    }

    bool writeStringAtomic(const char* path, const String& content) override {
        _files[String(path)] = content;
        return true;
    }

    bool remove(const char* path) override {
        _files.erase(String(path));
        return true;
    }

    bool mkdir(const char* path) override {
        (void)path;
        return true;
    }

    bool listFiles(const char* dirPath, std::vector<String>& outFiles) override {
        outFiles.clear();
        String prefix = String(dirPath);
        if (!prefix.endsWith("/")) prefix += "/";

        for (const auto& kv : _files) {
            if (kv.first.startsWith(prefix)) {
                String sub = kv.first.substring(prefix.length());
                if (sub.indexOf('/') < 0) {
                    outFiles.push_back(sub);
                }
            }
        }
        return true;
    }

    void clear() {
        _files.clear();
    }

private:
    std::map<String, String> _files;
};
