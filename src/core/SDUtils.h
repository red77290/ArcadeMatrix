#pragma once
#include <Arduino.h>
#include "HardwareProfile.h"

#if USE_SD_MMC
#include <SD_MMC.h>
#define sd SD_MMC
typedef fs::File FsFile;
#define FILE_OPEN_READ "r"
#define FILE_OPEN_WRITE "w"
#define FILE_OPEN_APPEND "a"
#else
#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif
#include <SdFat.h>
#ifdef FILE_READ
#undef FILE_READ
#endif
#ifdef FILE_WRITE
#undef FILE_WRITE
#endif
extern SdFs sd;
#define FILE_OPEN_READ O_READ
#define FILE_OPEN_WRITE (O_WRITE | O_CREAT | O_TRUNC)
#define FILE_OPEN_APPEND (O_WRITE | O_CREAT | O_APPEND)
#endif

inline bool isDirectory(FsFile& f) {
#if USE_SD_MMC
    return f.isDirectory();
#else
    return f.isDir();
#endif
}

inline String getFileName(FsFile& f) {
#if USE_SD_MMC
    return String(f.name());
#else
    char buf[256];
    f.getName(buf, sizeof(buf));
    return String(buf);
#endif
}

inline bool getFileNameBuffer(FsFile& f, char* outBuf, size_t outSize) {
    if (!outBuf || outSize == 0) return false;
    outBuf[0] = '\0';
#if USE_SD_MMC
    const char* n = f.name();
    if (!n) return false;
    strncpy(outBuf, n, outSize - 1);
    outBuf[outSize - 1] = '\0';
    return true;
#else
    return f.getName(outBuf, outSize);
#endif
}


/**
 * Returns true if the filename is a macOS system file that should be ignored:
 * - ._filename  (resource fork/metadata)
 * - .DS_Store
 * - .Spotlight-*
 * - .Trashes
 */
inline bool getNextFile(FsFile& dir, FsFile& file) {
#if USE_SD_MMC
    file = dir.openNextFile();
    return (bool)file;
#else
    if (file) file.close();
    return file.openNext(&dir, O_READ);
#endif
}

inline bool isMacJunk(const char* name) {
    if (!name || name[0] == '\0') return true;
    const char* lastSlash = strrchr(name, '/');
    const char* basename = lastSlash ? (lastSlash + 1) : name;
    if (basename[0] == '.') return true; // covers ._, .DS_Store, .Spotlight, .Trashes, hidden files
    return false;
}

inline bool isMacJunk(const String& name) {
    return isMacJunk(name.c_str());
}
