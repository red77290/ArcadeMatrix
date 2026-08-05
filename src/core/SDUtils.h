#pragma once
#include <Arduino.h>
#include "HardwareProfile.h"

#if USE_SD_MMC
#include <SD_MMC.h>
#define sd SD_MMC
typedef fs::File FsFile;
#define FILE_OPEN_READ "r"
#define FILE_OPEN_WRITE "w"
#else
#include <SdFat.h>
extern SdFs sd;
#define FILE_OPEN_READ O_READ
#define FILE_OPEN_WRITE (O_WRITE | O_CREAT | O_TRUNC)
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


/**
 * Returns true if the filename is a macOS system file that should be ignored:
 * - ._filename  (resource fork/metadata)
 * - .DS_Store
 * - .Spotlight-*
 * - .Trashes
 */
inline bool isMacJunk(const String& name) {
    if (name.length() == 0) return true;
    // Get just the filename part (after last '/')
    int lastSlash = name.lastIndexOf('/');
    String basename = (lastSlash >= 0) ? name.substring(lastSlash + 1) : name;
    if (basename.startsWith("._")) return true;
    if (basename == ".DS_Store") return true;
    if (basename.startsWith(".Spotlight")) return true;
    if (basename == ".Trashes") return true;
    if (basename.startsWith(".")) return true; // catch-all for hidden files
    return false;
}
