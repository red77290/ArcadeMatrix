#pragma once
#include <Arduino.h>

/**
 * @file TimingSafe.h
 * @brief Constant-time comparison utility to prevent timing side-channel attacks.
 */
class TimingSafe {
public:
    /**
     * @brief Performs a constant-time comparison of two strings.
     *
     * Iterates over max(lenA, lenB) without early termination, accumulating
     * byte differences in a volatile accumulator to defeat timing attacks.
     *
     * @param a First string.
     * @param b Second string.
     * @return true if strings are identical in length and content, false otherwise.
     */
    static inline bool compare(const String& a, const String& b) {
        size_t lenA = a.length();
        size_t lenB = b.length();
        volatile uint8_t diff = (lenA == lenB) ? 0 : 1;
        size_t maxLen = (lenA > lenB) ? lenA : lenB;
        for (size_t i = 0; i < maxLen; ++i) {
            char ca = (i < lenA) ? a[i] : 0;
            char cb = (i < lenB) ? b[i] : 0;
            diff |= (uint8_t)(ca ^ cb);
        }
        return diff == 0;
    }
};
