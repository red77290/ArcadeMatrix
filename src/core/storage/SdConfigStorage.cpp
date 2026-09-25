/**
 * @file SdConfigStorage.cpp
 * @brief Implementation of SdConfigStorage using unified SDUtils.
 */
#include "SdConfigStorage.h"
#include "../Logger.h"

SdConfigStorage::SdConfigStorage()
{
}

bool SdConfigStorage::exists(const char* path) {
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) return false;
    return sd.exists(path);
}

bool SdConfigStorage::readString(const char* path, String& outStr) {
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) {
        LOGE("SdConfigStorage", "Cannot read %s: SD lock timeout", path);
        return false;
    }
    if (!sd.exists(path)) return false;

    FsFile f = sd.open(path, FILE_OPEN_READ);
    if (!f) {
        LOGE("SdConfigStorage", "Failed to open %s for reading", path);
        return false;
    }

    size_t sz = f.size();
    if (sz == 0) {
        outStr = "";
        f.close();
        return true;
    }

    std::vector<char> buf(sz + 1);
    size_t readBytes = f.read(reinterpret_cast<uint8_t*>(buf.data()), sz);
    f.close();

    if (readBytes != sz) {
        LOGE("SdConfigStorage", "Short read on %s: expected %u, got %u", path, (unsigned)sz, (unsigned)readBytes);
        return false;
    }

    buf[sz] = '\0';
    outStr = buf.data();
    return true;
}

bool SdConfigStorage::writeStringAtomic(const char* path, const String& content) {
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) {
        LOGE("SdConfigStorage", "Cannot write %s: SD lock timeout", path);
        return false;
    }

    String tmpPath = String(path) + ".tmp";

    // 1. Write to temporary file
    FsFile f = sd.open(tmpPath.c_str(), FILE_OPEN_WRITE);
    if (!f) {
        LOGE("SdConfigStorage", "Failed to open temporary file %s", tmpPath.c_str());
        return false;
    }

    size_t len = content.length();
    size_t written = f.write(reinterpret_cast<const uint8_t*>(content.c_str()), len);
    f.flush();
    f.close();

    if (written != len) {
        LOGE("SdConfigStorage", "Short write to %s: expected %u, got %u", tmpPath.c_str(), (unsigned)len, (unsigned)written);
        sd.remove(tmpPath.c_str());
        return false;
    }

    // 2. Remove destination file if already exists
    if (sd.exists(path)) {
        sd.remove(path);
    }

    // 3. Rename temporary file to target path
#if USE_SD_MMC
    // On SD_MMC / FatFS, if rename is not supported or fails, read-copy-delete fallback
    if (!sd.rename(tmpPath.c_str(), path)) {
        // Fallback: copy content
        FsFile src = sd.open(tmpPath.c_str(), FILE_OPEN_READ);
        FsFile dst = sd.open(path, FILE_OPEN_WRITE);
        if (src && dst) {
            uint8_t copyBuf[256];
            int n;
            while ((n = src.read(copyBuf, sizeof(copyBuf))) > 0) {
                dst.write(copyBuf, n);
            }
            dst.flush();
            dst.close();
            src.close();
            sd.remove(tmpPath.c_str());
        } else {
            LOGE("SdConfigStorage", "Failed to atomic-commit %s", path);
            sd.remove(tmpPath.c_str());
            return false;
        }
    }
#else
    if (!sd.rename(tmpPath.c_str(), path)) {
        LOGE("SdConfigStorage", "Failed to rename %s to %s", tmpPath.c_str(), path);
        sd.remove(tmpPath.c_str());
        return false;
    }
#endif

    return true;
}

bool SdConfigStorage::remove(const char* path) {
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) return false;
    if (!sd.exists(path)) return true;
    return sd.remove(path);
}

bool SdConfigStorage::mkdir(const char* path) {
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) return false;
    if (sd.exists(path)) return true;
    return sd.mkdir(path);
}

bool SdConfigStorage::listFiles(const char* dirPath, std::vector<String>& outFiles) {
    outFiles.clear();
    SdLockGuard guard(pdMS_TO_TICKS(1500));
    if (!guard) return false;

    FsFile dir = sd.open(dirPath, FILE_OPEN_READ);
    if (!dir || !isDirectory(dir)) return false;

    FsFile entry;
    while (getNextFile(dir, entry)) {
        if (!isDirectory(entry)) {
            String name = getFileName(entry);
            if (!isMacJunk(name)) {
                outFiles.push_back(name);
            }
        }
        entry.close();
    }
    dir.close();
    return true;
}
