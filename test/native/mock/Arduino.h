#ifndef ARDUINO_MOCK_H
#define ARDUINO_MOCK_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cctype>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>

typedef bool boolean;
typedef uint8_t byte;

// Virtual clock for deterministic testing, initialized to real clock
inline uint64_t& getVirtualTimeOffset() {
    static uint64_t offset = 0;
    return offset;
}

inline unsigned long millis() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    return static_cast<unsigned long>(elapsed + getVirtualTimeOffset());
}

inline void advanceVirtualMillis(unsigned long ms) {
    getVirtualTimeOffset() += ms;
}

inline void resetVirtualClock() {
    getVirtualTimeOffset() = 0;
}

inline unsigned long micros() {
    static auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
    return static_cast<unsigned long>(elapsed + getVirtualTimeOffset() * 1000);
}

inline void delay(unsigned long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

inline void delayMicroseconds(unsigned int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

class String {
public:
    String() = default;
    String(const char* s) : _str(s ? s : "") {}
    String(const std::string& s) : _str(s) {}
    String(char c) : _str(1, c) {}
    String(int val) : _str(std::to_string(val)) {}
    String(unsigned int val) : _str(std::to_string(val)) {}
    String(long val) : _str(std::to_string(val)) {}
    String(unsigned long val) : _str(std::to_string(val)) {}
    String(float val, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, val);
        _str = buf;
    }
    String(double val, int decimals = 2) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.*f", decimals, val);
        _str = buf;
    }

    size_t length() const { return _str.length(); }
    const char* c_str() const { return _str.c_str(); }
    char charAt(size_t index) const { return index < _str.length() ? _str[index] : 0; }
    char operator[](size_t index) const { return charAt(index); }
    char& operator[](size_t index) { return _str[index]; }

    int indexOf(char c, size_t fromIndex = 0) const {
        size_t pos = _str.find(c, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    int indexOf(const String& s, size_t fromIndex = 0) const {
        size_t pos = _str.find(s._str, fromIndex);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    int lastIndexOf(char c) const {
        size_t pos = _str.rfind(c);
        return (pos == std::string::npos) ? -1 : static_cast<int>(pos);
    }

    String substring(size_t start, size_t end = (size_t)-1) const {
        if (start >= _str.length()) return String("");
        if (end == (size_t)-1 || end > _str.length()) end = _str.length();
        if (end <= start) return String("");
        return String(_str.substr(start, end - start));
    }

    void toLowerCase() {
        std::transform(_str.begin(), _str.end(), _str.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
    }

    void toUpperCase() {
        std::transform(_str.begin(), _str.end(), _str.begin(), [](unsigned char c) {
            return std::toupper(c);
        });
    }

    bool equalsIgnoreCase(const String& other) const {
        if (_str.length() != other._str.length()) return false;
        for (size_t i = 0; i < _str.length(); ++i) {
            if (std::tolower(static_cast<unsigned char>(_str[i])) !=
                std::tolower(static_cast<unsigned char>(other._str[i]))) {
                return false;
            }
        }
        return true;
    }

    bool startsWith(const String& prefix) const {
        if (prefix.length() > _str.length()) return false;
        return _str.compare(0, prefix.length(), prefix._str) == 0;
    }

    bool endsWith(const String& suffix) const {
        if (suffix.length() > _str.length()) return false;
        return _str.compare(_str.length() - suffix.length(), suffix.length(), suffix._str) == 0;
    }

    void trim() {
        size_t first = _str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            _str.clear();
            return;
        }
        size_t last = _str.find_last_not_of(" \t\r\n");
        _str = _str.substr(first, (last - first + 1));
    }

    int toInt() const {
        return _str.empty() ? 0 : std::atoi(_str.c_str());
    }

    float toFloat() const {
        return _str.empty() ? 0.0f : static_cast<float>(std::atof(_str.c_str()));
    }

    String& operator+=(const String& other) {
        _str += other._str;
        return *this;
    }

    String& operator+=(const char* s) {
        if (s) _str += s;
        return *this;
    }

    String& operator+=(char c) {
        _str += c;
        return *this;
    }

    bool operator==(const String& other) const { return _str == other._str; }
    bool operator==(const char* s) const { return _str == (s ? s : ""); }
    bool operator!=(const String& other) const { return _str != other._str; }
    bool operator!=(const char* s) const { return _str != (s ? s : ""); }
    bool operator<(const String& other) const { return _str < other._str; }

    const std::string& stdStr() const { return _str; }

private:
    std::string _str;
};

inline String operator+(const String& a, const String& b) {
    String res = a;
    res += b;
    return res;
}

inline String operator+(const String& a, const char* b) {
    String res = a;
    res += b;
    return res;
}

inline String operator+(const char* a, const String& b) {
    String res(a);
    res += b;
    return res;
}

class HardwareSerial {
public:
    void begin(unsigned long) {}
    void print(const char* s) { if (s) ::printf("%s", s); }
    void print(const String& s) { ::printf("%s", s.c_str()); }
    void print(int val) { ::printf("%d", val); }
    void println(const char* s = "") { if (s) ::printf("%s\n", s); else ::printf("\n"); }
    void println(const String& s) { ::printf("%s\n", s.c_str()); }
    void println(int val) { ::printf("%d\n", val); }
    
    template<typename... Args>
    void printf(const char* fmt, Args... args) {
        ::printf(fmt, args...);
    }
};

extern HardwareSerial Serial;

// FreeRTOS stubs
inline uint32_t xPortGetCoreID() { return 0; }
inline int xTaskNotifyGive(void*) { return 1; }
inline void* xTaskGetCurrentTaskHandle() { return nullptr; }
inline uint32_t ulTaskNotifyTake(int, uint32_t) { return 1; }

#endif // ARDUINO_MOCK_H
