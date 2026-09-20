#include "WebServerAPI.h"
#include "../engines/FighterEngine.h"
#include "../core/RenderStats.h"
#include <core/EngineRegistry.h>
#include <ArduinoJson.h>
#include <memory>
#include "../core/SDUtils.h"
#include <functional>
#include <Update.h>
#include <esp_ota_ops.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "../core/MatrixEngine.h"
#include "../engines/GifEngine.h"
#include "../core/RotationManager.h"
#include "WebUI.h"
#include "../core/Globals.h"
#include "../core/Logger.h"
#include "../core/BuildInfo.h"
#include "../core/ConfigSanitizer.h"
#include "../engines/EngineRegistrar.h"
#include "../core/AudioHub.h"
#include "../services/WebRadioService.h"
#include "../services/BluetoothAudioService.h"
#include "../services/DLNAService.h"
#include "../core/DisplayOrientationManager.h"
#include "../hal/GyroHAL.h"
#include <atomic>
#include "../core/SdLockGuard.h"

// A config change is applied in memory first and then written to the card. When the write fails (the
// card is busy with a rescan or upload, or the write itself fails) the change would silently vanish at
// the next reboot, so every handler that saves reports that with a 503 instead of a plain success.
static void sendConfigSaveFailed(AsyncWebServerRequest* request) {
    request->send(503, "application/json",
        "{\"success\":false,\"sd_saved\":false,\"error\":\"Change applied but could not be written to the SD card (busy or write failed). It will be lost at reboot - retry in a moment.\"}");
}

extern RotationManager* rotationManager;
extern GifEngine* gifEngine;

// In-RAM cache for /api/playlists. Enumerating the GIF folders costs many SD round-trips; doing it
// on every request keeps sdMutex held on the AsyncTCP task and stalls the whole HTTP server, which
// is what made the WebUI Display tab take tens of seconds on first connection.
// Index 0 = combined, 1 = yoko, 2 = tate. GIF folders are managed out of band (SD card swap, host
// copy), so the cache self-expires after a TTL and can be bypassed explicitly with ?refresh=1.
static const uint32_t PLAYLIST_CACHE_TTL_MS = 30000;
static String s_playlistCache[3];
static uint32_t s_playlistCacheStamp[3] = { 0, 0, 0 };
static bool s_playlistCacheValid[3] = { false, false, false };

static void invalidatePlaylistCache() {
    for (int i = 0; i < 3; ++i) {
        s_playlistCache[i] = String();
        s_playlistCacheStamp[i] = 0;
        s_playlistCacheValid[i] = false;
    }
}

// ---------------------------------------------------------------------------
// /api/engines incremental serializer
//
// Emits the descriptor array one element at a time so that peak heap usage stays proportional to
// the largest single descriptor instead of the whole payload. See the route handler for the two
// regressions this replaces.
// ---------------------------------------------------------------------------
struct EngineStreamState {
    size_t descriptorIndex = 0;
    String pending;
    size_t offset = 0;
    bool arrayOpened = false;
    bool arrayClosed = false;
};

static void serializeEngineDescriptor(const EngineDescriptor& desc, String& out) {
    const size_t fieldCount = desc.schema.fields.size();
    // ConfigField members are all `const char*`, so ArduinoJson links them instead of copying:
    // only structural slots consume capacity. A field object emits at most 12 members (id,
    // field_type, label, description, default_value, options, options_endpoint, multiple,
    // visible_when, min_val, max_val, step) - under-counting here silently truncates the schema
    // of the largest engines, which is exactly what the WebUI reported as an empty response.
    const size_t capacity = JSON_OBJECT_SIZE(8)            // root
                          + JSON_OBJECT_SIZE(4)            // metadata
                          + JSON_OBJECT_SIZE(5)            // capabilities
                          + JSON_OBJECT_SIZE(6)            // requirements
                          + JSON_ARRAY_SIZE(fieldCount)    // schema array
                          + fieldCount * JSON_OBJECT_SIZE(12)
                          + 512;                           // headroom

    SpiRamJsonDocument doc(capacity);
    JsonObject obj = doc.to<JsonObject>();

    JsonObject metaObj = obj.createNestedObject("metadata");
    metaObj["id"] = desc.metadata.id;
    metaObj["name"] = desc.metadata.name;
    metaObj["category"] = desc.metadata.category;
    metaObj["version"] = desc.metadata.version;

    JsonObject capObj = obj.createNestedObject("capabilities");
    capObj["supports_128x32"] = desc.capabilities.supports_128x32;
    capObj["supports_256x64"] = desc.capabilities.supports_256x64;
    capObj["realtime"] = desc.capabilities.realtime;
    capObj["interruptible"] = desc.capabilities.interruptible;
    capObj["selfPaced"] = desc.capabilities.selfPaced;

    JsonObject reqObj = obj.createNestedObject("requirements");
    reqObj["needs_psram"] = desc.requirements.needsPsram;
    reqObj["needs_audio"] = desc.requirements.needsAudio;
    reqObj["needs_temp_sensor"] = desc.requirements.needsTempSensor;
    reqObj["needs_gyroscope"] = desc.requirements.needsGyroscope;
    reqObj["needs_network"] = desc.requirements.needsNetwork;
    reqObj["needs_sd"] = desc.requirements.needsSd;

    auto reqCheck = EngineRegistrar::checkRequirements(desc.requirements);
    obj["available"] = reqCheck.satisfied;
    if (!reqCheck.satisfied) {
        obj["reason"] = reqCheck.reason;
    }

    JsonArray schema = obj.createNestedArray("schema");
    for (const auto& field : desc.schema.fields) {
        JsonObject fieldObj = schema.createNestedObject();
        fieldObj["id"] = field.id;
        fieldObj["field_type"] = (int)field.type;
        fieldObj["label"] = field.label;
        fieldObj["description"] = field.description;
        fieldObj["default_value"] = field.default_value;
        if (strlen(field.options) > 0) fieldObj["options"] = field.options;
        if (strlen(field.options_endpoint) > 0) fieldObj["options_endpoint"] = field.options_endpoint;
        if (field.multiple) fieldObj["multiple"] = true;
        if (strlen(field.visible_when) > 0) fieldObj["visible_when"] = field.visible_when;
        if (strlen(field.min_val) > 0) fieldObj["min_val"] = field.min_val;
        if (strlen(field.max_val) > 0) fieldObj["max_val"] = field.max_val;
        if (strlen(field.step) > 0) fieldObj["step"] = field.step;
    }

    if (doc.overflowed()) {
        LOGE("WebServer", "Engine descriptor %s overflowed its %u byte document; schema truncated.",
             desc.metadata.id ? desc.metadata.id : "?", (unsigned)capacity);
    }

    out = String();
    serializeJson(doc, out);
}

/**
 * @brief Produce the next JSON fragment of the /api/engines array.
 * @return false once the closing bracket has already been emitted.
 */
static bool refillEngineStream(EngineStreamState& state) {
    size_t count = 0;
    const EngineDescriptor* descriptors = EngineRegistry::getAllDescriptors(count);

    state.offset = 0;

    if (!state.arrayOpened) {
        state.arrayOpened = true;
        state.pending = "[";
        return true;
    }

    if (descriptors && state.descriptorIndex < count) {
        String body;
        serializeEngineDescriptor(descriptors[state.descriptorIndex], body);
        state.pending = (state.descriptorIndex > 0) ? "," : "";
        state.pending += body;
        state.descriptorIndex++;
        return true;
    }

    if (!state.arrayClosed) {
        state.arrayClosed = true;
        state.pending = "]";
        return true;
    }

    state.pending = String();
    return false;
}

// ---------------------------------------------------------------------------
// /api/instances incremental serializer (same rationale as /api/engines)
// ---------------------------------------------------------------------------
struct InstanceStreamState {
    size_t instanceIndex = 0;
    String pending;
    size_t offset = 0;
    bool arrayOpened = false;
    bool arrayClosed = false;
};

static bool refillInstanceStream(InstanceStreamState& state) {
    extern ConfigLoader config;

    // Safe to read config.instances directly across successive filler calls because every mutation
    // of that vector happens either at boot (AppRuntime) or inside a request handler, i.e. on the
    // very same AsyncTCP task that drives this filler. Moving config mutation to another task would
    // require snapshotting the instance ids here first.
    state.offset = 0;

    if (!state.arrayOpened) {
        state.arrayOpened = true;
        state.pending = "[";
        return true;
    }

    if (state.instanceIndex < config.instances.size()) {
        const auto& inst = config.instances[state.instanceIndex];
        const auto& dict = inst.config.getDictionary();

        // Instance config keys and values are Strings, which ArduinoJson duplicates into the
        // document, so their byte length must be accounted for on top of the structural slots.
        size_t stringBytes = inst.instance_id.length() + inst.engine_id.length() + 2;
        for (const auto& kv : dict) {
            stringBytes += kv.first.length() + kv.second.length() + 2;
        }
        const size_t capacity = JSON_OBJECT_SIZE(3)
                              + JSON_OBJECT_SIZE(dict.size())
                              + stringBytes
                              + 256;

        SpiRamJsonDocument doc(capacity);
        JsonObject obj = doc.to<JsonObject>();
        obj["instance_id"] = inst.instance_id;
        obj["engine_id"] = inst.engine_id;
        JsonObject cfgObj = obj.createNestedObject("config");
        for (const auto& kv : dict) {
            cfgObj[kv.first] = kv.second;
        }

        if (doc.overflowed()) {
            LOGE("WebServer", "Instance %s overflowed its %u byte document; config truncated.",
                 inst.instance_id.c_str(), (unsigned)capacity);
        }

        String body;
        serializeJson(doc, body);
        state.pending = (state.instanceIndex > 0) ? "," : "";
        state.pending += body;
        state.instanceIndex++;
        return true;
    }

    if (!state.arrayClosed) {
        state.arrayClosed = true;
        state.pending = "]";
        return true;
    }

    state.pending = String();
    return false;
}

// Helper class to stream large files from SdFat to ESPAsyncWebServer
class AsyncSdFatResponse : public AsyncAbstractResponse {
private:
    FsFile _content;
public:
    AsyncSdFatResponse(const String& path, const String& contentType) {
        _code = 200;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            _content = sd.open(path.c_str(), FILE_OPEN_READ);
            if (_content) _contentLength = _content.size();
            else _contentLength = 0;
            xSemaphoreGive(sdMutex);
        } else {
            _contentLength = 0;
        }
        _contentType = contentType;
    }
    ~AsyncSdFatResponse() {
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if(_content) _content.close();
            xSemaphoreGive(sdMutex);
        }
    }
    size_t _fillBuffer(uint8_t *buf, size_t maxLen) override {
        size_t bytesRead = 0;
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            if (_content && _content.available()) {
                // SdSpiConfig and DMA SPI transfers expect 32-bit aligned memory located in internal RAM.
                // The buffer passed by ESPAsyncWebServer (`buf`) is sometimes allocated dynamically by AsyncTCP
                // on PSRAM or non-word-aligned boundaries, causing SdFat SPI read to crash with a LoadProhibited/StoreProhibited panic.
                // We bounce reads through a dedicated word-aligned heap buffer if direct DMA isn't guaranteed.
                uint8_t* bounceBuf = (uint8_t*)heap_caps_malloc(4096, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                if (bounceBuf) {
                    size_t toRead = (maxLen > 4096) ? 4096 : maxLen;
                    size_t offset = 0;
                    while (toRead > 0) {
                        size_t chunk = (toRead > 512) ? 512 : toRead;
                        size_t r = _content.read(bounceBuf, chunk);
                        if (r > 0) memcpy(buf + offset, bounceBuf, r);
                        if (r == 0) break;
                        offset += r;
                        toRead -= r;
                        bytesRead += r;
                    }
                    heap_caps_free(bounceBuf);
                } else {
                    // Fallback if DMA memory is completely exhausted (might crash, but at least we tried)
                    bytesRead = _content.read(buf, maxLen);
                }
            }
            xSemaphoreGive(sdMutex);
        }
        return bytesRead;
    }
};


WebServerAPI::WebServerAPI(uint16_t port, MessageEngine* msgEngine) : server(port), msg(msgEngine) {}

void WebServerAPI::setMarqueeEngine(MarqueeEngine* engine) {
    marquee = engine;
}

void WebServerAPI::setVisualizerEngine(VisualizerEngine* engine) {
    visualizer = engine;
}


extern String getPosixTimezone(String tz);

// ---- GIF library: background full reindex -----------------------------------------------------------
// A full directory walk of a large library (10k+ files) takes minutes on the S3's SD_MMC bus; doing it inside an
// HTTP handler starves the async TCP task and trips the watchdog. The walk therefore runs in its own low-priority
// task on core 0, one folder per sdMutex hold, while the rotation is suspended and the panel shows a notice.
namespace {
// Which GIF library a request targets. The engine keeps the two orientations in separate roots
// (GifEngine selects /gifs_tate in vertical mode), so the management API has to do the same.
String gifOrientationOf(AsyncWebServerRequest* request) {
    String o = request->hasParam("orientation") ? request->getParam("orientation")->value() : String();
    o.trim(); o.toLowerCase();
    return o == "tate" ? String("tate") : String("yoko");   // anything unrecognised stays horizontal
}
String gifRootFor(const String& orientation) {
    return orientation == "tate" ? String("/gifs_tate") : String("/gifs");
}
String gifRootOf(AsyncWebServerRequest* request) { return gifRootFor(gifOrientationOf(request)); }

// Both library roots, in the order the UI shows them; the rescan walks every one of them.
const char* const GIF_ROOTS[] = { "/gifs", "/gifs_tate" };
const size_t GIF_ROOT_COUNT = sizeof(GIF_ROOTS) / sizeof(GIF_ROOTS[0]);

struct GifReindexState {
    // Claimed with compare_exchange_strong: the reindex worker runs pinned to core 0 while the
    // handlers run on the async TCP task, so a plain check-then-set can let two rescans overlap.
    std::atomic<bool> running{false};
    volatile int total = 0;
    volatile int done = 0;
    volatile long files = 0;      // directory entries seen so far (updated by the folder walk)
    volatile long expected = 0;   // files listed in playlists.json at start (for the ETA)
    volatile bool cancel = false;
    unsigned long lastMsgMs = 0;
    String current;
    String lastResult;          // "ok: N folders" / "error: ..."
    unsigned long startedMs = 0, finishedMs = 0;
};
GifReindexState g_gifReindex;
MessageEngine* g_gifMsg = nullptr;
std::function<int(const String&, const String&)> g_gifWriteFolderIndex;   // bound in begin()
std::function<String(const String&)> g_gifBuildPlaylistsFromIndexes;      // bound in begin()
std::function<bool(const String&)> g_gifIsMacJunk;                          // bound in begin()
std::function<String(const String&)> g_gifReadPlaylistsJson;                // bound in begin()

void gifReindexEnterMaintenance() {
    if (gifEngine) gifEngine->stop();
    if (rotationManager) rotationManager->setSuspended(true);
    if (g_gifMsg) {
        MessageConfig m; m.text = "Reindexing GIF library..."; m.offsetY = -8; m.color = 0x07FF; m.size = 1; m.direction = "rtl"; m.speed = 50; m.timeoutSeconds = 7200;
        g_gifMsg->queueMessage(m);
    }
}
String gifReindexEta() {
    unsigned long el = millis() - g_gifReindex.startedMs;
    if (g_gifReindex.expected <= 0 || g_gifReindex.files < 50 || el < 5000) return String();
    float rate = (float)g_gifReindex.files / (el / 1000.0f);                 // files per second so far
    long remaining = g_gifReindex.expected - g_gifReindex.files; if (remaining < 0) remaining = 0;
    long secs = rate > 0.01f ? (long)(remaining / rate) : 0;
    char buf[24]; snprintf(buf, sizeof(buf), "~%ld:%02ld left", secs / 60, secs % 60);
    return String(buf);
}
void gifReindexShowProgress() {
    if (!g_gifMsg) return;
    g_gifReindex.lastMsgMs = millis();
    MessageConfig m;
    String eta = gifReindexEta();
    m.text = "Indexing " + String(g_gifReindex.done) + "/" + String(g_gifReindex.total) + " folders - " + String((long)g_gifReindex.files) +
             (g_gifReindex.expected > 0 ? "/" + String((long)g_gifReindex.expected) : String()) + " files" + (eta.length() ? " - " + eta : String());
    m.offsetY = -8;   // one text line above centre, per Erik
    m.color = 0x07FF; m.size = 1; m.direction = "rtl"; m.speed = 50; m.timeoutSeconds = 7200;
    g_gifMsg->queueMessage(m);
}
void gifReindexLeaveMaintenance() {
    if (g_gifMsg) g_gifMsg->deactivate();
    if (rotationManager) rotationManager->setSuspended(false);
}

void gifReindexTask(void*) {
    // Both orientations are rebuilt: vertical orientation keeps its playlists under /gifs_tate, and a
    // rescan that only walked /gifs would leave those index.txt files stale for ever.
    std::vector<std::pair<String, String>> work;   // (root, folder)
    for (size_t r = 0; r < GIF_ROOT_COUNT; ++r) {
        const String root = GIF_ROOTS[r];
        SdLockGuard guard(pdMS_TO_TICKS(15000));   // one coarse hold per root: exists() + full directory scan
        if (guard) {
            FsFile dir = sd.exists(root.c_str()) ? sd.open(root.c_str(), FILE_OPEN_READ) : FsFile();
            if (dir && isDirectory(dir)) {
                FsFile f;
                while (getNextFile(dir, f)) {
                    if (!isDirectory(f)) continue;
                    String name = getFileName(f); int ls = name.lastIndexOf('/'); if (ls >= 0) name = name.substring(ls + 1);
                    if (g_gifIsMacJunk && g_gifIsMacJunk(name)) continue;
                    work.push_back({ root, name });
                }
                dir.close();
            }
        }
    }
    g_gifReindex.total = (int)work.size();
    if (g_gifReadPlaylistsJson) {
        long sum = 0;
        for (size_t r = 0; r < GIF_ROOT_COUNT; ++r) {
            const String root = GIF_ROOTS[r];
            String pl;
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) continue;
                pl = sd.exists(root.c_str()) ? g_gifReadPlaylistsJson(root) : String();
            }
            if (!pl.length()) continue;
            SpiRamJsonDocument doc(pl.length() * 2 + 1024);   // transient parse buffer belongs in PSRAM (Golden Rule #14)
            if (deserializeJson(doc, pl) == DeserializationError::Ok && doc.is<JsonObject>()) {
                for (JsonPair kv : doc.as<JsonObject>()) sum += kv.value()["count"] | 0;
            }
        }
        g_gifReindex.expected = sum;
    }
    for (auto& item : work) {
        if (g_gifReindex.cancel) break;
        g_gifReindex.current = item.second;
        gifReindexShowProgress();
        {
            SdLockGuard guard(pdMS_TO_TICKS(30000));
            if (guard && g_gifWriteFolderIndex) g_gifWriteFolderIndex(item.first, item.second);
        }
        g_gifReindex.done = g_gifReindex.done + 1;
        vTaskDelay(pdMS_TO_TICKS(20));   // let the web server breathe between folders
    }
    {
        SdLockGuard guard(pdMS_TO_TICKS(15000));
        if (guard) {
            for (size_t r = 0; r < GIF_ROOT_COUNT; ++r) {
                if (sd.exists(GIF_ROOTS[r]) && g_gifBuildPlaylistsFromIndexes) g_gifBuildPlaylistsFromIndexes(GIF_ROOTS[r]);
            }
        }
    }
    g_gifReindex.lastResult = g_gifReindex.cancel ? ("cancelled after " + String(g_gifReindex.done) + "/" + String(g_gifReindex.total) + " folders") : ("ok: " + String(g_gifReindex.total) + " folders, " + String((long)g_gifReindex.files) + " files");
    g_gifReindex.current = "";
    g_gifReindex.finishedMs = millis();
    gifReindexLeaveMaintenance();
    g_gifReindex.running = false;
    vTaskDelete(nullptr);
}
} // namespace

void WebServerAPI::begin() {
    g_gifMsg = msg;
    setupRoutes();
    
    // Default headers for CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    // Serve the Web UI directly from Firmware Flash (PROGMEM)
    // Compressed with gzip to save ~190KB flash and prevent LwIP TCP buffer exhaustion.
    // The ETag is content-derived (see scripts/build_webui.py): a commit-derived tag would not
    // change when data/index.html is edited without committing, leaving stale UI in the browser.
    auto serveWebUi = [](AsyncWebServerRequest *request) {
        if (request->hasHeader("If-None-Match")) {
            const AsyncWebHeader* h = request->getHeader("If-None-Match");
            if (h && h->value().equals(WebUI_html_etag)) {
                request->send(304);
                return;
            }
        }
        AsyncWebServerResponse *response = request->beginResponse(200, "text/html", WebUI_html, WebUI_html_len);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("ETag", WebUI_html_etag);
        response->addHeader("Cache-Control", "no-cache");
        request->send(response);
    };
    server.on("/", HTTP_GET, serveWebUi);
    server.on("/index.html", HTTP_GET, serveWebUi);

    server.begin();
    LOGI("WebServer", "Web Server Started.");
}

void WebServerAPI::sendJsonResponse(AsyncWebServerRequest *request, JsonDocument& doc) {
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void WebServerAPI::setupRoutes() {

    // API: GET /api/hardware (Hardware Profile & Runtime Capabilities)
    server.on("/api/hardware", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(512);
        const auto& caps = hardwareHAL.capabilities();
        doc["profile"] = (caps.profile == HwProfile::WAVESHARE_S3) ? "WAVESHARE_S3" : "ESP32_STD";
        JsonObject psramObj = doc.createNestedObject("psram");
        psramObj["available"] = caps.hasPsram;
        psramObj["bytes"] = caps.psramBytes;
        doc["microphone"] = caps.hasMicrophone;
        doc["temperature_sensor"] = caps.hasTempSensor;
        doc["gyroscope"] = gyroHAL.isAvailable();
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/engines (schema-driven engine descriptors, streamed chunk by chunk)
    //
    // Two regressions were fixed here and both must stay fixed:
    //  1. beginResponseStream() buffers the WHOLE response in a StreamString. With ~19 descriptors
    //     and ~130 config fields that is tens of KB of heap (plus the transient doubling of every
    //     String realloc), which starves the HUB75 DMA buffers and freezes the panel on Core 1.
    //     beginChunkedResponse() keeps a single serialized descriptor in RAM at a time.
    //  2. A fixed DynamicJsonDocument(4096) silently overflowed for the largest descriptors
    //     (DashboardEngine has 22 fields, GNewsEngine 20) - ArduinoJson then drops members and
    //     emits a structurally incomplete descriptor, which the WebUI reports as "returned empty".
    //     The capacity is now derived from the actual field count.
    server.on("/api/engines", HTTP_GET, [](AsyncWebServerRequest *request){
        auto state = std::make_shared<EngineStreamState>();

        AsyncWebServerResponse* response = request->beginChunkedResponse("application/json",
            [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                (void)index;
                // Returning 0 terminates a chunked response, so a zero-sized window must ask for
                // another call instead of truncating the array.
                if (maxLen == 0) return RESPONSE_TRY_AGAIN;
                if (state->offset >= state->pending.length()) {
                    if (!refillEngineStream(*state)) {
                        return 0; // Whole array emitted
                    }
                }
                size_t remaining = state->pending.length() - state->offset;
                size_t toCopy = (remaining < maxLen) ? remaining : maxLen;
                memcpy(buffer, state->pending.c_str() + state->offset, toCopy);
                state->offset += toCopy;
                return toCopy;
            });

        request->send(response);
    });

    // API: GET /api/themes (Dynamic options endpoint for themes)
    server.on("/api/themes", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        struct ThemeItem { int id; const char* name; };
        static const ThemeItem themes[] = {
            {0, "Nintendo"}, {1, "Capcom"}, {2, "Taito"}, {3, "Sega"},
            {4, "Cave"}, {5, "Konami"}, {6, "SNK"}, {7, "Technos"},
            {8, "IGS"}, {9, "Hudson"}, {10, "Banpresto"}, {11, "Namco"},
            {12, "Street Fighter (Ryu)"}, {13, "Super Mario"}, {14, "Metal Slug (Marco)"},
            {15, "Mega Man"}, {16, "Space Invaders"}, {17, "Bubble Bobble (Bub)"},
            {18, "Cyberpunk"}, {19, "Flip Clock"}, {20, "Custom Gradient"},
            {21, "True Matrix"}, {22, "Pong Clock"}, {23, "Tetris Clock"},
            {24, "Word Clock"}, {25, "Binary Clock"}, {26, "Pac-Man Clock"},
            {27, "Versus Clock"}, {28, "Slot Machine Clock"}, {29, "Tetris Game Boy"}
        };
        for (const auto& t : themes) {
            JsonObject obj = arr.createNestedObject();
            obj["id"] = t.id;
            obj["name"] = t.name;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/timezones (Dynamic options endpoint for timezones)
    server.on("/api/timezones", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(4096);
        JsonArray arr = doc.to<JsonArray>();
        struct TzItem { const char* value; const char* label; };
        static const TzItem timezones[] = {
            {"Europe/Paris", "Europe/Paris (UTC+1/+2)"},
            {"Europe/London", "Europe/London (UTC+0/+1)"},
            {"Europe/Dublin", "Europe/Dublin (UTC+0/+1)"},
            {"Europe/Lisbon", "Europe/Lisbon (UTC+0/+1)"},
            {"Europe/Berlin", "Europe/Berlin (UTC+1/+2)"},
            {"Europe/Madrid", "Europe/Madrid (UTC+1/+2)"},
            {"Europe/Rome", "Europe/Rome (UTC+1/+2)"},
            {"Europe/Brussels", "Europe/Brussels (UTC+1/+2)"},
            {"Europe/Amsterdam", "Europe/Amsterdam (UTC+1/+2)"},
            {"Europe/Zurich", "Europe/Zurich (UTC+1/+2)"},
            {"Europe/Vienna", "Europe/Vienna (UTC+1/+2)"},
            {"Europe/Warsaw", "Europe/Warsaw (UTC+1/+2)"},
            {"Europe/Prague", "Europe/Prague (UTC+1/+2)"},
            {"Europe/Stockholm", "Europe/Stockholm (UTC+1/+2)"},
            {"Europe/Oslo", "Europe/Oslo (UTC+1/+2)"},
            {"Europe/Copenhagen", "Europe/Copenhagen (UTC+1/+2)"},
            {"Europe/Athens", "Europe/Athens (UTC+2/+3)"},
            {"Europe/Helsinki", "Europe/Helsinki (UTC+2/+3)"},
            {"Europe/Bucharest", "Europe/Bucharest (UTC+2/+3)"},
            {"Europe/Kyiv", "Europe/Kyiv (UTC+2/+3)"},
            {"Europe/Moscow", "Europe/Moscow (UTC+3)"},
            {"Europe/Istanbul", "Europe/Istanbul (UTC+3)"},
            {"Atlantic/Reykjavik", "Atlantic/Reykjavik (UTC+0)"},
            {"Atlantic/Azores", "Atlantic/Azores (UTC-1/+0)"},
            {"America/New_York", "America/New_York (EST/EDT, UTC-5/-4)"},
            {"America/Detroit", "America/Detroit (EST/EDT, UTC-5/-4)"},
            {"America/Indiana/Indianapolis", "America/Indiana/Indianapolis (EST/EDT, UTC-5/-4)"},
            {"America/Montreal", "America/Montreal (EST/EDT, UTC-5/-4)"},
            {"America/Toronto", "America/Toronto (EST/EDT, UTC-5/-4)"},
            {"America/Chicago", "America/Chicago (CST/CDT, UTC-6/-5)"},
            {"America/Mexico_City", "America/Mexico_City (CST, UTC-6)"},
            {"America/Denver", "America/Denver (MST/MDT, UTC-7/-6)"},
            {"America/Boise", "America/Boise (MST/MDT, UTC-7/-6)"},
            {"America/Phoenix", "America/Phoenix (MST, UTC-7, no DST)"},
            {"America/Los_Angeles", "America/Los_Angeles (PST/PDT, UTC-8/-7)"},
            {"America/Vancouver", "America/Vancouver (PST/PDT, UTC-8/-7)"},
            {"America/Anchorage", "America/Anchorage (AKST/AKDT, UTC-9/-8)"},
            {"America/Halifax", "America/Halifax (AST/ADT, UTC-4/-3)"},
            {"America/St_Johns", "America/St_Johns (NST/NDT, UTC-3:30/-2:30)"},
            {"Pacific/Honolulu", "Pacific/Honolulu (HST, UTC-10)"},
            {"America/Sao_Paulo", "America/Sao_Paulo (BRT, UTC-3)"},
            {"America/Buenos_Aires", "America/Buenos_Aires (ART, UTC-3)"},
            {"America/Santiago", "America/Santiago (CLT/CLST, UTC-4/-3)"},
            {"America/Bogota", "America/Bogota (COT, UTC-5)"},
            {"America/Lima", "America/Lima (PET, UTC-5)"},
            {"Africa/Casablanca", "Africa/Casablanca (WEST, UTC+1)"},
            {"Africa/Cairo", "Africa/Cairo (EET/EEST, UTC+2/+3)"},
            {"Africa/Johannesburg", "Africa/Johannesburg (SAST, UTC+2)"},
            {"Africa/Nairobi", "Africa/Nairobi (EAT, UTC+3)"},
            {"Africa/Lagos", "Africa/Lagos (WAT, UTC+1)"},
            {"Asia/Jerusalem", "Asia/Jerusalem (IST/IDT, UTC+2/+3)"},
            {"Asia/Riyadh", "Asia/Riyadh (AST, UTC+3)"},
            {"Asia/Dubai", "Asia/Dubai (GST, UTC+4)"},
            {"Asia/Tehran", "Asia/Tehran (IRST, UTC+3:30)"},
            {"Asia/Karachi", "Asia/Karachi (PKT, UTC+5)"},
            {"Asia/Kolkata", "Asia/Kolkata (IST, UTC+5:30)"},
            {"Asia/Dhaka", "Asia/Dhaka (BST, UTC+6)"},
            {"Asia/Bangkok", "Asia/Bangkok (ICT, UTC+7)"},
            {"Asia/Jakarta", "Asia/Jakarta (WIB, UTC+7)"},
            {"Asia/Singapore", "Asia/Singapore (SGT, UTC+8)"},
            {"Asia/Hong_Kong", "Asia/Hong_Kong (HKT, UTC+8)"},
            {"Asia/Shanghai", "Asia/Shanghai (CST, UTC+8)"},
            {"Asia/Taipei", "Asia/Taipei (CST, UTC+8)"},
            {"Asia/Manila", "Asia/Manila (PST, UTC+8)"},
            {"Asia/Tokyo", "Asia/Tokyo (JST, UTC+9)"},
            {"Asia/Seoul", "Asia/Seoul (KST, UTC+9)"},
            {"Australia/Sydney", "Australia/Sydney (AEST/AEDT, UTC+10/+11)"},
            {"Australia/Melbourne", "Australia/Melbourne (AEST/AEDT, UTC+10/+11)"},
            {"Australia/Brisbane", "Australia/Brisbane (AEST, UTC+10)"},
            {"Australia/Adelaide", "Australia/Adelaide (ACST/ACDT, UTC+9:30/+10:30)"},
            {"Australia/Perth", "Australia/Perth (AWST, UTC+8)"},
            {"Pacific/Guam", "Pacific/Guam (ChST, UTC+10)"},
            {"Pacific/Auckland", "Pacific/Auckland (NZST/NZDT, UTC+12/+13)"},
            {"Pacific/Fiji", "Pacific/Fiji (FJT, UTC+12)"},
            {"UTC", "UTC (Coordinated Universal Time)"}
        };
        for (const auto& tz : timezones) {
            JsonObject obj = arr.createNestedObject();
            obj["value"] = tz.value;
            obj["label"] = tz.label;
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: GET /api/instances & POST /api/instances (CRUD instances)
    // API: GET /api/instances (streamed one instance at a time)
    // Same two fixes as /api/engines: chunked streaming instead of a full in-RAM StreamString, and
    // a capacity derived from the actual dictionary instead of a fixed 1024 bytes (instance configs
    // store String keys AND String values, which ArduinoJson copies, so large engine configs such
    // as DashboardEngine silently lost settings on serialization).
    server.on("/api/instances", HTTP_GET, [](AsyncWebServerRequest *request){
        auto state = std::make_shared<InstanceStreamState>();

        AsyncWebServerResponse* response = request->beginChunkedResponse("application/json",
            [state](uint8_t* buffer, size_t maxLen, size_t index) -> size_t {
                (void)index;
                if (maxLen == 0) return RESPONSE_TRY_AGAIN;
                if (state->offset >= state->pending.length()) {
                    if (!refillInstanceStream(*state)) {
                        return 0;
                    }
                }
                size_t remaining = state->pending.length() - state->offset;
                size_t toCopy = (remaining < maxLen) ? remaining : maxLen;
                memcpy(buffer, state->pending.c_str() + state->offset, toCopy);
                state->offset += toCopy;
                return toCopy;
            });

        request->send(response);
    });

    AsyncCallbackJsonWebHandler* instancesHandler = new AsyncCallbackJsonWebHandler("/api/instances", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        String instanceId = doc["instance_id"].as<String>();
        String engineId = doc["engine_id"].as<String>();
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required\"}");
            return;
        }
        
        bool found = false;
        String oldEngineId = "";
        {
            EngineInstanceSnapshot snap;
            if (config.getInstanceSnapshot(instanceId, snap)) {
                found = true;
                oldEngineId = snap.engine_id;
            }
        }
        bool isNew = !found;
        String targetEngineId = engineId.isEmpty() ? (found ? oldEngineId : instanceId) : engineId;

        const EngineDescriptor* desc = EngineRegistry::getDescriptor(targetEngineId.c_str());
        if (!desc) {
            request->send(400, "application/json", "{\"error\":\"Unknown engine_id\"}");
            return;
        }

        auto reqCheck = EngineRegistrar::checkRequirements(desc->requirements);
        if (!reqCheck.satisfied) {
            SpiRamJsonDocument errDoc(256);
            errDoc["error"] = "engine_unavailable";
            errDoc["reason"] = reqCheck.reason;
            String errResp;
            serializeJson(errDoc, errResp);
            request->send(400, "application/json", errResp);
            return;
        }
        
        DictionaryEngineConfig activeConfig;
        config.mutate([&](ConfigLoader& cfg) {
            if (isNew) {
                cfg.instances.push_back({instanceId, targetEngineId, {}});
            }
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == instanceId) {
                    if (doc.containsKey("engine_id") && !engineId.isEmpty()) {
                        inst.engine_id = engineId;
                    }
                    if (doc.containsKey("config") && doc["config"].is<JsonObject>()) {
                        JsonObject cfgObj = doc["config"].as<JsonObject>();
                        for (JsonPair kv : cfgObj) {
                            inst.config.setString(kv.key().c_str(), kv.value().as<String>());
                        }
                    }
                    activeConfig = inst.config;
                    break;
                }
            }
        });
        
        bool structuralChange = isNew || (oldEngineId != targetEngineId);

        // Sanitize and save
        ConfigSanitizer::sanitizeInstances(config);
        bool saved = config.saveToSD("/config.json");
        
        if (rotationManager) {
            if (structuralChange) {
                rotationManager->recreateInstance(instanceId);
                rotationManager->resetRotation();
            } else {
                rotationManager->notifyConfigChanged(instanceId);
            }
        }
        
        if (targetEngineId == "audiovisualizer" && visualizer) {
            visualizer->onConfigChanged(&activeConfig);
        }
        
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(instancesHandler);

    // API: DELETE /api/instances/{id} — Remove an instance by ID
    server.on("/api/instances", HTTP_DELETE, [](AsyncWebServerRequest *request){
        // ESPAsyncWebServer doesn't natively support path parameters,
        // so we look for ?id=xxx or parse the URL path manually.
        String instanceId = "";
        
        // Check query parameter first: DELETE /api/instances?id=xxx
        if (request->hasParam("id")) {
            instanceId = request->getParam("id")->value();
        }
        
        // Also support path-style: DELETE /api/instances/xxx (parsed from URL)
        String url = request->url();
        if (instanceId.isEmpty() && url.startsWith("/api/instances/")) {
            instanceId = url.substring(strlen("/api/instances/"));
            // URL-decode if needed (simple cases)
            instanceId.trim();
        }
        
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required (use ?id=xxx or /api/instances/xxx)\"}");
            return;
        }
        
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        bool removed = config.removeInstance(instanceId);
        if (!removed) {
            request->send(404, "application/json", "{\"error\":\"Instance not found\"}");
            return;
        }
        
        // Also remove from rotation if present
        for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
            if (it->instance_id == instanceId) {
                it = config.rotation.erase(it);
            } else {
                ++it;
            }
        }
        
        ConfigSanitizer::sanitizeInstances(config);
        bool saved = config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->recreateInstance(instanceId);
            rotationManager->resetRotation();
        }
        
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    });

    // Also handle path-style DELETE: /api/instances/xxx (catchall for sub-paths)
    server.on("/api/instances/*", HTTP_DELETE, [](AsyncWebServerRequest *request){
        String url = request->url();
        String instanceId = "";
        if (url.startsWith("/api/instances/")) {
            instanceId = url.substring(strlen("/api/instances/"));
            instanceId.trim();
        }
        
        if (instanceId.isEmpty()) {
            request->send(400, "application/json", "{\"error\":\"instance_id is required\"}");
            return;
        }
        
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        bool removed = config.removeInstance(instanceId);
        if (!removed) {
            request->send(404, "application/json", "{\"error\":\"Instance not found\"}");
            return;
        }
        
        for (auto it = config.rotation.begin(); it != config.rotation.end(); ) {
            if (it->instance_id == instanceId) {
                it = config.rotation.erase(it);
            } else {
                ++it;
            }
        }
        
        ConfigSanitizer::sanitizeInstances(config);
        bool saved = config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->recreateInstance(instanceId);
            rotationManager->resetRotation();
        }
        
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    });

    // API: GET /api/rotation — Return the current rotation list
    server.on("/api/rotation", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        // Capacity derived from the actual rotation length: a fixed 2048 bytes silently dropped
        // entries once the loop grew past roughly 25 screens.
        size_t stringBytes = 0;
        for (const auto& rot : config.rotation) {
            stringBytes += rot.instance_id.length() + 1;
        }
        const size_t capacity = JSON_ARRAY_SIZE(config.rotation.size())
                              + config.rotation.size() * (JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(1))
                              + stringBytes
                              + 256;
        SpiRamJsonDocument doc(capacity);
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& rot : config.rotation) {
            JsonObject obj = arr.createNestedObject();
            obj["instance_id"] = rot.instance_id;
            obj["duration_sec"] = rot.duration_sec;
            if (rot.overlays.fighter != FighterOverride::Unspecified) {
                JsonObject ovObj = obj.createNestedObject("overlays");
                ovObj["fighter"] = (rot.overlays.fighter == FighterOverride::Enabled);
            }
        }
        if (doc.overflowed()) {
            LOGE("WebServer", "Rotation list overflowed its %u byte document; entries truncated.",
                 (unsigned)capacity);
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: POST /api/rotation — Replace the entire rotation list
    AsyncCallbackJsonWebHandler* rotationHandler = new AsyncCallbackJsonWebHandler("/api/rotation", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonArray>()) {
            request->send(400, "application/json", "{\"error\":\"Expected a JSON array of rotation entries\"}");
            return;
        }
        JsonArray arr = json.as<JsonArray>();
        extern ConfigLoader config;
        extern RotationManager* rotationManager;
        
        config.rotation.clear();
        for (JsonObject entry : arr) {
            RotationEntry re;
            re.instance_id = entry["instance_id"].as<String>();
            re.duration_sec = entry["duration_sec"] | 15;
            if (entry.containsKey("overlays") && entry["overlays"].is<JsonObject>() && entry["overlays"].containsKey("fighter")) {
                re.overlays.fighter = entry["overlays"]["fighter"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
            } else if (entry.containsKey("fighter_overlay")) {
                re.overlays.fighter = entry["fighter_overlay"].as<bool>() ? FighterOverride::Enabled : FighterOverride::Disabled;
            } else {
                re.overlays.fighter = FighterOverride::Unspecified;
            }
            if (!re.instance_id.isEmpty()) {
                config.rotation.push_back(re);
            }
        }
        
        bool saved = config.saveToSD("/config.json");
        
        if (rotationManager) {
            rotationManager->resetRotation();
        }
        
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(rotationHandler);

    // API: Get Device Status
    // Fighter overlay diagnostics (roster, loader, memory floors, last warning): the overlay has no
    // other network-visible state and its failures are otherwise only on the serial console.
    server.on("/api/fighter/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", FighterEngine::debugStatusJson());
    });

    server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(1024);
        doc["status"] = "online";
        doc["uptime"] = millis();
        doc["cpu_mhz"] = getCpuFrequencyMhz();
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["max_alloc_heap"] = ESP.getMaxAllocHeap();
        doc["internal_free"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        doc["internal_largest"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        doc["psram_found"] = hardwareHAL.capabilities().hasPsram;
        if (hardwareHAL.capabilities().hasPsram) {
            doc["free_psram"] = ESP.getFreePsram();
        }
        UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
        doc["async_tcp_hwm_bytes"] = (uint32_t)(hwm * sizeof(StackType_t));

        // Render rates since the previous /api/status call (poll it twice, a few seconds apart).
        {
            static uint32_t lastMs = 0, lastLoops = 0, lastPresents = 0, lastGifFrames = 0,
                            lastWritten = 0, lastTotal = 0, lastBlitUs = 0, lastDecodeUs = 0;
            uint32_t nowMs = millis();
            uint32_t loops = g_renderStats.loops.load(), presents = g_renderStats.presents.load(),
                     gifFrames = g_renderStats.gifFrames.load(), written = g_renderStats.gifPixelsWritten.load(),
                     total = g_renderStats.gifPixelsTotal.load(), blitUs = g_renderStats.gifBlitMicros.load(),
                     decodeUs = g_renderStats.gifDecodeMicros.load();
            uint32_t dtMs = nowMs - lastMs;
            if (lastMs != 0 && dtMs >= 500) {
                JsonObject r = doc.createNestedObject("render");
                r["window_ms"] = dtMs;
                r["loop_fps"] = (float)(loops - lastLoops) * 1000.0f / dtMs;
                r["present_fps"] = (float)(presents - lastPresents) * 1000.0f / dtMs;
                uint32_t gf = gifFrames - lastGifFrames;
                r["gif_fps"] = (float)gf * 1000.0f / dtMs;
                if (gf > 0) {
                    r["gif_blit_ms"] = (float)(blitUs - lastBlitUs) / 1000.0f / gf;
                    r["gif_decode_ms"] = (float)(decodeUs - lastDecodeUs) / 1000.0f / gf;
                    uint32_t tot = total - lastTotal;
                    if (tot > 0) r["gif_pixels_written_pct"] = (float)(written - lastWritten) * 100.0f / tot;
                }
            }
            lastMs = nowMs; lastLoops = loops; lastPresents = presents; lastGifFrames = gifFrames;
            lastWritten = written; lastTotal = total; lastBlitUs = blitUs; lastDecodeUs = decodeUs;
        }
        sendJsonResponse(request, doc);
    });

    // API: System Info & Stats (Dashboard metrics compatibility)
    auto sendSysStats = [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(1024);
        float tempC = 0.0f;
        float humidity = 0.0f;
        if (hardwareHAL.capabilities().hasTempSensor) {
            EnvironmentData env = hardwareHAL.readEnvironment();
            tempC = env.temperatureC;
            humidity = env.humidity;
        } else {
            tempC = temperatureRead();
        }
        
        doc["cpu_load"] = 0.0f;
        doc["temperature_c"] = tempC;
        doc["humidity"] = humidity;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["free_heap_kb"] = ESP.getFreeHeap() / 1024;
        doc["total_heap_kb"] = ESP.getHeapSize() / 1024;
        doc["ram_used_mb"] = (float)(ESP.getHeapSize() - ESP.getFreeHeap()) / (1024.0f * 1024.0f);
        doc["ram_total_mb"] = (float)ESP.getHeapSize() / (1024.0f * 1024.0f);
        doc["psram_found"] = hardwareHAL.capabilities().hasPsram;
        doc["psram_free_mb"] = (float)ESP.getFreePsram() / (1024.0f * 1024.0f);
        doc["psram_total_mb"] = (float)ESP.getPsramSize() / (1024.0f * 1024.0f);
        doc["disk_free_gb"] = 0.0f;
        doc["uptime_sec"] = millis() / 1000;
        doc["has_temp_sensor"] = hardwareHAL.capabilities().hasTempSensor;
        doc["has_microphone"] = hardwareHAL.capabilities().hasMicrophone;
        doc["has_dac"] = hardwareHAL.capabilities().audio.output;
        doc["has_gyro"] = gyroHAL.isAvailable();
        doc["gyro_sensor"] = gyroHAL.getOrientation().sensorName;
        doc["hardware_profile"] = "Waveshare ESP32-S3 RGB Matrix";
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    };
    server.on("/api/system_info", HTTP_GET, sendSysStats);
    server.on("/api/stats", HTTP_GET, sendSysStats);

    // API: List fonts (Built-in + custom SD .amf files)
    server.on("/api/fonts", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(2048);
        JsonArray arr = doc.to<JsonArray>();
        
        arr.add("Default");
        arr.add("PressStart2P");
        arr.add("namco");
        arr.add("FreeSansBold");
        arr.add("FreeMonoBold");
        arr.add("RetroGaming");
        
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile dir = sd.open("/fonts", FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile entry = dir.openNextFile();
            while (entry) {
                yield();
                if (!isDirectory(entry)) {
                    String name = getFileName(entry);
                    if (name.endsWith(".amf") || name.endsWith(".AMF")) {
                        if (!name.startsWith("/")) name = "/fonts/" + name;
                        arr.add(name);
                    }
                }
                entry.close();
                entry = dir.openNextFile();
            }
            dir.close();
            }
            xSemaphoreGive(sdMutex);
        }
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Return version with Git commit and build timestamp
    server.on("/api/version", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(256);
        doc["version"] = FIRMWARE_VERSION;
        doc["git_commit"] = BUILD_GIT_COMMIT;
        doc["build_timestamp"] = BUILD_TIMESTAMP;
        doc["arch"] = (hardwareHAL.capabilities().profile == HwProfile::WAVESHARE_S3) ? "esp32s3" : "esp32";
        const esp_partition_t* running = esp_ota_get_running_partition();
        doc["partition"] = running ? running->label : "app0";
        String response;
        serializeJson(doc, response);
        AsyncWebServerResponse *res = request->beginResponse(200, "application/json", response);
        res->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        res->addHeader("Pragma", "no-cache");
        res->addHeader("Expires", "0");
        request->send(res);
    });

    // API: Get Indoor Environment Sensor (Home Automation / REST Sensor)
    server.on("/api/sensor", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        EnvironmentData data = hardwareHAL.readEnvironment();
        SpiRamJsonDocument doc(256);
        doc["available"] = data.available;
        doc["temperature_c"] = data.temperatureC;
        doc["temperature_f"] = data.temperatureF;
        doc["humidity"] = data.humidity;
        doc["unit"] = config.system.unit;
        doc["status"] = data.available ? "ok" : "not_detected";
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: Music Visualizer Control (Priority Display Override)
    AsyncCallbackJsonWebHandler* visHandler = new AsyncCallbackJsonWebHandler("/api/visualizer", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        config.mutate([&](ConfigLoader& cfg) {
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == "visualizer_main") {
                    if (!doc["enabled"].isNull()) {
                        inst.config.setBool("enabled", (bool)doc["enabled"]);
                        if (visualizer) {
                            if (inst.config.getBool("enabled")) visualizer->activate();
                            else visualizer->deactivate();
                        }
                    }
                    if (!doc["style"].isNull()) {
                        inst.config.setString("style", (const char*)doc["style"]);
                    }
                    if (!doc["mode"].isNull()) {
                        inst.config.setString("style", (const char*)doc["mode"]);
                    }
                    if (visualizer) {
                        visualizer->onConfigChanged(&inst.config);
                    }
                    if (!doc["gain"].isNull()) {
                        float g = (float)doc["gain"];
                        inst.config.setString("gain", String(g));
                        hardwareHAL.setMicGain(g);
                    }
                    if (!doc["sensitivity"].isNull()) {
                        int s = (int)doc["sensitivity"];
                        inst.config.setString("sensitivity", String(s));
                    }
                    break;
                }
            }
        });
        bool saved = config.saveToSD("/config.json");
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(visHandler);

    // API: List GIF Playlists (Direct SD streaming with zero heap allocation, falls back to dynamic directory scan)
    server.on("/api/playlists", HTTP_GET, [](AsyncWebServerRequest *request){
        String reqType = "";
        if (request->hasParam("type")) reqType = request->getParam("type")->value();
        else if (request->hasParam("folder") && request->getParam("folder")->value().indexOf("tate") != -1) reqType = "tate";

        int cacheSlot = 0;
        if (reqType.equalsIgnoreCase("tate")) cacheSlot = 2;
        else if (reqType.equalsIgnoreCase("yoko") || reqType.equalsIgnoreCase("horizontal")) cacheSlot = 1;

        if (request->hasParam("refresh")) {
            invalidatePlaylistCache();
        } else if (s_playlistCacheValid[cacheSlot] &&
                   (millis() - s_playlistCacheStamp[cacheSlot]) < PLAYLIST_CACHE_TTL_MS) {
            request->send(200, "application/json", s_playlistCache[cacheSlot]);
            return;
        }

        auto readOrScan = [](const String& rootDir) -> String {
            String cleanRoot = rootDir;
            if (!sd.exists(cleanRoot.c_str()) && cleanRoot.startsWith("/")) {
                cleanRoot = cleanRoot.substring(1);
            }
            if (!sd.exists(cleanRoot.c_str())) {
                return "{}";
            }

            String jsonPath = cleanRoot + "/playlists.json";
            if (sd.exists(jsonPath.c_str())) {
                FsFile f = sd.open(jsonPath.c_str(), FILE_OPEN_READ);
                if (f) {
                    size_t sz = f.size();
                    if (sz > 0 && sz < 131072) {
                        char* buf = (hardwareHAL.capabilities().hasPsram && psramFound()) 
                                    ? (char*)ps_malloc(sz + 1) 
                                    : (char*)malloc(sz + 1);
                        if (buf) {
                            size_t n = f.read((uint8_t*)buf, sz);
                            buf[n] = '\0';
                            f.close();
                            char* jsonStart = buf;
                            while (*jsonStart && *jsonStart != '{') jsonStart++;
                            char* jsonEnd = buf + n - 1;
                            while (jsonEnd > jsonStart && *jsonEnd != '}') jsonEnd--;
                            if (*jsonStart == '{' && *jsonEnd == '}' && jsonEnd > jsonStart) {
                                *(jsonEnd + 1) = '\0';
                                String s(jsonStart);
                                free(buf);
                                if (s.length() > 2) return s;
                            } else {
                                free(buf);
                            }
                        } else {
                            f.close();
                        }
                    } else {
                        f.close();
                    }
                }
            }

            // Fallback directory scan: an SD card without playlists.json must still expose its
            // folders, otherwise the WebUI shows an empty library and the engine plays nothing.
            String content = "{";
            bool first = true;
            FsFile dir = sd.open(cleanRoot.c_str(), FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile file;
                while (getNextFile(dir, file)) {
                    if (!isDirectory(file)) continue;
                    String name = getFileName(file);
                    int lastSlash = name.lastIndexOf('/');
                    if (lastSlash >= 0) name = name.substring(lastSlash + 1);
                    if (name.length() == 0 || isMacJunk(name)) continue;

                    int count = 0;
                    String indexPath = cleanRoot + "/" + name + "/index.txt";
                    if (sd.exists(indexPath.c_str())) {
                        FsFile idx = sd.open(indexPath.c_str(), FILE_OPEN_READ);
                        if (idx) {
                            while (idx.available()) {
                                String l = idx.readStringUntil('\n');
                                l.trim();
                                if (l.length() > 0 && !isMacJunk(l)) count++;
                            }
                            idx.close();
                        }
                    }
                    if (!first) content += ",";
                    content += "\"" + name + "\":{\"path\":\"" + rootDir + "/" + name + "\",\"count\":" + String(count) + "}";
                    first = false;
                }
                if (file) file.close();
                dir.close();
            } else if (dir) {
                dir.close();
            }
            content += "}";
            return content;
        };

        // Bounded wait: never block the AsyncTCP task indefinitely on the SD mutex, otherwise a
        // long Core 1 decode stalls every pending HTTP connection.
        SdLockGuard guard(pdMS_TO_TICKS(5000));
        if (!guard) {
            if (s_playlistCacheValid[cacheSlot]) {
                // Serve the stale snapshot rather than failing the whole Display tab.
                request->send(200, "application/json", s_playlistCache[cacheSlot]);
            } else {
                request->send(503, "application/json", "{\"error\":\"SD card busy\"}");
            }
            return;
        }

        String payload;
        if (cacheSlot == 2) {
            payload = "{}";
            if (sd.exists("/gifs_tate") || sd.exists("gifs_tate")) {
                payload = readOrScan("/gifs_tate");
            } else if (sd.exists("/gifs/tate") || sd.exists("gifs/tate")) {
                payload = readOrScan("/gifs/tate");
            } else if (sd.exists("/tate") || sd.exists("tate")) {
                payload = readOrScan("/tate");
            }
        } else if (cacheSlot == 1) {
            payload = (sd.exists("/gifs") || sd.exists("gifs")) ? readOrScan("/gifs") : "{}";
        } else {
            String yoko = (sd.exists("/gifs") || sd.exists("gifs")) ? readOrScan("/gifs") : "{}";
            String tate = "{}";
            if (sd.exists("/gifs_tate") || sd.exists("gifs_tate")) {
                tate = readOrScan("/gifs_tate");
            } else if (sd.exists("/gifs/tate") || sd.exists("gifs/tate")) {
                tate = readOrScan("/gifs/tate");
            } else if (sd.exists("/tate") || sd.exists("tate")) {
                tate = readOrScan("/tate");
            }
            payload = "{\"yoko\":" + yoko + ",\"tate\":" + tate + "}";
        }
        guard.unlock();

        s_playlistCache[cacheSlot] = payload;
        s_playlistCacheStamp[cacheSlot] = millis();
        s_playlistCacheValid[cacheSlot] = true;
        request->send(200, "application/json", payload);
    });

    // API: Play GIF Playlists immediately
    AsyncCallbackJsonWebHandler* playHandler = new AsyncCallbackJsonWebHandler("/api/playlists/play", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        JsonArray playlistsArray = doc["playlists"].as<JsonArray>();
        
        std::vector<String> paths;
        for (JsonVariant v : playlistsArray) {
            paths.push_back(v.as<String>());
        }
        
        extern GifEngine* gifEngine;
        if (gifEngine) gifEngine->playPlaylists(paths);
        
        request->send(200, "application/json", "{\"success\":true}");
    }, 4096);
    server.addHandler(playHandler);
    
        server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        AsyncJsonResponse * response = new AsyncJsonResponse(false, 4096);
        JsonObject doc = response->getRoot().as<JsonObject>();
        
        if (doc.isNull()) {
            request->send(500, "text/plain", "OOM JSON");
            delete response;
            return;
        }

        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const ConfigSnapshot& snap = guard.get();

        // Matrix
        doc["brightness_limit"] = snap.matrix.powerLimitPercent;
        doc["color_depth"] = snap.matrix.colorDepth;
        doc["matrix_chain"] = snap.matrix.chainLength;
        doc["matrix_rows"] = snap.matrix.height;
        doc["matrix_cols"] = snap.matrix.width;
        doc["matrix_rgb_sequence"] = snap.matrix.rgbSequence;
        doc["matrix_force_single_buffer"] = snap.matrix.forceSingleBuffer;
        doc["matrix_driver_chip"] = snap.matrix.driverChip;
        doc["matrix_clk_phase"] = snap.matrix.clkPhase;
        doc["matrix_latch_blanking"] = snap.matrix.latchBlanking;
        doc["matrix_row_address_mode"] = snap.matrix.rowAddressMode;
        doc["matrix_limit_refresh_rate_hz"] = snap.matrix.limitRefreshRateHz;
        doc["rotation_offset"] = snap.matrix.rotation_offset;
        doc["auto_rotate"] = snap.matrix.auto_rotate;
        doc["rotation_transition"] = snap.matrix.rotation_transition;
        doc["rotation_transition_duration_ms"] = snap.matrix.rotation_transition_duration_ms;

        auto getInst = [&](const String& id) { return snap.getInstance(id); };
        
        // Crypto
        auto cryptoInst = getInst("crypto_main");
        if (cryptoInst) {
            doc["crypto_enabled"] = cryptoInst->config.getBool("enabled");
            doc["crypto_symbols"] = cryptoInst->config.getString("symbols");
            doc["crypto_duration_sec"] = cryptoInst->config.getInt("duration_sec");
            doc["crypto_cache_ttl_min"] = cryptoInst->config.getInt("cache_ttl_min");
            doc["crypto_currency"] = cryptoInst->config.getString("currency");
        }

        // Stock
        auto stockInst = getInst("stock_main");
        if (stockInst) {
            doc["stock_enabled"] = stockInst->config.getBool("enabled");
            doc["stock_symbols"] = stockInst->config.getString("symbols");
            doc["stock_duration_sec"] = stockInst->config.getInt("duration_sec");
            doc["stock_cache_ttl_min"] = stockInst->config.getInt("cache_ttl_min");
        }

        // Idle rotation
        String rotStr = "";
        for (const auto& r : snap.rotation) rotStr += r.instance_id + ",";
        if (rotStr.endsWith(",")) rotStr.remove(rotStr.length()-1);
        doc["rotation"] = rotStr;
        
        auto getRot = [&](const String& id) {
            for (const auto& r : snap.rotation) if (r.instance_id == id) return r.duration_sec;
            return 15;
        };
        doc["clock_duration_sec"] = getRot("clock_main");
        doc["date_duration_sec"] = getRot("date_main");
        doc["weather_duration_sec"] = getRot("weather_main");
        doc["temp_duration_sec"] = getRot("temp_main");
        doc["decibel_duration_sec"] = getRot("decibel_main");
        
        auto fighterInst = getInst("fighter_main");
        if (fighterInst) {
            doc["fighter_enabled"] = true;
            doc["fighter_interval_sec"] = fighterInst->config.getInt("fighter_interval_sec");
        }

        // Environment & Audio
        doc["temp_unit"] = snap.system.unit;
        doc["temp_offset"] = snap.system.temp_offset;
        
        auto visInst = getInst("visualizer_main");
        if (visInst) {
            doc["visualizer_enabled"] = visInst->config.getBool("enabled");
            doc["visualizer_mode"] = visInst->config.getString("mode");
            doc["mic_gain"] = visInst->config.getFloat("gain");
            doc["db_calibration"] = visInst->config.getFloat("db_calibration");
        }

        doc["sensor_available"] = hardwareHAL.isTempSensorAvailable();
        doc["audio_available"] = hardwareHAL.isAudioAvailable();
        doc["psram_available"] = hardwareHAL.capabilities().hasPsram;

        // Clock
        auto clockInst = getInst("clock_main");
        if (clockInst) {
            doc["clock_font"] = clockInst->config.getInt("clock_font");
            doc["clock_size"] = clockInst->config.getInt("clock_size");
            doc["clock_theme"] = clockInst->config.getInt("clock_theme");
            doc["clock_offset_x"] = clockInst->config.getInt("clock_offset_x");
            doc["clock_offset_y"] = clockInst->config.getInt("clock_offset_y");
            doc["clock_color_1"] = clockInst->config.getString("clock_color_1");
            doc["clock_color_2"] = clockInst->config.getString("clock_color_2");
            doc["clock_font_path"] = clockInst->config.getString("clock_font_path");
        }

        // Date
        auto dateInst = getInst("date_main");
        if (dateInst) {
            doc["date_font"] = dateInst->config.getInt("date_font");
            doc["date_size"] = dateInst->config.getInt("date_size");
            doc["date_theme"] = dateInst->config.getInt("theme");
            doc["date_offset_x"] = dateInst->config.getInt("date_offset_x");
            doc["date_offset_y"] = dateInst->config.getInt("date_offset_y");
            doc["date_format"] = dateInst->config.getString("format");
            doc["date_sprite"] = dateInst->config.getString("background_sprite");
            doc["date_color_1"] = dateInst->config.getString("date_color_1");
            doc["date_color_2"] = dateInst->config.getString("date_color_2");
            doc["date_font_path"] = dateInst->config.getString("date_font_path");
        }

        // Weather
        auto weatherInst = getInst("weather_main");
        if (weatherInst) {
            doc["weather_api_key"] = weatherInst->config.getString("api_key");
            doc["weather_city"] = weatherInst->config.getString("city");
            doc["weather_lang"] = weatherInst->config.getString("lang");
            doc["weather_offset_x"] = weatherInst->config.getInt("weather_offset_x");
            doc["weather_offset_y"] = weatherInst->config.getInt("weather_offset_y");
        }

        // System / Time
        doc["lang"] = snap.system.lang;
        doc["timezone"] = snap.system.timezone;
        doc["format_24h"] = snap.system.format24h;

        // Standby
        doc["night_mode_enabled"] = snap.system.night_mode_enabled;
        doc["turn_off_at"] = snap.system.turn_off_at;
        doc["wake_up_at"] = snap.system.wake_up_at;
        doc["night_brightness"] = snap.system.night_brightness;
        doc["idle_fighter_enabled"] = snap.system.idle_fighter_enabled;
        doc["idle_fighter_interval"] = snap.system.idle_fighter_interval;
        doc["idle_fighter_speed"] = snap.system.idle_fighter_speed;
        doc["matrix_power"] = snap.matrix.matrix_power;

        // WiFi
        doc["wifi_ssid"] = snap.wifi.ssid;
        doc["wifi_hostname"] = snap.wifi.hostname;

        // MQTT
        doc["mqtt_enabled"] = snap.mqtt.enabled;
        doc["mqtt_broker"] = snap.mqtt.broker;
        doc["mqtt_port"] = snap.mqtt.port;
        doc["mqtt_user"] = snap.mqtt.user;
        doc["mqtt_pass"] = snap.mqtt.pass;
        doc["mqtt_device"] = snap.mqtt.deviceName;
        doc["mqtt_allow_overlay"] = snap.mqtt.allow_overlay;

        response->setLength();
        request->send(response);
    });

    // API: Settings (POST) — saves immediately to SD
    AsyncCallbackJsonWebHandler* settingsHandler = new AsyncCallbackJsonWebHandler("/api/settings", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        bool willReboot = (!doc["reboot"].isNull() && doc["reboot"].as<bool>());
        bool cryptoChanged = false;
        bool stockChanged = false;
        bool fighterChanged = false;

        config.mutate([&](ConfigLoader& cfg) {
            // Matrix
            if (!doc["brightness_limit"].isNull()) {
                cfg.matrix.powerLimitPercent = doc["brightness_limit"].as<int>();
                extern MatrixEngine matrixEngine;
                matrixEngine.setBrightness(cfg.matrix.powerLimitPercent);
            }
            if (!doc["color_depth"].isNull()) cfg.matrix.colorDepth = doc["color_depth"].as<int>();
            if (!doc["matrix_chain"].isNull()) cfg.matrix.chainLength = doc["matrix_chain"].as<int>();
            if (!doc["matrix_rows"].isNull()) cfg.matrix.height = doc["matrix_rows"].as<int>();
            if (!doc["matrix_cols"].isNull()) cfg.matrix.width = doc["matrix_cols"].as<int>();
            if (!doc["matrix_rgb_sequence"].isNull()) cfg.matrix.rgbSequence = doc["matrix_rgb_sequence"].as<String>();
            if (!doc["matrix_force_single_buffer"].isNull()) cfg.matrix.forceSingleBuffer = doc["matrix_force_single_buffer"].as<bool>();
            if (!doc["matrix_limit_refresh_rate_hz"].isNull()) cfg.matrix.limitRefreshRateHz = doc["matrix_limit_refresh_rate_hz"].as<int>();
            if (!doc["matrix_driver_chip"].isNull()) cfg.matrix.driverChip = doc["matrix_driver_chip"].as<String>();
            if (!doc["matrix_clk_phase"].isNull()) cfg.matrix.clkPhase = doc["matrix_clk_phase"].as<bool>();
            if (!doc["matrix_latch_blanking"].isNull()) cfg.matrix.latchBlanking = doc["matrix_latch_blanking"].as<int>();
            if (!doc["matrix_row_address_mode"].isNull()) cfg.matrix.rowAddressMode = doc["matrix_row_address_mode"].as<int>();
            if (!doc["rotation_offset"].isNull()) {
                cfg.matrix.rotation_offset = doc["rotation_offset"].as<int>();
                displayOrientationManager.setRotationOffset(cfg.matrix.rotation_offset);
            }
            if (!doc["auto_rotate"].isNull()) {
                cfg.matrix.auto_rotate = doc["auto_rotate"].as<bool>();
            }
            if (!doc["rotation_transition"].isNull()) {
                cfg.matrix.rotation_transition = doc["rotation_transition"].as<String>();
                displayOrientationManager.setTransitionEffect(cfg.matrix.rotation_transition);
            }
            if (!doc["rotation_transition_duration_ms"].isNull()) {
                cfg.matrix.rotation_transition_duration_ms = doc["rotation_transition_duration_ms"].as<int>();
                displayOrientationManager.setTransitionDuration(cfg.matrix.rotation_transition_duration_ms);
            }

            auto getInst = [&](const String& id) -> EngineInstance* {
                for (auto& inst : cfg.instances) {
                    if (inst.instance_id == id) return &inst;
                }
                return nullptr;
            };

            auto cryptoInst = getInst("crypto_main");
            if (cryptoInst) {
                if (!doc["crypto_enabled"].isNull()) cryptoInst->config.setBool("enabled", doc["crypto_enabled"].as<bool>());
                if (!doc["crypto_symbols"].isNull()) cryptoInst->config.setString("symbols", doc["crypto_symbols"].as<String>());
                if (!doc["crypto_duration_sec"].isNull()) cryptoInst->config.setInt("duration_sec", doc["crypto_duration_sec"].as<int>());
                if (!doc["crypto_cache_ttl_min"].isNull()) cryptoInst->config.setInt("cache_ttl_min", doc["crypto_cache_ttl_min"].as<int>());
                if (!doc["crypto_currency"].isNull()) cryptoInst->config.setString("currency", doc["crypto_currency"].as<String>());
                cryptoChanged = true;
            }

            auto stockInst = getInst("stock_main");
            if (stockInst) {
                if (!doc["stock_enabled"].isNull()) stockInst->config.setBool("enabled", doc["stock_enabled"].as<bool>());
                if (!doc["stock_symbols"].isNull()) stockInst->config.setString("symbols", doc["stock_symbols"].as<String>());
                if (!doc["stock_duration_sec"].isNull()) stockInst->config.setInt("duration_sec", doc["stock_duration_sec"].as<int>());
                if (!doc["stock_cache_ttl_min"].isNull()) stockInst->config.setInt("cache_ttl_min", doc["stock_cache_ttl_min"].as<int>());
                stockChanged = true;
            }

            auto setRot = [&](const String& id, int dur) {
                for (auto& r : cfg.rotation) if (r.instance_id == id) { r.duration_sec = dur; return; }
            };
            if (!doc["clock_duration_sec"].isNull()) setRot("clock_main", doc["clock_duration_sec"].as<int>());
            if (!doc["date_duration_sec"].isNull()) setRot("date_main", doc["date_duration_sec"].as<int>());
            if (!doc["weather_duration_sec"].isNull()) setRot("weather_main", doc["weather_duration_sec"].as<int>());
            if (!doc["temp_duration_sec"].isNull()) setRot("temp_main", doc["temp_duration_sec"].as<int>());
            if (!doc["decibel_duration_sec"].isNull()) setRot("decibel_main", doc["decibel_duration_sec"].as<int>());
            
            auto fighterInst = getInst("fighter_main");
            if (fighterInst) {
                if (!doc["fighter_interval_sec"].isNull()) fighterInst->config.setInt("fighter_interval_sec", doc["fighter_interval_sec"].as<int>());
                fighterChanged = true;
            }

            if (!doc["temp_unit"].isNull()) cfg.system.unit = doc["temp_unit"].as<String>();
            if (!doc["unit"].isNull()) cfg.system.unit = doc["unit"].as<String>();
            if (!doc["temp_offset"].isNull()) cfg.system.temp_offset = doc["temp_offset"].as<float>();

            auto visInst = getInst("visualizer_main");
            if (visInst) {
                if (!doc["visualizer_enabled"].isNull()) {
                    visInst->config.setBool("enabled", doc["visualizer_enabled"].as<bool>());
                    extern VisualizerEngine* visualizerEngine;
                    if (visualizerEngine) {
                        if (visInst->config.getBool("enabled")) visualizerEngine->activate();
                        else visualizerEngine->deactivate();
                    }
                }
                if (!doc["visualizer_mode"].isNull()) {
                    visInst->config.setString("mode", doc["visualizer_mode"].as<String>());
                    extern VisualizerEngine* visualizerEngine;
                    if (visualizerEngine) visualizerEngine->onConfigChanged(&visInst->config);
                }
                if (!doc["mic_gain"].isNull()) {
                    visInst->config.setString("gain", String(doc["mic_gain"].as<float>()));
                    hardwareHAL.setMicGain(doc["mic_gain"].as<float>());
                }
                if (!doc["db_calibration"].isNull()) visInst->config.setString("db_calibration", String(doc["db_calibration"].as<float>()));
            }

            auto clockInst = getInst("clock_main");
            if (clockInst) {
                bool cChange = false;
                if (!doc["clock_font"].isNull()) { clockInst->config.setInt("clock_font", doc["clock_font"].as<int>()); cChange = true; }
                if (!doc["clock_size"].isNull()) { clockInst->config.setInt("clock_size", doc["clock_size"].as<int>()); cChange = true; }
                if (!doc["clock_offset_x"].isNull()) { clockInst->config.setInt("clock_offset_x", doc["clock_offset_x"].as<int>()); cChange = true; }
                if (!doc["clock_offset_y"].isNull()) { clockInst->config.setInt("clock_offset_y", doc["clock_offset_y"].as<int>()); cChange = true; }
                if (!doc["clock_color_1"].isNull()) { clockInst->config.setString("clock_color_1", doc["clock_color_1"].as<String>()); cChange = true; }
                if (!doc["clock_color_2"].isNull()) { clockInst->config.setString("clock_color_2", doc["clock_color_2"].as<String>()); cChange = true; }
                if (!doc["clock_font_path"].isNull()) { clockInst->config.setString("clock_font_path", doc["clock_font_path"].as<String>()); cChange = true; }
                if (!doc["clock_theme"].isNull()) { clockInst->config.setInt("clock_theme", doc["clock_theme"].as<int>()); cChange = true; }
                
                if (cChange && !willReboot && rotationManager) {
                    rotationManager->notifyConfigChanged("clock_main");
                }
            }

            auto dateInst = getInst("date_main");
            if (dateInst) {
                bool dChange = false;
                if (!doc["date_font"].isNull()) { dateInst->config.setInt("date_font", doc["date_font"].as<int>()); dChange = true; }
                if (!doc["date_size"].isNull()) { dateInst->config.setInt("date_size", doc["date_size"].as<int>()); dChange = true; }
                if (!doc["date_offset_x"].isNull()) { dateInst->config.setInt("date_offset_x", doc["date_offset_x"].as<int>()); dChange = true; }
                if (!doc["date_offset_y"].isNull()) { dateInst->config.setInt("date_offset_y", doc["date_offset_y"].as<int>()); dChange = true; }
                if (!doc["date_format"].isNull()) { dateInst->config.setString("format", doc["date_format"].as<String>()); dChange = true; }
                if (!doc["date_sprite"].isNull()) { dateInst->config.setString("background_sprite", doc["date_sprite"].as<String>()); dChange = true; }
                if (!doc["date_color_1"].isNull()) { dateInst->config.setString("date_color_1", doc["date_color_1"].as<String>()); dChange = true; }
                if (!doc["date_color_2"].isNull()) { dateInst->config.setString("date_color_2", doc["date_color_2"].as<String>()); dChange = true; }
                if (!doc["date_font_path"].isNull()) { dateInst->config.setString("date_font_path", doc["date_font_path"].as<String>()); dChange = true; }
                if (!doc["date_theme"].isNull()) { dateInst->config.setInt("theme", doc["date_theme"].as<int>()); dChange = true; }
                
                if (dChange && !willReboot && rotationManager) {
                    rotationManager->notifyConfigChanged("date_main");
                }
            }

            auto weatherInst = getInst("weather_main");
            if (weatherInst) {
                bool wChange = false;
                if (!doc["weather_api_key"].isNull()) { weatherInst->config.setString("api_key", doc["weather_api_key"].as<String>()); wChange = true; }
                if (!doc["weather_city"].isNull()) { weatherInst->config.setString("city", doc["weather_city"].as<String>()); wChange = true; }
                if (!doc["weather_lang"].isNull()) { weatherInst->config.setString("lang", doc["weather_lang"].as<String>()); wChange = true; }
                if (!doc["weather_offset_x"].isNull()) { weatherInst->config.setInt("weather_offset_x", doc["weather_offset_x"].as<int>()); wChange = true; }
                if (!doc["weather_offset_y"].isNull()) { weatherInst->config.setInt("weather_offset_y", doc["weather_offset_y"].as<int>()); wChange = true; }
                
                if (wChange && !willReboot && rotationManager) {
                    rotationManager->notifyConfigChanged("weather_main");
                }
            }

            if (!doc["lang"].isNull()) {
                String newLang = doc["lang"].as<String>();
                if (newLang != cfg.system.lang) {
                    cfg.system.lang = newLang;
                    if (rotationManager) {
                        for (const auto& inst : cfg.instances) {
                            rotationManager->notifyConfigChanged(inst.instance_id);
                        }
                    }
                }
            }
            if (!doc["night_mode_enabled"].isNull()) cfg.system.night_mode_enabled = doc["night_mode_enabled"].as<bool>();
            if (!doc["turn_off_at"].isNull()) cfg.system.turn_off_at = doc["turn_off_at"].as<String>();
            if (!doc["wake_up_at"].isNull()) cfg.system.wake_up_at = doc["wake_up_at"].as<String>();
            if (!doc["night_brightness"].isNull()) cfg.system.night_brightness = doc["night_brightness"].as<int>();
            if (!doc["idle_fighter_enabled"].isNull()) cfg.system.idle_fighter_enabled = doc["idle_fighter_enabled"].as<bool>();
            if (!doc["idle_fighter_interval"].isNull()) cfg.system.idle_fighter_interval = doc["idle_fighter_interval"].as<int>();
            if (!doc["idle_fighter_speed"].isNull()) cfg.system.idle_fighter_speed = doc["idle_fighter_speed"].as<int>();

            if (!doc["timezone"].isNull()) {
                cfg.system.timezone = doc["timezone"].as<String>();
                configTzTime(getPosixTimezone(cfg.system.timezone).c_str(), "pool.ntp.org");
            }
            if (!doc["format_24h"].isNull()) cfg.system.format24h = doc["format_24h"].as<bool>();

            if (!doc["wifi_ssid"].isNull()) cfg.wifi.ssid = (const char*)doc["wifi_ssid"];
            if (!doc["wifi_password"].isNull() && String((const char*)doc["wifi_password"]) != "") cfg.wifi.password = (const char*)doc["wifi_password"];
            if (!doc["wifi_hostname"].isNull()) cfg.wifi.hostname = (const char*)doc["wifi_hostname"];

            if (!doc["mqtt_enabled"].isNull()) {
                bool newMqtt = (bool)doc["mqtt_enabled"];
                if (newMqtt != cfg.mqtt.enabled) willReboot = true;
                cfg.mqtt.enabled = newMqtt;
            }
            if (!doc["mqtt_broker"].isNull()) {
                String newBroker = (const char*)doc["mqtt_broker"];
                if (newBroker != cfg.mqtt.broker) willReboot = true;
                cfg.mqtt.broker = newBroker;
            }
            if (!doc["mqtt_port"].isNull()) {
                int newPort = (int)doc["mqtt_port"];
                if (newPort != cfg.mqtt.port) willReboot = true;
                cfg.mqtt.port = newPort;
            }
            if (!doc["mqtt_user"].isNull()) cfg.mqtt.user = (const char*)doc["mqtt_user"];
            if (!doc["mqtt_pass"].isNull()) cfg.mqtt.pass = (const char*)doc["mqtt_pass"];
            if (!doc["mqtt_device"].isNull()) cfg.mqtt.deviceName = (const char*)doc["mqtt_device"];
            if (!doc["mqtt_allow_overlay"].isNull()) cfg.mqtt.allow_overlay = (bool)doc["mqtt_allow_overlay"];
        });

        // Sanitize all instances before persisting
        ConfigSanitizer::sanitizeInstances(config);
        bool saved = config.saveToSD("/config.json");
        if (!saved) willReboot = false;   // a reboot now would discard the change

        if (rotationManager && !willReboot) {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            for (const auto& inst : guard->instances) {
                rotationManager->notifyConfigChanged(inst.instance_id);
            }
        }

        if (!saved) { sendConfigSaveFailed(request); return; }
        if (willReboot) {
            request->send(200, "application/json", "{\"status\":\"rebooting\"}");
            delay(500);
            ESP.restart();
        } else {
            request->send(200, "application/json", "{\"status\":\"success\"}");
        }
    });
  server.addHandler(settingsHandler);
    
    // API: Get Selected GIF Playlist
    server.on("/api/playlists/selected", HTTP_GET, [](AsyncWebServerRequest *request){
        bool exists = false;
        String content = "";
        if (xSemaphoreTake(sdMutex, portMAX_DELAY)) {
            FsFile f = sd.open("/playlists_selected.json", FILE_OPEN_READ);
            if (f) {
                exists = true;
                content = f.readString();
                f.close();
            }
            xSemaphoreGive(sdMutex);
        }
        if (exists && content.length() > 0) {
            request->send(200, "application/json", content);
        } else {
            request->send(200, "application/json", "{\"playlists\":[]}");
        }
    });
    
    // API: Save Selected GIF Playlists — write directly to SD


    // API: Send Marquee Message
    AsyncCallbackJsonWebHandler* msgHandler = new AsyncCallbackJsonWebHandler("/api/message", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        
        MessageConfig cfg;
        cfg.text = doc["text"] | "Hello";
        
        if (doc["color"].is<const char*>()) {
            const char* hex = doc["color"].as<const char*>();
            if (hex[0] == '#') hex++;
            long val = strtol(hex, NULL, 16);
            extern MatrixEngine matrixEngine;
            cfg.color = matrixEngine.getDisplay() ? matrixEngine.getDisplay()->color565((val >> 16) & 0xFF, (val >> 8) & 0xFF, val & 0xFF) : 0xFFFF;
        } else {
            cfg.color = doc["color"] | 63488;
        }
        
        cfg.size = doc["size"] | 2;
        
        String dir = doc["direction"] | "rtl";
        if (dir == "left") dir = "rtl";
        else if (dir == "right") dir = "ltr";
        else if (dir == "down") dir = "ttb";
        else if (dir == "up") dir = "btt";
        cfg.direction = dir;
        
        cfg.speed = doc["speed"] | 30;
        cfg.timeoutSeconds = doc["timeoutSeconds"] | 30;
        
        if (msg) msg->displayMessage(cfg);
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(msgHandler);
    
    // API: Change Clock Theme (also updates config + saves to SD)
    AsyncCallbackJsonWebHandler* clockHandler = new AsyncCallbackJsonWebHandler("/api/clock", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        
        int themeId = doc["clock_theme"] | doc["characterId"] | 0;
        extern ConfigLoader config;
        config.mutate([&](ConfigLoader& cfg) {
            for (auto& inst : cfg.instances) {
                if (inst.instance_id == "clock_main") {
                    inst.config.setInt("clock_theme", themeId);
                    break;
                }
            }
        });
        if (rotationManager) {
            rotationManager->notifyConfigChanged("clock_main");
        }
        bool saved = config.saveToSD("/config.json");
            if (!saved) {
                request->send(200, "application/json", "{\"success\":true,\"sd_saved\":false,\"warning\":\"Theme applied but could not be saved to the SD card - it will revert on reboot.\"}");
                return;
            }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(clockHandler);

    // API: Toggle Panel Power
    AsyncCallbackJsonWebHandler* powerHandler = new AsyncCallbackJsonWebHandler("/api/system/power", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        
        if (!doc["state"].isNull()) {
            config.mutate([&](ConfigLoader& cfg) {
                cfg.matrix.matrix_power = doc["state"].as<bool>();
            });
        }
        SpiRamJsonDocument resp(1024);
        resp["status"] = "success";
        resp["matrix_power"] = config.acquireSnapshot()->matrix.matrix_power;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);
    });
    server.addHandler(powerHandler);

    // API: System settings (GET /api/system)
    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request){
        extern ConfigLoader config;
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        const ConfigSnapshot& snap = guard.get();
        SpiRamJsonDocument doc(4096);
        JsonObject sys = doc.createNestedObject("system");
        sys["lang"] = snap.system.lang.length() > 0 ? snap.system.lang : "fr";
        sys["timezone"] = snap.system.timezone;
        sys["format_24h"] = snap.system.format24h;
        sys["unit"] = snap.system.unit;
        sys["temp_unit"] = snap.system.unit;
        sys["temp_offset"] = snap.system.temp_offset;
        sys["night_mode_enabled"] = snap.system.night_mode_enabled;
        sys["turn_off_at"] = snap.system.turn_off_at;
        sys["wake_up_at"] = snap.system.wake_up_at;
        sys["night_brightness"] = snap.system.night_brightness;
        sys["day_brightness"] = snap.matrix.powerLimitPercent;
        sys["brightness_limit"] = snap.matrix.powerLimitPercent;
        sys["idle_fighter_enabled"] = snap.system.idle_fighter_enabled;
        sys["idle_fighter_interval"] = snap.system.idle_fighter_interval;
        sys["idle_fighter_speed"] = snap.system.idle_fighter_speed;

        JsonObject mat = doc.createNestedObject("matrix");
        mat["height"] = snap.matrix.height;
        mat["width"] = snap.matrix.width;
        mat["chain_length"] = snap.matrix.chainLength;
        mat["parallel"] = 1;
        mat["driver_chip"] = snap.matrix.driverChip;
        mat["row_address_mode"] = snap.matrix.rowAddressMode;
        mat["rgb_sequence"] = snap.matrix.rgbSequence;
        mat["color_depth"] = snap.matrix.colorDepth;
        mat["pwm_bits"] = snap.matrix.colorDepth;
        mat["limit_refresh_rate_hz"] = snap.matrix.limitRefreshRateHz;
        mat["clk_phase"] = snap.matrix.clkPhase;
        mat["latch_blanking"] = snap.matrix.latchBlanking;
        mat["force_single_buffer"] = snap.matrix.forceSingleBuffer;
        mat["rotation_offset"] = snap.matrix.rotation_offset;
        mat["auto_rotate"] = snap.matrix.auto_rotate;
        mat["rotation_transition"] = snap.matrix.rotation_transition;
        mat["rotation_transition_duration_ms"] = snap.matrix.rotation_transition_duration_ms;

        JsonObject mqtt = doc.createNestedObject("mqtt");
        mqtt["enabled"] = snap.mqtt.enabled;
        mqtt["broker"] = snap.mqtt.broker;
        mqtt["port"] = snap.mqtt.port;
        mqtt["user"] = snap.mqtt.user;
        mqtt["pass"] = snap.mqtt.pass;
        mqtt["device_name"] = snap.mqtt.deviceName;
        mqtt["allow_overlay"] = snap.mqtt.allow_overlay;

        JsonObject wifi = doc.createNestedObject("wifi");
        wifi["ssid"] = snap.wifi.ssid;
        wifi["hostname"] = snap.wifi.hostname;

        JsonObject hw = doc.createNestedObject("hardware");
        const auto& caps = hardwareHAL.capabilities();
        hw["profile"] = (caps.profile == HwProfile::WAVESHARE_S3) ? "WAVESHARE_S3" : "ESP32_STD";
        JsonObject psramObj = hw.createNestedObject("psram");
        psramObj["available"] = caps.hasPsram;
        psramObj["bytes"] = caps.psramBytes;
        hw["microphone"] = caps.hasMicrophone;
        hw["temperature_sensor"] = caps.hasTempSensor;
        hw["gyroscope"] = gyroHAL.isAvailable();

        doc["api_auth_enabled"] = false;
        doc["api_token"] = "";

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });

    // API: System settings update (POST /api/system)
    AsyncCallbackJsonWebHandler* sysHandler = new AsyncCallbackJsonWebHandler("/api/system", [this](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject doc = json.as<JsonObject>();
        extern ConfigLoader config;
        bool changed = false;
        bool willReboot = false;
        if (doc.containsKey("reboot") && doc["reboot"].as<bool>()) willReboot = true;
        bool langChanged = false;

        config.mutate([&](ConfigLoader& cfg) {
            JsonObject sys = doc.containsKey("system") ? doc["system"].as<JsonObject>() : doc;
            if (!sys["lang"].isNull()) {
                String newLang = sys["lang"].as<String>();
                if (newLang != cfg.system.lang) {
                    cfg.system.lang = newLang;
                    changed = true;
                    langChanged = true;
                }
            }
            if (!sys["timezone"].isNull()) {
                cfg.system.timezone = sys["timezone"].as<String>();
                configTzTime(getPosixTimezone(cfg.system.timezone).c_str(), "pool.ntp.org");
                changed = true;
            }
            if (!sys["format_24h"].isNull()) {
                cfg.system.format24h = sys["format_24h"].as<bool>();
                changed = true;
            }
            if (!sys["unit"].isNull()) {
                cfg.system.unit = sys["unit"].as<String>();
                changed = true;
            }
            if (!sys["temp_unit"].isNull()) {
                cfg.system.unit = sys["temp_unit"].as<String>();
                changed = true;
            }
            if (!sys["temp_offset"].isNull()) {
                cfg.system.temp_offset = sys["temp_offset"].as<float>();
                changed = true;
            }
            if (!sys["night_mode_enabled"].isNull()) {
                cfg.system.night_mode_enabled = sys["night_mode_enabled"].as<bool>();
                changed = true;
            }
            if (!sys["turn_off_at"].isNull()) {
                cfg.system.turn_off_at = sys["turn_off_at"].as<String>();
                changed = true;
            }
            if (!sys["wake_up_at"].isNull()) {
                cfg.system.wake_up_at = sys["wake_up_at"].as<String>();
                changed = true;
            }
            if (!sys["night_brightness"].isNull()) {
                cfg.system.night_brightness = sys["night_brightness"].as<int>();
                changed = true;
            }
            if (!sys["brightness_limit"].isNull() || !sys["brightness"].isNull() || !sys["day_brightness"].isNull()) {
                int b = !sys["brightness_limit"].isNull() ? sys["brightness_limit"].as<int>() : (!sys["brightness"].isNull() ? sys["brightness"].as<int>() : sys["day_brightness"].as<int>());
                if (b < 1) b = 1;
                if (b > 100) b = 100;
                cfg.matrix.powerLimitPercent = b;
                extern MatrixEngine matrixEngine;
                matrixEngine.setBrightness(b);
                changed = true;
            }
            if (!sys["idle_fighter_enabled"].isNull()) {
                cfg.system.idle_fighter_enabled = sys["idle_fighter_enabled"].as<bool>();
                changed = true;
            }
            if (!sys["idle_fighter_interval"].isNull()) {
                cfg.system.idle_fighter_interval = sys["idle_fighter_interval"].as<int>();
                changed = true;
            }
            if (!sys["idle_fighter_speed"].isNull()) {
                cfg.system.idle_fighter_speed = sys["idle_fighter_speed"].as<int>();
                changed = true;
            }

            if (doc.containsKey("matrix")) {
                JsonObject mat = doc["matrix"].as<JsonObject>();
                if (!mat["height"].isNull()) cfg.matrix.height = mat["height"].as<int>();
                if (!mat["width"].isNull()) cfg.matrix.width = mat["width"].as<int>();
                if (!mat["chain_length"].isNull()) cfg.matrix.chainLength = mat["chain_length"].as<int>();
                if (!mat["driver_chip"].isNull()) cfg.matrix.driverChip = mat["driver_chip"].as<String>();
                if (!mat["row_address_mode"].isNull()) cfg.matrix.rowAddressMode = mat["row_address_mode"].as<int>();
                if (!mat["rgb_sequence"].isNull()) cfg.matrix.rgbSequence = mat["rgb_sequence"].as<String>();
                if (!mat["pwm_bits"].isNull()) cfg.matrix.colorDepth = mat["pwm_bits"].as<int>();
                else if (!mat["color_depth"].isNull()) cfg.matrix.colorDepth = mat["color_depth"].as<int>();
                if (!mat["limit_refresh_rate_hz"].isNull()) cfg.matrix.limitRefreshRateHz = mat["limit_refresh_rate_hz"].as<int>();
                if (!mat["clk_phase"].isNull()) cfg.matrix.clkPhase = mat["clk_phase"].as<bool>();
                else if (!mat["clkPhase"].isNull()) cfg.matrix.clkPhase = mat["clkPhase"].as<bool>();
                if (!mat["latch_blanking"].isNull()) cfg.matrix.latchBlanking = mat["latch_blanking"].as<int>();
                else if (!mat["latchBlanking"].isNull()) cfg.matrix.latchBlanking = mat["latchBlanking"].as<int>();
                if (!mat["force_single_buffer"].isNull()) cfg.matrix.forceSingleBuffer = mat["force_single_buffer"].as<bool>();
                else if (!mat["forceSingleBuffer"].isNull()) cfg.matrix.forceSingleBuffer = mat["forceSingleBuffer"].as<bool>();
                if (!mat["rotation_offset"].isNull()) {
                    cfg.matrix.rotation_offset = mat["rotation_offset"].as<int>();
                    displayOrientationManager.setRotationOffset(cfg.matrix.rotation_offset);
                }
                if (!mat["auto_rotate"].isNull()) cfg.matrix.auto_rotate = mat["auto_rotate"].as<bool>();
                if (!mat["rotation_transition"].isNull()) {
                    cfg.matrix.rotation_transition = mat["rotation_transition"].as<String>();
                    displayOrientationManager.setTransitionEffect(cfg.matrix.rotation_transition);
                }
                if (!mat["rotation_transition_duration_ms"].isNull()) {
                    cfg.matrix.rotation_transition_duration_ms = mat["rotation_transition_duration_ms"].as<int>();
                    displayOrientationManager.setTransitionDuration(cfg.matrix.rotation_transition_duration_ms);
                }
                changed = true;
                willReboot = true;
            }

            if (doc.containsKey("mqtt")) {
                JsonObject mq = doc["mqtt"].as<JsonObject>();
                bool prevMqtt = cfg.mqtt.enabled;
                String prevBroker = cfg.mqtt.broker;
                int prevPort = cfg.mqtt.port;
                if (!mq["enabled"].isNull()) cfg.mqtt.enabled = mq["enabled"].as<bool>();
                if (!mq["broker"].isNull()) cfg.mqtt.broker = mq["broker"].as<String>();
                if (!mq["port"].isNull()) cfg.mqtt.port = mq["port"].as<int>();
                if (!mq["user"].isNull()) cfg.mqtt.user = mq["user"].as<String>();
                if (!mq["pass"].isNull()) cfg.mqtt.pass = mq["pass"].as<String>();
                if (!mq["device_name"].isNull()) cfg.mqtt.deviceName = mq["device_name"].as<String>();
                if (!mq["allow_overlay"].isNull()) cfg.mqtt.allow_overlay = mq["allow_overlay"].as<bool>();
                changed = true;
                if (prevMqtt != cfg.mqtt.enabled || prevBroker != cfg.mqtt.broker || prevPort != cfg.mqtt.port) {
                    willReboot = true;
                }
            }
        });

        bool saved = true;
        if (changed) {
            ConfigSanitizer::sanitize(config);
            saved = config.saveToSD("/config.json");
            if (!saved) willReboot = false;   // a reboot now would discard the change
        }

        if ((changed || langChanged) && rotationManager && !willReboot) {
            ConfigSnapshotGuard guard = config.acquireSnapshot();
            for (const auto& inst : guard->instances) {
                rotationManager->notifyConfigChanged(inst.instance_id);
            }
        }

        if (!saved) { sendConfigSaveFailed(request); return; }
        SpiRamJsonDocument resp(512);
        resp["status"] = willReboot ? "rebooting" : "success";
        resp["lang"] = config.acquireSnapshot()->system.lang;
        String response;
        serializeJson(resp, response);
        request->send(200, "application/json", response);

        if (willReboot) {
            xTaskCreate([](void *param) {
                vTaskDelay(pdMS_TO_TICKS(500));
                ESP.restart();
            }, "config_reboot_task", 2048, NULL, 1, NULL);
        }
    });
    sysHandler->setFilter([](AsyncWebServerRequest *request) {
        return request->url() == "/api/system";
    });
    server.addHandler(sysHandler);
    
    // API: System commands (Reboot / Shutdown / Restart)
    server.on("/api/system/shutdown", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "shutdown_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/reboot", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "reboot_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/restart", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "restart_task", 2048, NULL, 1, NULL);
    });
    server.on("/api/system/restart_app", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true}");
        xTaskCreate([](void *param) {
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
        }, "restart_app_task", 2048, NULL, 1, NULL);
    });
    
    static std::atomic<bool> s_otaRunning{false};
    static std::atomic<int> s_otaProgressPercent{0};
    static std::atomic<bool> s_otaUploadSuccess{false};

    // API: GET /api/ota/check (Parity with RPi & OpenAPI specification)
    server.on("/api/ota/check", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(512);
        doc["current_version"] = FIRMWARE_VERSION;
        #if defined(CONFIG_IDF_TARGET_ESP32S3)
        doc["current_arch"] = "esp32s3";
        #else
        doc["current_arch"] = "esp32";
        #endif
        doc["latest_version"] = FIRMWARE_VERSION;
        doc["update_available"] = false;
        doc["supported"] = true;
        doc["in_progress"] = s_otaRunning.load(std::memory_order_relaxed);
        doc["progress"] = s_otaProgressPercent.load(std::memory_order_relaxed);
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/ota/auto-update (Autonomous ESP32 background download & flash)
    // Registered BEFORE generic /api/ota to prevent ESPAsyncWebServer prefix matching collision
    AsyncCallbackJsonWebHandler* autoOtaHandler = new AsyncCallbackJsonWebHandler("/api/ota/auto-update", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON payload\"}");
            return;
        }
        JsonObject body = json.as<JsonObject>();
        if (body["download_url"].isNull()) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing download_url\"}");
            return;
        }
        String downloadUrl = body["download_url"].as<String>();
        if (!downloadUrl.startsWith("http://") && !downloadUrl.startsWith("https://")) {
            request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid download URL scheme\"}");
            return;
        }

        if (s_otaRunning.exchange(true)) {
            request->send(409, "application/json", "{\"status\":\"error\",\"message\":\"OTA update already in progress\"}");
            return;
        }

        s_otaProgressPercent.store(0, std::memory_order_relaxed);
        request->send(200, "application/json", "{\"status\":\"ok\",\"message\":\"OTA download initiated\"}");

        struct AutoOtaParams {
            String url;
        };
        AutoOtaParams* params = new AutoOtaParams{ downloadUrl };

        xTaskCreatePinnedToCore([](void *param) {
            AutoOtaParams* p = static_cast<AutoOtaParams*>(param);
            String url = p->url;
            delete p;

            const esp_partition_t* targetPart = esp_ota_get_next_update_partition(NULL);
            LOGI("OTA", "Auto-OTA starting download from %s -> target partition %s (0x%08X)",
                 url.c_str(), targetPart ? targetPart->label : "unknown", targetPart ? (unsigned)targetPart->address : 0);
            vTaskDelay(pdMS_TO_TICKS(600)); // allow HTTP 200 response to flush over network

            extern MatrixEngine matrixEngine;
            if (matrixEngine.getDisplay()) {
                matrixEngine.getDisplay()->fillScreen(0);
                matrixEngine.present();
            }

            WiFiClientSecure secureClient;
            secureClient.setInsecure();
            secureClient.setTimeout(30000); // 30 seconds timeout (unit is milliseconds in Arduino Stream)

            httpUpdate.setLedPin(-1);
            httpUpdate.rebootOnUpdate(false);
            httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
            httpUpdate.onProgress([](int cur, int total) {
                if (total > 0) {
                    int pct = (cur * 100) / total;
                    s_otaProgressPercent.store(pct, std::memory_order_relaxed);
                    static int lastLoggedPct = -1;
                    if (pct / 10 != lastLoggedPct / 10) {
                        lastLoggedPct = pct;
                        LOGI("OTA", "Auto-OTA progress: %d%% (%d/%d bytes)", pct, cur, total);
                    }
                }
            });

            t_httpUpdate_return ret = httpUpdate.update(secureClient, url);
            if (ret == HTTP_UPDATE_OK) {
                const esp_partition_t* bootPart = esp_ota_get_boot_partition();
                LOGI("OTA", "Auto-OTA succeeded! Next boot partition: %s (0x%08X). Rebooting...",
                     bootPart ? bootPart->label : "unknown", bootPart ? (unsigned)bootPart->address : 0);
                s_otaProgressPercent.store(100, std::memory_order_relaxed);
                vTaskDelay(pdMS_TO_TICKS(250));
                if (matrixEngine.getDisplay()) {
                    matrixEngine.getDisplay()->fillScreen(0);
                    matrixEngine.present();
                }
                WiFi.disconnect(true, true);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            } else {
                LOGE("OTA", "Auto-OTA failed: (%d) %s", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
                s_otaRunning.store(false, std::memory_order_relaxed);
                s_otaProgressPercent.store(0, std::memory_order_relaxed);
            }

            vTaskDelete(NULL);
        }, "ota_auto_worker", 16384, params, 1, NULL, 0);
    });
    server.addHandler(autoOtaHandler);

    // API: OTA Firmware Update (/api/update and /api/ota alias)
    auto otaResponseHandler = [](AsyncWebServerRequest *request) {
        bool shouldReboot = s_otaUploadSuccess.exchange(false) && !Update.hasError();
        AsyncWebServerResponse *response = request->beginResponse(
            shouldReboot ? 200 : 400,
            "application/json",
            shouldReboot ? "{\"status\":\"success\",\"message\":\"Update successful, rebooting...\"}"
                         : "{\"status\":\"error\",\"message\":\"No firmware received or update failed\"}"
        );
        response->addHeader("Connection", "close");
        request->send(response);

        if (shouldReboot) {
            const esp_partition_t* bootPart = esp_ota_get_boot_partition();
            LOGI("OTA", "OTA Upload verified! Next boot partition: %s (0x%08X). Rebooting...",
                 bootPart ? bootPart->label : "unknown", bootPart ? (unsigned)bootPart->address : 0);
            xTaskCreate([](void *param) {
                vTaskDelay(pdMS_TO_TICKS(250));
                extern MatrixEngine matrixEngine;
                if (matrixEngine.getDisplay()) {
                    matrixEngine.getDisplay()->fillScreen(0);
                    matrixEngine.present();
                }
                WiFi.disconnect(true, true);
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
            }, "ota_reboot", 4096, NULL, configMAX_PRIORITIES - 1, NULL);
        } else {
            LOGW("OTA", "OTA request received without valid upload or with errors. Reboot cancelled.");
        }
    };

    auto otaUploadHandler = [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            const esp_partition_t* nextPart = esp_ota_get_next_update_partition(NULL);
            LOGI("OTA", "Update Start: %s -> target partition %s (0x%08X)",
                 filename.c_str(), nextPart ? nextPart->label : "unknown", nextPart ? (unsigned)nextPart->address : 0);
            extern GifEngine* gifEngine;
            if (gifEngine) gifEngine->stop();
            s_otaUploadSuccess.store(false, std::memory_order_relaxed);
            if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                Update.printError(Serial);
            }
        }
        if (!Update.hasError()) {
            if (Update.write(data, len) != len) {
                Update.printError(Serial);
            }
        }
        if (final) {
            if (Update.end(true)) {
                LOGI("OTA", "Update Success: %uB written.", index + len);
                s_otaUploadSuccess.store(true, std::memory_order_relaxed);
            } else {
                Update.printError(Serial);
                s_otaUploadSuccess.store(false, std::memory_order_relaxed);
            }
        }
    };

    server.on("/api/update", HTTP_POST, otaResponseHandler, otaUploadHandler)
        .setFilter([](AsyncWebServerRequest *req) { return req->url().equals("/api/update"); });
    server.on("/api/ota", HTTP_POST, otaResponseHandler, otaUploadHandler)
        .setFilter([](AsyncWebServerRequest *req) { return req->url().equals("/api/ota"); });

    // =====================================================================================
    // GIF LIBRARY MANAGEMENT (parity with ArcadeMatrix_RPI: /api/gifs/*)
    //   GET    /api/gifs/library                  -> live scan of /gifs (folders + counts)
    //   POST   /api/gifs/upload?folder=<Name>     -> multipart upload of one or many files
    //   POST   /api/gifs/reindex                  -> rebuild index.txt + playlists.json
    //   DELETE /api/gifs/file?folder=<N>&name=<F> -> delete one file, refresh indexes
    // =====================================================================================
    {
        struct GifUploadCtx {
            String folder;
            String orientation = "yoko";
            String root = "/gifs";   // resolved from ?orientation= when the first body chunk arrives
            FsFile file;
            String currentName;
            bool currentOk = false;
            size_t currentBytes = 0;
            String saved;    // JSON array body (without brackets)
            String skipped;  // JSON array body of {"name","reason"}
            int savedCount = 0;
            std::vector<String> savedNames;
            bool engineStopped = false;
            bool badFolder = false;
            bool modeOn = false;   // rotation suspended + "upload mode" notice on the panel
            ~GifUploadCtx() { if (file) file.close(); }
        };

        // Upload mode: per DEVELOPER.md §3 (Golden Rules #2/#5 - lock-free render core, sdMutex around SD access) the
        // S3 cannot absorb a multi-file SD write while an engine renders, so for the duration of an upload the
        // rotation is suspended and the panel shows a notice (same pattern as the OTA handler stopping the GifEngine).
        // Both are undone when the request completes or the client disconnects. Idempotent on purpose.
        auto enterUploadMode = [this]() {
            extern RotationManager* rotationManager;
            if (rotationManager) rotationManager->setSuspended(true);
            if (msg) {
                MessageConfig m;
                m.text = "Upload mode - receiving files...";
                m.offsetY = -8;   // one text line above centre
                m.color = 0x07FF;
                m.size = 1;
                m.direction = "rtl";
                m.speed = 50;
                m.timeoutSeconds = 1800;
                msg->queueMessage(m);
            }
        };
        auto leaveUploadMode = [this]() {
            extern RotationManager* rotationManager;
            if (msg) msg->deactivate();
            if (rotationManager) rotationManager->setSuspended(false);
        };

        static auto sanitizeName = [](const String& in, bool allowExt) -> String {
            String s = in;
            int slash = s.lastIndexOf('/'); if (slash >= 0) s = s.substring(slash + 1);
            slash = s.lastIndexOf('\\'); if (slash >= 0) s = s.substring(slash + 1);
            String out = "";
            for (size_t i = 0; i < s.length(); i++) {
                char c = s[i];
                bool ok = isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ' || (allowExt && c == '.');
                if (ok) out += c;
            }
            out.trim();
            while (out.startsWith(".")) out = out.substring(1);
            if (out.length() > 64) out = out.substring(0, 64);
            return out;
        };
        static auto hasGifExt = [](const String& name) -> bool {
            String l = name; l.toLowerCase();
            return l.endsWith(".gif") || l.endsWith(".png") || l.endsWith(".raw");
        };
        static auto jsonEsc = [](const String& s) -> String {
            String o = ""; for (size_t i = 0; i < s.length(); i++) { char c = s[i]; if (c == '"' || c == '\\') o += '\\'; o += c; } return o;
        };

        // List media files of one folder (sorted order not required by the engine).
        static auto listFolderFiles = [](const String& folderPath, std::vector<String>& out) {
            out.clear();
            FsFile dir = sd.open(folderPath.c_str(), FILE_OPEN_READ);
            if (!dir || !isDirectory(dir)) return;
            FsFile f;
            while (getNextFile(dir, f)) {
                if (isDirectory(f)) continue;
                if (g_gifReindex.running) { g_gifReindex.files = g_gifReindex.files + 1; if ((g_gifReindex.files % 64) == 0) vTaskDelay(1); if (millis() - g_gifReindex.lastMsgMs > 30000) gifReindexShowProgress(); }   // 30 s: each refresh restarts the scroll
                String n = getFileName(f);
                int ls = n.lastIndexOf('/'); if (ls >= 0) n = n.substring(ls + 1);
                if (!isMacJunk(n) && n != "index.txt" && hasGifExt(n)) out.push_back(n);
            }
            dir.close();
        };

        // Rewrite <root>/<folder>/index.txt from the real directory contents.
        static auto writeFolderIndex = [](const String& root, const String& folder) -> int {
            std::vector<String> files; listFolderFiles(root + "/" + folder, files);
            String idxPath = root + "/" + folder + "/index.txt";
            FsFile idx = sd.open(idxPath.c_str(), FILE_OPEN_WRITE);
            if (idx) { for (auto& n : files) { idx.print(n); idx.print('\n'); } idx.close(); }
            return (int)files.size();
        };

        // Rewrite <root>/playlists.json from the real directory contents (same shape as generate_index.sh).
        static auto writePlaylistsJson = [](const String& root) -> String {
            String json = "{"; bool first = true;
            FsFile dir = sd.open(root.c_str(), FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile f;
                while (getNextFile(dir, f)) {
                    if (!isDirectory(f)) continue;
                    String name = getFileName(f); int ls = name.lastIndexOf('/'); if (ls >= 0) name = name.substring(ls + 1);
                    if (isMacJunk(name)) continue;
                    int count = writeFolderIndex(root, name);
                    if (count <= 0) continue;
                    if (!first) json += ",";
                    json += "\"" + jsonEsc(name) + "\":{\"path\":\"" + root + "/" + jsonEsc(name) + "\",\"count\":" + String(count) + "}";
                    first = false;
                }
                dir.close();
            }
            json += "}";
            String plPath = root + "/playlists.json";
            FsFile pl = sd.open(plPath.c_str(), FILE_OPEN_WRITE);
            if (pl) { pl.print(json); pl.close(); }
            return json;
        };

        // ---- incremental index maintenance (no directory walks in request handlers) -------------------------
        // index.txt is the source of truth per folder; playlists.json is rebuilt from the index line counts only.
        static auto indexLineCount = [](const String& root, const String& folder) -> int {
            String p = root + "/" + folder + "/index.txt";
            FsFile f = sd.open(p.c_str(), FILE_OPEN_READ);
            if (!f) return 0;
            int n = 0; bool lastNl = true; uint8_t buf[256]; int r;
            while ((r = f.read(buf, sizeof(buf))) > 0) { for (int i = 0; i < r; i++) if (buf[i] == '\n') n++; lastNl = (buf[r - 1] == '\n'); }
            if (!lastNl) n++;
            f.close();
            return n;
        };
        // Rewrite index.txt once: current lines minus `remove` plus `add` (deduplicated). Returns the new count.
        static auto updateFolderIndex = [](const String& root, const String& folder, const std::vector<String>& add, const std::vector<String>& remove) -> int {
            String p = root + "/" + folder + "/index.txt";
            std::vector<String> lines;
            FsFile f = sd.open(p.c_str(), FILE_OPEN_READ);
            if (f) {
                while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length()) lines.push_back(l); }
                f.close();
            }
            auto contains = [](const std::vector<String>& v, const String& x) { for (auto& e : v) if (e == x) return true; return false; };
            std::vector<String> out;
            for (auto& l : lines) if (!contains(remove, l) && !contains(out, l)) out.push_back(l);
            for (auto& a : add) if (a.length() && !contains(out, a) && !contains(remove, a)) out.push_back(a);
            FsFile w = sd.open(p.c_str(), FILE_OPEN_WRITE);
            if (w) { for (auto& l : out) { w.print(l); w.print('\n'); } w.close(); }
            return (int)out.size();
        };
        // playlists.json from the folders' index.txt line counts (cheap: one small read per folder, no file walks).
        static auto buildPlaylistsFromIndexes = [](const String& root) -> String {
            String json = "{"; bool first = true;
            FsFile dir = sd.open(root.c_str(), FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile f;
                while (getNextFile(dir, f)) {
                    if (!isDirectory(f)) continue;
                    String name = getFileName(f); int ls = name.lastIndexOf('/'); if (ls >= 0) name = name.substring(ls + 1);
                    if (isMacJunk(name)) continue;
                    int count = indexLineCount(root, name);
                    if (!first) json += ",";
                    json += "\"" + jsonEsc(name) + "\":{\"path\":\"" + root + "/" + jsonEsc(name) + "\",\"count\":" + String(count) + "}";
                    first = false;
                }
                dir.close();
            }
            json += "}";
            String plPath = root + "/playlists.json";
            FsFile pl = sd.open(plPath.c_str(), FILE_OPEN_WRITE);
            if (pl) { pl.print(json); pl.close(); }
            return json;
        };
        static auto readPlaylistsJson = [](const String& root) -> String {
            String plPath = root + "/playlists.json";
            FsFile pl = sd.open(plPath.c_str(), FILE_OPEN_READ);
            if (!pl) return String();
            String json; json.reserve(pl.size() + 16);
            uint8_t buf[256]; int r;
            while ((r = pl.read(buf, sizeof(buf))) > 0) for (int i = 0; i < r; i++) json += (char)buf[i];
            pl.close();
            json.trim();
            return json.startsWith("{") ? json : String();
        };
        // Set (count >= 0) or remove (count < 0) one folder entry in playlists.json; falls back to a rebuild when the
        // file is missing or unparsable. Returns the JSON written.
        static auto updatePlaylistsEntry = [](const String& root, const String& folder, int count) -> String {
            String cur = readPlaylistsJson(root);
            if (cur.isEmpty()) cur = buildPlaylistsFromIndexes(root);
            size_t cap = cur.length() * 2 + 1024; if (cap < 4096) cap = 4096; if (cap > 24576) cap = 24576;
            SpiRamJsonDocument doc(cap);   // Golden Rule #14: application JSON lives in PSRAM
            if (deserializeJson(doc, cur) != DeserializationError::Ok || !doc.is<JsonObject>()) {
                cur = buildPlaylistsFromIndexes(root);
                doc.clear();
                if (deserializeJson(doc, cur) != DeserializationError::Ok) return cur;
            }
            JsonObject obj = doc.as<JsonObject>();
            if (count < 0) obj.remove(folder);
            else { JsonObject e = obj.containsKey(folder) ? obj[folder].as<JsonObject>() : obj.createNestedObject(folder); e["path"] = root + "/" + folder; e["count"] = count; }
            String out; serializeJson(doc, out);
            String plPath = root + "/playlists.json";
            FsFile pl = sd.open(plPath.c_str(), FILE_OPEN_WRITE);
            if (pl) { pl.print(out); pl.close(); }
            return out;
        };
        g_gifWriteFolderIndex = writeFolderIndex;
        g_gifBuildPlaylistsFromIndexes = buildPlaylistsFromIndexes;
        g_gifIsMacJunk = isMacJunk;
        g_gifReadPlaylistsJson = readPlaylistsJson;

        // Live library scan (does NOT touch index files).
        static auto scanLibrary = [](const String& root) -> String {
            String json = "{"; bool first = true;
            FsFile dir = sd.open(root.c_str(), FILE_OPEN_READ);
            if (dir && isDirectory(dir)) {
                FsFile f;
                while (getNextFile(dir, f)) {
                    if (!isDirectory(f)) continue;
                    String name = getFileName(f); int ls = name.lastIndexOf('/'); if (ls >= 0) name = name.substring(ls + 1);
                    if (isMacJunk(name)) continue;
                    std::vector<String> files; listFolderFiles(root + "/" + name, files);
                    if (!first) json += ",";
                    json += "\"" + jsonEsc(name) + "\":{\"path\":\"" + root + "/" + jsonEsc(name) + "\",\"count\":" + String(files.size()) + "}";
                    first = false;
                }
                dir.close();
            }
            json += "}";
            return json;
        };

        server.on("/api/gifs/library", HTTP_GET, [](AsyncWebServerRequest *request){
            // Served from playlists.json (kept current by the upload/delete/rename handlers); never walks the files here.
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String folders = "{}";
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) { request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                folders = readPlaylistsJson(root);
                if (folders.isEmpty()) folders = buildPlaylistsFromIndexes(root);
            }
            request->send(200, "application/json", "{\"root\":\"" + root + "\",\"orientation\":\"" + orientation + "\",\"reindexing\":" + String(g_gifReindex.running ? "true" : "false") + ",\"folders\":" + folders + "}");
        });

        // Full rescan of every folder from the real directory contents: runs in the background (202) — see gifReindexTask.
        server.on("/api/gifs/reindex", HTTP_POST, [](AsyncWebServerRequest *request){
            // Claim the slot atomically: a plain check-then-set lets two concurrent requests both
            // start, and the first to finish clears the panel notice and the flag under the second.
            bool expected = false;
            if (!g_gifReindex.running.compare_exchange_strong(expected, true)) {
                request->send(409, "application/json", "{\"status\":\"busy\",\"message\":\"Reindex already running\"}");
                return;
            }
            g_gifReindex.total = 0; g_gifReindex.done = 0; g_gifReindex.files = 0; g_gifReindex.expected = 0; g_gifReindex.cancel = false; g_gifReindex.lastMsgMs = 0; g_gifReindex.current = ""; g_gifReindex.lastResult = "";
            g_gifReindex.startedMs = millis(); g_gifReindex.finishedMs = 0;
            gifReindexEnterMaintenance();
            if (xTaskCreatePinnedToCore(gifReindexTask, "gif_reindex", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
                g_gifReindex.running = false; g_gifReindex.lastResult = "error: task";
                gifReindexLeaveMaintenance();
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Could not start reindex task\"}");
                return;
            }
            request->send(202, "application/json", "{\"status\":\"started\"}");
        });
        server.on("/api/gifs/reindex", HTTP_DELETE, [](AsyncWebServerRequest *request){
            if (!g_gifReindex.running) { request->send(409, "application/json", "{\"status\":\"idle\"}"); return; }
            g_gifReindex.cancel = true;   // honoured between folders; the folder being indexed completes first
            request->send(202, "application/json", "{\"status\":\"cancelling\"}");
        });
        server.on("/api/gifs/reindex/status", HTTP_GET, [](AsyncWebServerRequest *request){
            String j = "{\"running\":" + String(g_gifReindex.running ? "true" : "false") +
                       ",\"total\":" + String(g_gifReindex.total) + ",\"done\":" + String(g_gifReindex.done) + ",\"files\":" + String((long)g_gifReindex.files) + ",\"expected\":" + String((long)g_gifReindex.expected) + ",\"eta\":\"" + jsonEsc(gifReindexEta()) + "\"" + ",\"cancelling\":" + String(g_gifReindex.cancel ? "true" : "false") +
                       ",\"current\":\"" + jsonEsc(g_gifReindex.current) + "\",\"last_result\":\"" + jsonEsc(g_gifReindex.lastResult) + "\"" +
                       ",\"elapsed_ms\":" + String(g_gifReindex.running ? (millis() - g_gifReindex.startedMs) : (g_gifReindex.finishedMs ? g_gifReindex.finishedMs - g_gifReindex.startedMs : 0)) + "}";
            request->send(200, "application/json", j);
        });

        server.on("/api/gifs/file", HTTP_DELETE, [](AsyncWebServerRequest *request){
            String rawFolder = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String rawName   = request->hasParam("name")   ? request->getParam("name")->value()   : "";
            String folder = sanitizeName(rawFolder, false);
            String name   = sanitizeName(rawName, true);
            if (rawFolder.indexOf('/') >= 0 || rawName.indexOf('/') >= 0 || rawFolder.indexOf("..") >= 0 || rawName.indexOf("..") >= 0 || folder.isEmpty() || name.isEmpty() || !hasGifExt(name)) {
                request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder and name (.gif/.png/.raw) are required\"}");
                return;
            }
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String path = root + "/" + folder + "/" + name;
            bool removed = false;
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) { request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                if (sd.exists(path.c_str())) removed = sd.remove(path.c_str());
                if (removed) { int c = updateFolderIndex(root, folder, {}, { name }); updatePlaylistsEntry(root, folder, c); }
            }
            if (!removed) { request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}"); return; }
            request->send(200, "application/json", "{\"status\":\"ok\",\"orientation\":\"" + orientation + "\",\"deleted\":\"" + jsonEsc(path) + "\"}");
        });

        server.on("/api/gifs/files", HTTP_GET, [](AsyncWebServerRequest *request){
            // Streamed from the folder's index.txt in chunks: a 3,600-entry folder is ~150 KB of JSON, far more than
            // the S3 can hold in one String. Falls back to a (small-folder) directory walk with sizes when no index exists.
            String raw = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String folder = sanitizeName(raw, false);
            if (raw.indexOf('/') >= 0 || raw.indexOf("..") >= 0 || folder.isEmpty()) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder must be a plain playlist name\"}"); return; }
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String path = root + "/" + folder;
            struct FilesCtx { FsFile idx; String head; String carry; bool first = true; bool done = false; bool closed = false; };
            FilesCtx* ctx = new FilesCtx();
            bool found = false; bool haveIndex = false;
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) { delete ctx; request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                if (sd.exists(path.c_str())) {
                    found = true;
                    String idxPath = path + "/index.txt";
                    ctx->idx = sd.open(idxPath.c_str(), FILE_OPEN_READ);
                    haveIndex = (bool)ctx->idx;
                }
            }
            if (!found) { delete ctx; request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Folder not found\"}"); return; }
            if (!haveIndex) {
                // no index yet (folder created offline): walk the directory; such folders are small in practice
                String json = "["; bool first = true;
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) { delete ctx; request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                {
                    FsFile dir = sd.open(path.c_str(), FILE_OPEN_READ);
                    if (dir && isDirectory(dir)) {
                        FsFile f;
                        while (getNextFile(dir, f)) {
                            if (isDirectory(f)) continue;
                            String n = getFileName(f); int ls = n.lastIndexOf('/'); if (ls >= 0) n = n.substring(ls + 1);
                            if (isMacJunk(n) || n == "index.txt" || !hasGifExt(n)) continue;
                            if (!first) json += ","; first = false;
                            json += "{\"name\":\"" + jsonEsc(n) + "\",\"bytes\":" + String((unsigned long)f.size()) + "}";
                        }
                        dir.close();
                    }
                }
                guard.unlock();
                delete ctx;
                request->send(200, "application/json", "{\"folder\":\"" + jsonEsc(folder) + "\",\"orientation\":\"" + orientation + "\",\"path\":\"" + jsonEsc(path) + "\",\"source\":\"scan\",\"files\":" + json + "]}");
                return;
            }
            ctx->head = "{\"folder\":\"" + jsonEsc(folder) + "\",\"orientation\":\"" + orientation + "\",\"path\":\"" + jsonEsc(path) + "\",\"source\":\"index\",\"files\":[";
            AsyncWebServerResponse* resp = request->beginChunkedResponse("application/json", [ctx](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                size_t out = 0;
                auto emit = [&](const String& piece) -> bool {   // append if it fits, else keep it in carry
                    if (out + piece.length() > maxLen) { ctx->carry = piece; return false; }
                    memcpy(buf + out, piece.c_str(), piece.length()); out += piece.length(); return true;
                };
                if (ctx->head.length()) { String h = ctx->head; ctx->head = ""; if (!emit(h)) return out; }
                if (ctx->carry.length()) { String c = ctx->carry; ctx->carry = ""; if (!emit(c)) return out; }
                if (ctx->done) return 0;   // 0 = end of body (only reached after the closing bracket went out)
                {
                    // One bounded hold per chunk (a Core 0 producer, Golden Rule #5): holding the card for the
                    // whole streamed response would stall GifEngine for as long as the download takes.
                    SdLockGuard guard(pdMS_TO_TICKS(5000));
                    if (guard && !ctx->closed) {
                        while (ctx->idx.available()) {
                            String n = ctx->idx.readStringUntil('\n'); n.trim();
                            if (n.isEmpty() || !hasGifExt(n)) continue;
                            String piece = String(ctx->first ? "" : ",") + "{\"name\":\"" + jsonEsc(n) + "\"}";
                            ctx->first = false;
                            if (!emit(piece)) return out;   // the guard releases the card on this early return
                            if (out > maxLen - 96) break;   // leave room; next call continues
                        }
                        if (!ctx->idx.available()) { ctx->idx.close(); ctx->closed = true; ctx->done = true; }
                    }
                }
                if (ctx->done && ctx->carry.isEmpty()) { emit("]}"); }   // may land in carry if the buffer is full
                if (out == 0 && !ctx->done && ctx->carry.isEmpty()) { ctx->done = true; emit("]}"); }   // safety: never return 0 mid-body
                return out;
            });
            resp->addHeader("Cache-Control", "no-cache");
            request->onDisconnect([ctx]() {
                if (ctx->idx && !ctx->closed) {
                    SdLockGuard guard(portMAX_DELAY);
                    if (ctx->idx && !ctx->closed) {
                        ctx->idx.close();
                        ctx->closed = true;
                    }
                }
                delete ctx;
            });
            request->send(resp);
        });

        // recursive folder delete (playlist folders may hold subfolders, e.g. Logo/256)
        static std::function<int(const String&)> removeTree = [](const String& path) -> int {
            int n = 0; std::vector<String> subdirs; std::vector<String> files;
            FsFile dir = sd.open(path.c_str(), FILE_OPEN_READ);
            if (!dir || !isDirectory(dir)) return 0;
            FsFile f;
            while (getNextFile(dir, f)) {
                String nm = getFileName(f); int ls = nm.lastIndexOf('/'); if (ls >= 0) nm = nm.substring(ls + 1);
                if (isDirectory(f)) subdirs.push_back(path + "/" + nm); else files.push_back(path + "/" + nm);
            }
            dir.close();
            for (auto& fp : files) { if (sd.remove(fp.c_str())) n++; }
            for (auto& sp : subdirs) n += removeTree(sp);
            sd.rmdir(path.c_str());
            return n;
        };

        server.on("/api/gifs/folder", HTTP_DELETE, [](AsyncWebServerRequest *request){
            String raw = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String folder = sanitizeName(raw, false);
            if (raw.indexOf('/') >= 0 || raw.indexOf("..") >= 0 || folder.isEmpty()) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder is required\"}"); return; }
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String path = root + "/" + folder; int n = -1;
            {
                SdLockGuard guard(pdMS_TO_TICKS(30000));
                if (!guard) { request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                if (sd.exists(path.c_str())) {
                    extern GifEngine* gifEngine; if (gifEngine) gifEngine->stop();
                    n = removeTree(path); updatePlaylistsEntry(root, folder, -1);
                }
            }
            if (n < 0) { request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"Folder not found\"}"); return; }
            request->send(200, "application/json", "{\"status\":\"ok\",\"orientation\":\"" + orientation + "\",\"deleted\":\"" + jsonEsc(path) + "\",\"files\":" + String(n) + "}");
        });

        static auto badName = [](const String& raw) -> bool { return raw.indexOf('/') >= 0 || raw.indexOf('\\') >= 0 || raw.indexOf("..") >= 0; };

        server.on("/api/gifs/mkdir", HTTP_POST, [](AsyncWebServerRequest *request){
            String raw = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String folder = sanitizeName(raw, false);
            if (badName(raw) || folder.isEmpty()) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder must be a plain name\"}"); return; }
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String path = root + "/" + folder; int code = 503; String msg = "SD card busy (rescan in progress?) - try again in a moment";
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (guard) {
                    // SdFat's parent-creation flag does not create the library root on this card, so make
                    // it explicitly (as the upload handler does) or the first vertical folder fails.
                    if (!sd.exists(root.c_str())) sd.mkdir(root.c_str());
                    if (sd.exists(path.c_str())) { code = 409; msg = "Folder already exists"; }
                    else if (sd.mkdir(path.c_str())) { code = 200; }
                    else { msg = "mkdir failed"; }
                    if (code == 200) { String ip = path + "/index.txt"; FsFile ix = sd.open(ip.c_str(), FILE_OPEN_WRITE); if (ix) ix.close(); updatePlaylistsEntry(root, folder, 0); }
                }
            }
            if (code != 200) { request->send(code, "application/json", "{\"status\":\"error\",\"message\":\"" + msg + "\"}"); return; }
            request->send(200, "application/json", "{\"status\":\"ok\",\"folder\":\"" + jsonEsc(folder) + "\",\"orientation\":\"" + orientation + "\",\"path\":\"" + jsonEsc(path) + "\"}");
        });

        // rename a file (folder+name+to) or a folder (folder+to)
        server.on("/api/gifs/rename", HTTP_POST, [](AsyncWebServerRequest *request){
            String rawFolder = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String rawName   = request->hasParam("name")   ? request->getParam("name")->value()   : "";
            String rawTo     = request->hasParam("to")     ? request->getParam("to")->value()     : "";
            String folder = sanitizeName(rawFolder, false);
            if (badName(rawFolder) || badName(rawName) || badName(rawTo) || folder.isEmpty() || rawTo.isEmpty()) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder and to are required (plain names)\"}"); return; }
            const String orientation = gifOrientationOf(request);
            const String root = gifRootFor(orientation);
            String from, to, what, oldName, newName;
            if (rawName.isEmpty()) {
                String toFolder = sanitizeName(rawTo, false); if (toFolder.isEmpty()) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"bad target name\"}"); return; }
                from = root + "/" + folder; to = root + "/" + toFolder; what = to;
            } else {
                String name = sanitizeName(rawName, true); String toName = sanitizeName(rawTo, true);
                if (!hasGifExt(toName)) { int d = name.lastIndexOf('.'); if (d >= 0) toName += name.substring(d); }
                if (name.isEmpty() || toName.isEmpty() || !hasGifExt(toName)) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"bad file name\"}"); return; }
                from = root + "/" + folder + "/" + name; to = root + "/" + folder + "/" + toName; what = to; oldName = name; newName = toName;
            }
            int code = 503; String msg = "SD card busy (rescan in progress?) - try again in a moment";
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (guard) {
                    if (!sd.exists(from.c_str())) { code = 404; msg = "Source not found"; }
                    else if (sd.exists(to.c_str())) { code = 409; msg = "Target already exists"; }
                    else if (sd.rename(from.c_str(), to.c_str())) { code = 200; }
                    else { msg = "rename failed"; }
                    if (code == 200) {
                        if (rawName.isEmpty()) { String tf = sanitizeName(rawTo, false); updatePlaylistsEntry(root, folder, -1); updatePlaylistsEntry(root, tf, indexLineCount(root, tf)); }
                        else { int c = updateFolderIndex(root, folder, { newName }, { oldName }); updatePlaylistsEntry(root, folder, c); }
                    }
                }
            }
            if (code != 200) { request->send(code, "application/json", "{\"status\":\"error\",\"message\":\"" + msg + "\"}"); return; }
            request->send(200, "application/json", "{\"status\":\"ok\",\"orientation\":\"" + orientation + "\",\"renamed_to\":\"" + jsonEsc(what) + "\"}");
        });

        // serve one media file (inline preview, or attachment with ?download=1) — chunked from the SD under the mutex
        server.on("/api/gifs/file", HTTP_GET, [](AsyncWebServerRequest *request){
            String rawFolder = request->hasParam("folder") ? request->getParam("folder")->value() : "";
            String rawName   = request->hasParam("name")   ? request->getParam("name")->value()   : "";
            String folder = sanitizeName(rawFolder, false); String name = sanitizeName(rawName, true);
            if (badName(rawFolder) || badName(rawName) || folder.isEmpty() || name.isEmpty() || !hasGifExt(name)) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder and name are required\"}"); return; }
            const String root = gifRootOf(request);
            String path = root + "/" + folder + "/" + name;
            struct FileCtx { FsFile f; size_t size = 0; bool closed = false; };
            FileCtx* ctx = new FileCtx();
            bool ok = false;
            {
                SdLockGuard guard(pdMS_TO_TICKS(5000));
                if (!guard) { delete ctx; request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD card busy (rescan in progress?) - try again in a moment\"}"); return; }
                if (sd.exists(path.c_str())) { ctx->f = sd.open(path.c_str(), FILE_OPEN_READ); ok = (bool)ctx->f; if (ok) ctx->size = ctx->f.size(); }
            }
            if (!ok) { delete ctx; request->send(404, "application/json", "{\"status\":\"error\",\"message\":\"File not found\"}"); return; }
            String lower = name; lower.toLowerCase();
            const char* mime = lower.endsWith(".png") ? "image/png" : lower.endsWith(".gif") ? "image/gif" : "application/octet-stream";
            AsyncWebServerResponse* resp = request->beginResponse(mime, ctx->size, [ctx](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
                size_t n = 0;
                SdLockGuard guard(pdMS_TO_TICKS(5000));   // one bounded hold per chunk, see /api/gifs/files
                if (guard && ctx->f && !ctx->closed) {
                    n = ctx->f.read(buf, maxLen);
                    if (index + n >= ctx->size || n == 0) {
                        ctx->f.close();
                        ctx->closed = true;
                    }
                }
                return n;
            });
            resp->addHeader("Cache-Control", "no-cache");
            if (request->hasParam("download") && request->getParam("download")->value() == "1") resp->addHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
            request->onDisconnect([ctx]() {
                if (ctx->f && !ctx->closed) {
                    SdLockGuard guard(portMAX_DELAY);
                    if (ctx->f && !ctx->closed) {
                        ctx->f.close();
                        ctx->closed = true;
                    }
                }
                delete ctx;
            });
            request->send(resp);
        });

        server.on("/api/gifs/upload", HTTP_POST,
            [leaveUploadMode](AsyncWebServerRequest *request) {
                GifUploadCtx* ctx = (GifUploadCtx*)request->_tempObject;
                if (!ctx) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"No file received\"}"); return; }
                if (ctx->badFolder) { request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"folder must be a plain playlist name (no path separators)\"}"); delete ctx; request->_tempObject = nullptr; return; }
                {
                    SdLockGuard guard(pdMS_TO_TICKS(15000));
                    if (guard && ctx->savedCount > 0) { int c = updateFolderIndex(ctx->root, ctx->folder, ctx->savedNames, {}); updatePlaylistsEntry(ctx->root, ctx->folder, c); }
                }
                String body = "{\"status\":\"" + String(ctx->savedCount > 0 ? "ok" : "error") + "\",\"folder\":\"" + jsonEsc(ctx->folder) +
                              "\",\"orientation\":\"" + ctx->orientation + "\",\"count\":" + String(ctx->savedCount) + ",\"saved\":[" + ctx->saved + "],\"skipped\":[" + ctx->skipped + "]}";
                request->send(ctx->savedCount > 0 ? 200 : 400, "application/json", body);
                if (ctx->modeOn) leaveUploadMode();
                delete ctx; request->_tempObject = nullptr;
            },
            [enterUploadMode, leaveUploadMode](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
                GifUploadCtx* ctx = (GifUploadCtx*)request->_tempObject;
                if (!ctx) {
                    ctx = new GifUploadCtx();
                    String rawFolder = request->hasParam("folder") ? request->getParam("folder")->value() : "";
                    ctx->badFolder = rawFolder.indexOf('/') >= 0 || rawFolder.indexOf('\\') >= 0 || rawFolder.indexOf("..") >= 0;
                    ctx->folder = ctx->badFolder ? "" : sanitizeName(rawFolder, false);
                    if (ctx->folder.isEmpty() && !ctx->badFolder) ctx->folder = "Uploads";
                    ctx->orientation = gifOrientationOf(request);
                    ctx->root = gifRootFor(ctx->orientation);
                    request->_tempObject = ctx;
                    // free the context if the client disconnects mid-upload (the final handler nulls _tempObject after deleting it)
                    request->onDisconnect([request, leaveUploadMode]() {
                        GifUploadCtx* c = (GifUploadCtx*)request->_tempObject;
                        if (c) {
                            if (c->file) {
                                SdLockGuard guard(pdMS_TO_TICKS(1000));
                                if (guard) c->file.close();
                            }
                            if (c->modeOn) leaveUploadMode();
                            delete c;
                            request->_tempObject = nullptr;
                        }
                    });
                    extern GifEngine* gifEngine;
                    if (gifEngine) { gifEngine->stop(); ctx->engineStopped = true; }   // no concurrent SD reads during the write
                    enterUploadMode(); ctx->modeOn = true;
                    {
                        SdLockGuard guard(pdMS_TO_TICKS(5000));
                        if (guard) {
                            if (!sd.exists(ctx->root.c_str())) sd.mkdir(ctx->root.c_str());
                            String fp = ctx->root + "/" + ctx->folder; if (!sd.exists(fp.c_str())) sd.mkdir(fp.c_str());
                        }
                    }
                }
                if (ctx->badFolder) return;   // rejected folder name: swallow the body, final handler answers 400
                if (index == 0) {
                    // new part begins: close any previous file
                    if (ctx->file) {
                        SdLockGuard guard(pdMS_TO_TICKS(5000));
                        if (guard) ctx->file.close();
                    }
                    ctx->currentName = sanitizeName(filename, true); ctx->currentBytes = 0; ctx->currentOk = false;
                    if (ctx->currentName.isEmpty() || !hasGifExt(ctx->currentName)) {
                        if (ctx->skipped.length()) ctx->skipped += ",";
                        ctx->skipped += "{\"name\":\"" + jsonEsc(filename) + "\",\"reason\":\"unsupported type\"}";
                    } else {
                        bool busy = false;
                        {
                            SdLockGuard guard(pdMS_TO_TICKS(5000));
                            if (guard) {
                                String path = ctx->root + "/" + ctx->folder + "/" + ctx->currentName;
                                ctx->file = sd.open(path.c_str(), FILE_OPEN_WRITE);
                                ctx->currentOk = (bool)ctx->file;
                            } else busy = true;   // previously a silent drop; report it like any other skipped file
                        }
                        if (!ctx->currentOk) { if (ctx->skipped.length()) ctx->skipped += ","; ctx->skipped += "{\"name\":\"" + jsonEsc(ctx->currentName) + "\",\"reason\":\"" + (busy ? "SD busy" : "open failed") + "\"}"; }
                    }
                }
                if (ctx->currentOk && len > 0) {
                    SdLockGuard guard(pdMS_TO_TICKS(5000));
                    if (guard) {
                        size_t w = ctx->file.write(data, len); ctx->currentBytes += w;
                        if (w != len) { ctx->currentOk = false; }
                    } else ctx->currentOk = false;   // a chunk we could not write means the file is incomplete
                }
                if (final) {
                    if (ctx->file) { SdLockGuard guard(pdMS_TO_TICKS(5000)); if (guard) ctx->file.close(); }
                    if (ctx->currentOk) {
                        if (ctx->saved.length()) ctx->saved += ",";
                        ctx->saved += "{\"name\":\"" + jsonEsc(ctx->currentName) + "\",\"bytes\":" + String(ctx->currentBytes) + "}"; ctx->savedNames.push_back(ctx->currentName);
                        ctx->savedCount++;
                        LOGI("GIFS", "Uploaded %s/%s/%s (%u bytes)", ctx->root.c_str(), ctx->folder.c_str(), ctx->currentName.c_str(), (unsigned)ctx->currentBytes);
                    } else if (!ctx->currentName.isEmpty() && hasGifExt(ctx->currentName)) {
                        if (ctx->skipped.length()) ctx->skipped += ",";
                        ctx->skipped += "{\"name\":\"" + jsonEsc(ctx->currentName) + "\",\"reason\":\"write failed\"}";
                    }
                    ctx->currentOk = false;
                }
            });
    }

    // API: Wi-Fi (re)configuration with an immediate connection attempt (parity with the RPi's
    // /api/wifi). Unlike the generic /api/settings handler, this persists the new credentials to
    // SD *and* tries to associate right away, reporting success/failure synchronously instead of
    // requiring a full reboot to find out if the new SSID/password actually work.
    AsyncCallbackJsonWebHandler* wifiHandler = new AsyncCallbackJsonWebHandler("/api/wifi", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }
        JsonObject body = json.as<JsonObject>();
        if (body["ssid"].isNull() || body["password"].isNull()) {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"Missing ssid or password\"}");
            return;
        }

        extern ConfigLoader config;
        String newSsid = body["ssid"].as<String>();
        String newPass = body["password"].as<String>();

        config.wifi.ssid = newSsid;
        config.wifi.password = newPass;
        bool saved = config.saveToSD("/config.json");

        WiFi.disconnect(true);
        delay(100);
        WiFi.begin(newSsid.c_str(), newPass.c_str());

        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            String msg = "Connected! IP: " + WiFi.localIP().toString();
            if (!saved) msg += " (Warning: credentials could NOT be saved to SD - will be lost on reboot)";
            request->send(200, "application/json", "{\"success\":true,\"message\":\"" + msg + "\"}");
        } else if (!saved) {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect AND could not save credentials to SD - nothing was persisted.\"}");
        } else {
            request->send(500, "application/json", "{\"success\":false,\"message\":\"Failed to connect to the new network. Credentials were still saved to SD for the next reboot.\"}");
        }
    });
    server.addHandler(wifiHandler);

    // API: MQTT SSH helpers (Parity stubs for ESP32)
    server.on("/api/mqtt/install", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":false,\"message\":\"SSH install is only available on Raspberry Pi. On ESP32, configure Recalbox/Batocera/RetroPie manually with tools/rpi_emulationstation_base_os_setup.sh to send MQTT to this device's IP.\"}");
    });
    server.on("/api/mqtt/logs", HTTP_POST, [](AsyncWebServerRequest *request){
        request->send(200, "application/json", "{\"success\":true,\"logs\":\"SSH logs are only available on Raspberry Pi.\"}");
    });

    // API: Marquee endpoint — Supports multipart file upload (GIF/PNG/JPG saved to /marquees/custom_marquee.<ext>)
    // API: Marquee & Upload endpoints — Supports multipart file upload (GIF/PNG/JPG saved to /marquees/marquee.<ext>)
    // with single-file overwrite and zero rotation preemption, as well as direct raw RGB565 streaming (application/octet-stream).
    auto uploadHandler = [this](AsyncWebServerRequest *request) {
        if (request->_tempObject) {
            String* pPath = (String*)request->_tempObject;
            String json = "{\"success\":true,\"path\":\"" + *pPath + "\",\"message\":\"Asset uploaded successfully\"}";
            delete pPath;
            request->_tempObject = nullptr;
            request->send(200, "application/json", json);
        } else {
            request->send(400, "application/json", "{\"success\":false,\"message\":\"No image data received\"}");
        }
    };

    auto uploadFileHandler = [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
        String ext = ".gif";
        int dotIdx = filename.lastIndexOf('.');
        if (dotIdx >= 0) {
            ext = filename.substring(dotIdx);
            ext.toLowerCase();
        }
        String destPath = "/marquees/marquee" + ext;

        if (!index) {
            LOGI("WebServer", "Asset upload start: %s -> %s", filename.c_str(), destPath.c_str());
            SdLockGuard guard(pdMS_TO_TICKS(5000));
            if (guard) {
                if (!sd.exists("/marquees")) {
                    sd.mkdir("/marquees");
                }
                // Enforce single-file overwrite: remove any previous marquee files
                const char* oldFiles[] = {
                    "/marquees/marquee.gif", "/marquees/marquee.png", "/marquees/marquee.jpg", "/marquees/marquee.jpeg",
                    "/marquees/custom_marquee.gif", "/marquees/custom_marquee.png", "/marquees/custom_marquee.jpg", "/marquees/custom_marquee.raw"
                };
                for (const char* f : oldFiles) {
                    if (sd.exists(f)) {
                        sd.remove(f);
                    }
                }
                FsFile uploadFile = sd.open(destPath.c_str(), FILE_OPEN_WRITE);
                if (uploadFile) {
                    uploadFile.write(data, len);
                    uploadFile.close();
                    request->_tempObject = new String(destPath);
                } else {
                    LOGE("WebServer", "Failed to create marquee file: %s", destPath.c_str());
                }
            } else {
                LOGE("WebServer", "Failed to acquire SD lock for marquee file upload: %s", destPath.c_str());
            }
        } else if (request->_tempObject) {
            SdLockGuard guard(pdMS_TO_TICKS(5000));
            if (guard) {
                FsFile uploadFile = sd.open(destPath.c_str(), FILE_OPEN_APPEND);
                if (uploadFile) {
                    uploadFile.write(data, len);
                    uploadFile.close();
                }
            }
        }

        if (final && request->_tempObject) {
            LOGI("WebServer", "Asset upload complete: %s (%u bytes)", destPath.c_str(), index + len);
            if (marquee) {
                marquee->setMarqueeFile(destPath.c_str());
                // Do NOT call marquee->activate() so rotation continues seamlessly
            }
        }
    };

    server.on("/api/marquee", HTTP_POST,
        uploadHandler,
        uploadFileHandler,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            if (request->contentType().startsWith("multipart/")) {
                return;
            }
            if (!marquee) {
                if (index == 0) request->send(503, "application/json", "{\"success\":false,\"message\":\"Marquee engine not initialized\"}");
                return;
            }
            if (index == 0 && total > 0) {
                request->_tempObject = malloc(total);
            }
            if (request->_tempObject) {
                memcpy((uint8_t*)request->_tempObject + index, data, len);
            }
            if (index + len == total) {
                if (request->_tempObject && total == marquee->expectedBufferBytes()) {
                    marquee->show((uint8_t*)request->_tempObject, total);
                    request->send(200, "application/json", "{\"success\":true,\"message\":\"Marquee image received and displayed\"}");
                } else {
                    char msgBuf[128];
                    snprintf(msgBuf, sizeof(msgBuf), "{\"success\":false,\"message\":\"Expected exactly %u bytes of raw RGB565, got %u\"}", (unsigned)marquee->expectedBufferBytes(), (unsigned)total);
                    request->send(400, "application/json", msgBuf);
                }
            }
        }
    );

    server.on("/api/upload", HTTP_POST,
        uploadHandler,
        uploadFileHandler
    );

    // API: GET /api/audio/status — Returns current audio playback snapshot
    server.on("/api/audio/status", HTTP_GET, [](AsyncWebServerRequest *request){
        auto st = audioHub.getPlaybackStateSnapshot();
        SpiRamJsonDocument doc(512);
        doc["source"] = AudioHub::getSourceName(st.source);
        doc["status"] = (int)st.status;
        doc["title"] = st.title;
        doc["artist"] = st.artist;
        doc["album"] = st.album;
        doc["duration_ms"] = st.durationMs;
        doc["position_ms"] = st.positionMs;
        doc["volume"] = st.volume;
        doc["artwork_id"] = st.artworkId;
        doc["generation"] = st.generation;
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/audio/volume — Adjusts master volume (0-100%)
    AsyncCallbackJsonWebHandler* audioVolHandler = new AsyncCallbackJsonWebHandler("/api/audio/volume", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        if (!obj["volume"].isNull()) {
            uint8_t vol = obj["volume"].as<uint8_t>();
            audioHub.setVolume(vol);
        }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(audioVolHandler);

    // API: POST /api/audio/radio — Controls WebRadio playback
    AsyncCallbackJsonWebHandler* radioHandler = new AsyncCallbackJsonWebHandler("/api/audio/radio", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        String url = obj["url"] | "";
        String name = obj["name"] | "Web Radio";
        if (url.isEmpty()) {
            webRadioService.stop();
            request->send(200, "application/json", "{\"success\":true,\"message\":\"Radio stopped\"}");
        } else {
            bool ok = webRadioService.play(url, name);
            request->send(ok ? 200 : 500, "application/json", ok ? "{\"success\":true}" : "{\"error\":\"Failed to connect to radio stream\"}");
        }
    });
    server.addHandler(radioHandler);

    // API: POST /api/audio/stop — Stops active audio stream
    server.on("/api/audio/stop", HTTP_POST, [](AsyncWebServerRequest *request){
        webRadioService.stop();
        bluetoothAudioService.stop();
        request->send(200, "application/json", "{\"success\":true}");
    });

    // API: POST /api/audio/test — Plays a short diagnostic test tone (880 Hz) on the onboard speaker
    server.on("/api/audio/test", HTTP_POST, [](AsyncWebServerRequest *request){
        audioOutputHAL.playSine(880.0f, 500);
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Test tone played\"}");
    });

    // API: GET /api/gyro/status — Returns gravity vector and suggested orientation
    server.on("/api/gyro/status", HTTP_GET, [](AsyncWebServerRequest *request){
        SpiRamJsonDocument doc(512);
        GyroOrientation orient = gyroHAL.getOrientation();
        doc["available"] = gyroHAL.isAvailable();
        doc["sensor"] = orient.sensorName;
        doc["ax"] = orient.ax;
        doc["ay"] = orient.ay;
        doc["az"] = orient.az;
        doc["gx"] = orient.gx;
        doc["gy"] = orient.gy;
        doc["gz"] = orient.gz;
        doc["suggested_rotation"] = orient.suggestedRotation;
        doc["rotation_offset"] = displayOrientationManager.getRotationOffset();
        doc["current_rotation"] = displayOrientationManager.getRotation();
        doc["transition_effect"] = RotationTransitionFX::effectToString(displayOrientationManager.getTransitionEffect());
        doc["transition_duration_ms"] = displayOrientationManager.getTransitionDuration();
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/gyro/calibrate — Calibrates current physical position as 0° reference
    server.on("/api/gyro/calibrate", HTTP_POST, [](AsyncWebServerRequest *request){
        displayOrientationManager.calibrateZeroReference();
        extern ConfigLoader config;
        config.matrix.rotation_offset = displayOrientationManager.getRotationOffset();
        ConfigSanitizer::sanitize(config);
        bool saved = config.saveToSD("/config.json");
        if (!saved) { sendConfigSaveFailed(request); return; }
        SpiRamJsonDocument doc(256);
        doc["success"] = true;
        doc["rotation_offset"] = displayOrientationManager.getRotationOffset();
        doc["current_rotation"] = displayOrientationManager.getRotation();
        String res;
        serializeJson(doc, res);
        request->send(200, "application/json", res);
    });

    // API: POST /api/display/test-transition — Triggers a preview of the rotation transition FX
    AsyncCallbackJsonWebHandler* testFxHandler = new AsyncCallbackJsonWebHandler("/api/display/test-transition", [](AsyncWebServerRequest *request, JsonVariant &json) {
        RotationEffect eff = displayOrientationManager.getTransitionEffect();
        if (json.is<JsonObject>()) {
            JsonObject obj = json.as<JsonObject>();
            if (!obj["effect"].isNull()) {
                eff = RotationTransitionFX::parseEffect(obj["effect"].as<String>());
            }
        }
        displayOrientationManager.triggerTestTransition(eff);
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(testFxHandler);

    // API: POST /api/display/orientation — Sets manual rotation index, rotation offset, or transition effect
    AsyncCallbackJsonWebHandler* orientHandler = new AsyncCallbackJsonWebHandler("/api/display/orientation", [](AsyncWebServerRequest *request, JsonVariant &json) {
        if (!json.is<JsonObject>()) {
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }
        JsonObject obj = json.as<JsonObject>();
        extern ConfigLoader config;
        bool changed = false;
        if (!obj["manual_rotation"].isNull()) {
            displayOrientationManager.setRotation(obj["manual_rotation"].as<uint8_t>());
        }
        if (!obj["auto_rotate"].isNull()) {
            config.matrix.auto_rotate = obj["auto_rotate"].as<bool>();
            changed = true;
        }
        if (!obj["rotation_offset"].isNull()) {
            config.matrix.rotation_offset = obj["rotation_offset"].as<int>();
            displayOrientationManager.setRotationOffset((uint8_t)config.matrix.rotation_offset);
            changed = true;
        }
        if (!obj["transition_effect"].isNull()) {
            config.matrix.rotation_transition = obj["transition_effect"].as<String>();
            displayOrientationManager.setTransitionEffect(config.matrix.rotation_transition);
            changed = true;
        }
        if (!obj["transition_duration_ms"].isNull()) {
            config.matrix.rotation_transition_duration_ms = obj["transition_duration_ms"].as<int>();
            displayOrientationManager.setTransitionDuration((uint32_t)config.matrix.rotation_transition_duration_ms);
            changed = true;
        }
        bool saved = true;
        if (changed) {
            ConfigSanitizer::sanitize(config);
            saved = config.saveToSD("/config.json");
        }
        if (!saved) { sendConfigSaveFailed(request); return; }
        request->send(200, "application/json", "{\"success\":true}");
    });
    server.addHandler(orientHandler);

    // Register DLNA MediaRenderer description, SCPD and SOAP endpoints
    dlnaService.registerRoutes(&server);

    // Helper to serve marquee and custom assets from SD card
    auto serveMarqueeFile = [](AsyncWebServerRequest *request, const String& path) {
        struct MarqueeFileCtx {
            FsFile f;
            size_t size = 0;
            bool closed = false;
        };
        MarqueeFileCtx* ctx = new MarqueeFileCtx();
        bool ok = false;
        {
            SdLockGuard guard(pdMS_TO_TICKS(3000));
            if (!guard) {
                delete ctx;
                request->send(503, "application/json", "{\"status\":\"busy\",\"message\":\"SD busy\"}");
                return;
            }
            if (sd.exists(path.c_str())) {
                ctx->f = sd.open(path.c_str(), FILE_OPEN_READ);
                if (ctx->f) {
                    ctx->size = ctx->f.size();
                    ok = true;
                }
            }
        }
        if (!ok) {
            delete ctx;
            request->send(404, "text/plain", "File not found");
            return;
        }
        String lower = path; lower.toLowerCase();
        const char* mime = lower.endsWith(".png") ? "image/png" :
                           lower.endsWith(".gif") ? "image/gif" :
                           (lower.endsWith(".jpg") || lower.endsWith(".jpeg")) ? "image/jpeg" :
                           "application/octet-stream";
        AsyncWebServerResponse* resp = request->beginResponse(mime, ctx->size, [ctx](uint8_t* buf, size_t maxLen, size_t index) -> size_t {
            size_t n = 0;
            SdLockGuard guard(pdMS_TO_TICKS(5000));
            if (guard && ctx->f && !ctx->closed) {
                n = ctx->f.read(buf, maxLen);
                if (index + n >= ctx->size || n == 0) {
                    ctx->f.close();
                    ctx->closed = true;
                }
            }
            return n;
        });
        resp->addHeader("Cache-Control", "no-cache");
        request->onDisconnect([ctx]() {
            if (ctx->f && !ctx->closed) {
                SdLockGuard guard(portMAX_DELAY);
                if (ctx->f && !ctx->closed) {
                    ctx->f.close();
                    ctx->closed = true;
                }
            }
            delete ctx;
        });
        request->send(resp);
    };

    // Handle Preflight CORS & asset routes (e.g. /marquees/...)
    server.onNotFound([serveMarqueeFile](AsyncWebServerRequest *request) {
        if (request->method() == HTTP_OPTIONS) {
            request->send(200);
            return;
        }
        String url = request->url();
        if (url.startsWith("/marquees/")) {
            serveMarqueeFile(request, url);
            return;
        }
        request->send(404, "text/plain", "Not found");
    });
}
