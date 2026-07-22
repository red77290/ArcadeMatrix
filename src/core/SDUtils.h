#pragma once
#include <Arduino.h>
#include <SdFat.h>

extern SdFs sd;

inline String getFileName(FsFile& f) {
    char buf[256];
    f.getName(buf, sizeof(buf));
    return String(buf);
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
