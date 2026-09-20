#include "../core/SDUtils.h"
#include "FighterEngine.h"
#include <ArduinoJson.h>
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"
#include "../core/SdLockGuard.h"
#include "../core/NetworkBudget.h"


FighterEngine::FighterEngine() : matrix(nullptr) {}

EngineError FighterEngine::initialize(EngineContext* context, const EngineConfig* config) {
    matrix = context ? context->getMatrix() : nullptr;
    m_hasPsram = context ? context->hasPsram() : false;
    initialize();
    return EngineError::OK;
}

void FighterEngine::activate() {
    startLoaderTaskIfNeeded();
    startFight();
}

void FighterEngine::update(EngineContext* context) {
    loop();
}

void FighterEngine::render(EngineContext* context) {
    draw();
}

void FighterEngine::deactivate() {
    // Non-blocking state-only transition on Core 1: zero allocation, zero mutex
    active = false;
    m_taskShouldExit = true;
    if (loaderTaskHandle) {
        xTaskNotifyGive(loaderTaskHandle);
    }
}

void FighterEngine::onConfigChanged(const EngineConfig* engineConfig) {
    // Config now comes from global config.system
}

bool FighterEngine::shutdownForDestruction() {
    // 1. Core 0 cooperative worker task shutdown
    if (loaderTaskHandle) {
        m_taskShouldExit = true;
        xTaskNotifyGive(loaderTaskHandle);
        uint32_t start = millis();
        while (!m_loaderStopped.load(std::memory_order_acquire) && (millis() - start < 300)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!m_loaderStopped.load(std::memory_order_acquire)) {
            LOGE("FighterEngine", "CRITICAL: FgtLoader task failed to stop within 300ms!");
            return false; // Quarantined: do not delete object, no UAF
        }
    }

    // 2. Safe resource cleanup on Core 0 (closes SD file handles and frees frame buffers)
    stop();
    return true;
}

FighterEngine::~FighterEngine() {
    if (loaderTaskHandle && !m_loaderStopped.load(std::memory_order_acquire)) {
        m_taskShouldExit = true;
        xTaskNotifyGive(loaderTaskHandle);
        for (int i = 0; i < 30 && !m_loaderStopped.load(std::memory_order_acquire); i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // Strict cooperative shutdown: ZERO forced vTaskDelete() fallback
    }
    if (m_roster) {
        if (esp_ptr_external_ram(m_roster)) {
            heap_caps_free(m_roster);
        } else {
            free(m_roster);
        }
        m_roster = nullptr;
    }
    freeFighter(p1);
    freeFighter(p2);
    freeFighter(nextP1);
    freeFighter(nextP2);
}

void FighterEngine::initialize() {
    loadRoster();
}

String FighterEngine::getFightersDir() {
    int targetHeight = (matrix && matrix->height() <= 32) ? 32 : (m_hasPsram ? 64 : 32);
    if (!cachedFightersDir.isEmpty() && cachedScaleClass == targetHeight) {
        return cachedFightersDir;
    }
    cachedScaleClass = targetHeight;

    bool has64 = false;
    bool has32 = false;
    bool hasDef = false;
    SdLockGuard guard(pdMS_TO_TICKS(1000));
    if (guard) {
        if (targetHeight == 64 && sd.exists("/fighters_64/index.txt")) has64 = true;
        if (sd.exists("/fighters_32/index.txt")) has32 = true;
        if (sd.exists("/fighters/index.txt")) hasDef = true;
    }
    if (has64) {
        cachedFightersDir = "/fighters_64";
        return cachedFightersDir;
    }
    if (has32) {
        cachedFightersDir = "/fighters_32";
        return cachedFightersDir;
    }
    if (hasDef) {
        cachedFightersDir = "/fighters";
        return cachedFightersDir;
    }
    cachedFightersDir = "/fighters_32";
    return cachedFightersDir;
}

void FighterEngine::loadRoster() {
    if (m_roster) {
        if (esp_ptr_external_ram(m_roster)) heap_caps_free(m_roster);
        else free(m_roster);
        m_roster = nullptr;
    }
    numAvailableFighters = 0;
    String indexPath = getFightersDir() + "/index.txt";
    
    SdLockGuard guard(pdMS_TO_TICKS(3000));
    if (!guard) {
        LOGW("FighterEngine", "Could not acquire sdMutex to load roster.");
        return;
    }

    if (!sd.exists(indexPath.c_str())) {
        LOGW("FighterEngine", "No index.txt found at %s!", indexPath.c_str());
        return;
    }

    FsFile f = sd.open(indexPath.c_str(), FILE_OPEN_READ);
    if (!f) {
        LOGE("FighterEngine", "Failed to open %s", indexPath.c_str());
        return;
    }

    size_t fileSize = f.size();
    if (fileSize == 0 || fileSize > 256 * 1024) {
        LOGW("FighterEngine", "Invalid index.txt size: %u", (unsigned)fileSize);
        f.close();
        return;
    }

    // Allocate temporary read buffer in PSRAM if available, or internal DRAM
    char* rawBuf = nullptr;
    if (m_hasPsram) {
        rawBuf = (char*)heap_caps_malloc(fileSize + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!rawBuf) {
        rawBuf = (char*)malloc(fileSize + 1);
    }

    if (!rawBuf) {
        LOGE("FighterEngine", "Failed to allocate %u bytes for index.txt buffer.", (unsigned)(fileSize + 1));
        f.close();
        return;
    }

    size_t bytesRead = f.read((uint8_t*)rawBuf, fileSize);
    f.close();
    guard.unlock(); // SD access complete in ~15ms!

    if (bytesRead != fileSize) {
        LOGE("FighterEngine", "Short read on index.txt: %u of %u bytes.", (unsigned)bytesRead, (unsigned)fileSize);
        if (esp_ptr_external_ram(rawBuf)) heap_caps_free(rawBuf);
        else free(rawBuf);
        return;
    }
    rawBuf[fileSize] = '\0';

    std::vector<FighterMeta> parsed;
    parsed.reserve(1200);

    char* line = rawBuf;
    while (line < rawBuf + fileSize) {
        char* nextLine = strchr(line, '\n');
        if (nextLine) {
            *nextLine = '\0';
        }
        char* cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        while (*line == ' ' || *line == '\t') line++;

        if (*line != '\0' && *line != '.') {
            char* c1 = strchr(line, ',');
            if (c1) {
                *c1 = '\0';
                char* c2 = strchr(c1 + 1, ',');
                if (c2) *c2 = '\0';
                char* c3 = c2 ? strchr(c2 + 1, ',') : nullptr;
                if (c3) *c3 = '\0';
                char* c4 = c3 ? strchr(c3 + 1, ',') : nullptr;
                if (c4) *c4 = '\0';
                char* c5 = c4 ? strchr(c4 + 1, ',') : nullptr;
                if (c5) *c5 = '\0';

                FighterMeta entry;
                strncpy(entry.name, line, sizeof(entry.name) - 1);
                entry.name[sizeof(entry.name) - 1] = '\0';
                entry.height = atoi(c1 + 1);
                entry.ground_y = c2 ? atoi(c2 + 1) : 0;
                entry.origin_x = c3 ? atoi(c3 + 1) : 0;
                entry.width_px = c4 ? atoi(c4 + 1) : 32;
                entry.head_y = c5 ? atoi(c5 + 1) : 0;

                parsed.push_back(entry);
            }
        }

        if (!nextLine) break;
        line = nextLine + 1;
    }

    if (esp_ptr_external_ram(rawBuf)) heap_caps_free(rawBuf);
    else free(rawBuf);

    numAvailableFighters = (int)parsed.size();
    if (numAvailableFighters > 0) {
        size_t rosterBytes = numAvailableFighters * sizeof(FighterMeta);
        if (m_hasPsram) {
            m_roster = (FighterMeta*)heap_caps_malloc(rosterBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        }
        if (!m_roster) {
            m_roster = (FighterMeta*)malloc(rosterBytes);
        }
        if (m_roster) {
            memcpy(m_roster, parsed.data(), rosterBytes);
        } else {
            numAvailableFighters = 0;
        }
    }

    LOGI("FighterEngine", "Loaded %d fighters (Fast PSRAM Roster: %u bytes %s)",
         numAvailableFighters, (unsigned)(numAvailableFighters * sizeof(FighterMeta)),
         (m_roster && esp_ptr_external_ram(m_roster)) ? "PSRAM" : "internal DRAM");
}

bool FighterEngine::getRandomFighter(FighterPlayer& p) {
    if (numAvailableFighters == 0 || !m_roster) return false;
    
    int target = esp_random() % numAvailableFighters;
    const FighterMeta& meta = m_roster[target];
    p.name = meta.name;
    p.height = meta.height;
    p.ground_y = meta.ground_y;
    p.origin_x = meta.origin_x;
    p.width_px = meta.width_px;
    p.head_y = meta.head_y;
    return true;
}

bool FighterEngine::loadFighterAnim(FgtAnimation& anim, const char* filepath) {
    if (m_taskShouldExit) return false;
    if (ESP.getFreeHeap() < 32768) {
        LOGW("FighterEngine", "Skip anim %s: low heap %u", filepath, (unsigned)ESP.getFreeHeap());
        return false;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < 18432) {
        LOGW("FighterEngine", "Skip anim %s: low DMA heap %u", filepath, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
        return false;
    }
    if (m_hasPsram && ESP.getFreePsram() < 1048576) {
        LOGW("FighterEngine", "Skip anim %s: low PSRAM %u", filepath, (unsigned)ESP.getFreePsram());
        return false;
    }

    // Do not stress SD bus / memory while another task on Core 0 is performing a heavy TLS handshake
    if (NetworkBudget::getTlsHandshakeMutex()) {
        if (xSemaphoreTake(NetworkBudget::getTlsHandshakeMutex(), pdMS_TO_TICKS(2000)) == pdTRUE) {
            xSemaphoreGive(NetworkBudget::getTlsHandshakeMutex());
        } else {
            LOGD("FighterEngine", "Waiting for TLS handshake before loading %s (timeout)", filepath);
            return false;
        }
    }

    SdLockGuard sdGuard(pdMS_TO_TICKS(3000));
    if (!sdGuard) {
        LOGW("FighterEngine", "Could not acquire sdMutex for %s (timeout 3s)", filepath);
        return false;
    }

    if (!sd.exists(filepath)) {
        LOGD("FighterEngine", "Anim not found on SD: %s", filepath);
        return false;
    }
    
    FsFile f = sd.open(filepath, FILE_OPEN_READ);
    if (!f) {
        LOGE("FighterEngine", "Could not open file: %s", filepath);
        return false;
    }
    
    if (f.available() < 11) {
        LOGW("FighterEngine", "Anim truncated header (%d bytes): %s", (int)f.available(), filepath);
        f.close();
        return false;
    }

    char magic[3];
    if (f.read((uint8_t*)magic, 3) != 3 || magic[0] != 'F' || magic[1] != 'G' || magic[2] != 'T') {
        LOGW("FighterEngine", "Anim invalid header (magic: %.3s): %s", magic, filepath);
        f.close();
        return false;
    }
    
    uint8_t version = f.read();
    if (version != 1) {
        LOGW("FighterEngine", "Anim unsupported version %u: %s", (unsigned)version, filepath);
        f.close();
        return false;
    }
    
    f.read((uint8_t*)&anim.width, 2);
    f.read((uint8_t*)&anim.height, 2);
    uint16_t fileNumFrames = 0;
    f.read((uint8_t*)&fileNumFrames, 2);
    f.read((uint8_t*)&anim.transparentColor, 2);
    
    if (fileNumFrames == 0 || anim.width == 0 || anim.height == 0 || anim.width > 256 || anim.height > 256) {
        LOGW("FighterEngine", "Anim invalid geometry (%ux%u, %u frames): %s", anim.width, anim.height, fileNumFrames, filepath);
        f.close();
        return false;
    }
    
    uint16_t maxFrames = 40;
    anim.numFrames = (fileNumFrames > maxFrames) ? maxFrames : fileNumFrames;
    
    anim.frameDelays = (uint16_t*)malloc(anim.numFrames * 2);
    if (!anim.frameDelays) {
        LOGW("FighterEngine", "Anim failed to allocate %u frame delays: %s", anim.numFrames, filepath);
        f.close();
        return false;
    }
    if (f.read((uint8_t*)anim.frameDelays, anim.numFrames * 2) != (int)(anim.numFrames * 2)) {
        LOGW("FighterEngine", "Anim failed reading %u frame delays: %s", anim.numFrames, filepath);
        free(anim.frameDelays);
        anim.frameDelays = nullptr;
        f.close();
        return false;
    }
    if (fileNumFrames > anim.numFrames) {
        f.seek(f.position() + (fileNumFrames - anim.numFrames) * 2);
    }
    
    anim.filepath = String(filepath);
    anim.pixelsOffset = f.position();
    anim.cachedFrameIndex = -1;
    
    int frameSize = anim.width * anim.height * 2;
    anim.totalPixelsSize = frameSize * anim.numFrames;
    int maxFrameSize = m_hasPsram ? (2 * 1024 * 1024) : 32768;
    if (frameSize > maxFrameSize) {
        LOGE("FighterEngine", "Frame too big! %d bytes for %s", frameSize, filepath);
        free(anim.frameDelays);
        anim.frameDelays = nullptr;
        f.close();
        return false;
    }

    size_t fileSize = f.size();
    if (fileSize < anim.pixelsOffset + anim.totalPixelsSize) {
        LOGW("FighterEngine", "Corrupt/truncated animation %s: size %u < expected %u", filepath, (uint32_t)fileSize, (uint32_t)(anim.pixelsOffset + anim.totalPixelsSize));
        free(anim.frameDelays);
        anim.frameDelays = nullptr;
        f.close();
        return false;
    }
    
    if (m_hasPsram) {
        size_t freePsram = ESP.getFreePsram();
        size_t safetyHeadroom = 1048576; // 1 MB safety reserve
        if (freePsram <= safetyHeadroom || anim.totalPixelsSize > (freePsram - safetyHeadroom)) {
            LOGW("FighterEngine", "Animation too large (%d bytes, free PSRAM: %u) for %s", anim.totalPixelsSize, (uint32_t)freePsram, filepath);
            free(anim.frameDelays);
            anim.frameDelays = nullptr;
            f.close();
            return false;
        }
        anim.psramBuffer = (uint8_t*)heap_caps_malloc(anim.totalPixelsSize, MALLOC_CAP_SPIRAM);
        if (anim.psramBuffer) {
            size_t toRead = anim.totalPixelsSize;
            size_t offset = 0;
            // Use a 2048-byte DRAM bounce buffer for fast sequential SD reads
            // without bus thrashing or in-loop sleep while holding sdGuard.
            uint8_t bounceBuf[2048];
            while (toRead > 0) {
                if (m_taskShouldExit || ESP.getFreeHeap() < 24576) {
                    LOGW("FighterEngine", "Cut short chunk read for %s: low heap %u", filepath, (unsigned)ESP.getFreeHeap());
                    toRead = 1; // force abort
                    break;
                }
                size_t chunk = (toRead > sizeof(bounceBuf)) ? sizeof(bounceBuf) : toRead;
                size_t r = f.read(bounceBuf, chunk);
                if (r == 0) break;
                memcpy(anim.psramBuffer + offset, bounceBuf, r);
                offset += r;
                toRead -= r;
            }
            if (toRead > 0) {
                LOGW("FighterEngine", "Incomplete read for %s (%u remaining)", filepath, (uint32_t)toRead);
                heap_caps_free(anim.psramBuffer);
                anim.psramBuffer = nullptr;
                free(anim.frameDelays);
                anim.frameDelays = nullptr;
                f.close();
                return false;
            }
        } else {
            LOGW("FighterEngine", "PSRAM alloc failed for %d bytes (%s). Skipping fighter.", anim.totalPixelsSize, filepath);
            free(anim.frameDelays);
            anim.frameDelays = nullptr;
            f.close();
            return false;
        }
    }
    
    f.close();
    sdGuard.unlock();
    anim.loaded = true;
    return true;
}

void FighterEngine::freeAnim(FgtAnimation& anim) {
    if (anim.frameDelays) {
        free(anim.frameDelays);
        anim.frameDelays = nullptr;
    }
    if (anim.psramBuffer) {
        heap_caps_free(anim.psramBuffer);
        anim.psramBuffer = nullptr;
    }
    anim.loaded = false;
    anim.cachedFrameIndex = -1;
    anim.totalPixelsSize = 0;
}

void FighterEngine::freeFighter(FighterPlayer& p) {
    if (p.currentFrameBuffer) {
        if (m_hasPsram) heap_caps_free(p.currentFrameBuffer);
        else free(p.currentFrameBuffer);
        p.currentFrameBuffer = nullptr;
        p.currentBufferSize = 0;
    }
    freeAnim(p.animStand);
    freeAnim(p.animWalk);
    freeAnim(p.animAttack);
    freeAnim(p.animHit);
    freeAnim(p.animWin);
    freeAnim(p.animSpecial);
    freeAnim(p.animSuper);
    freeAnim(p.animFall);
}

void FighterEngine::startLoaderTaskIfNeeded() {
    if (loaderTaskHandle) return;

    m_taskShouldExit = false;
    m_loaderStopped.store(false, std::memory_order_release);
    if (xTaskCreatePinnedToCore(loaderTaskFunc, "FgtLoader", 10240, this, 1, &loaderTaskHandle, 0) != pdPASS) {
        LOGE("FighterEngine", "Failed to spawn preload worker task.");
        loaderTaskHandle = nullptr;
    }
}

void FighterEngine::triggerBackgroundPreload() {
    if (millis() < retryDelayEnd) return;
    if (isNextReady.load(std::memory_order_acquire) || isPreloading || numAvailableFighters < 2) return;
    if (!loaderTaskHandle) return; // Worker failed to start; nothing to notify.

    static constexpr uint32_t PRELOAD_MIN_FREE_HEAP = 30 * 1024;
    static uint32_t lastSkipLogMs = 0;
    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < PRELOAD_MIN_FREE_HEAP) {
        const uint32_t now = millis();
        if (now - lastSkipLogMs > 30000) {
            lastSkipLogMs = now;
            LOGW("FighterEngine", "Skipping background preload: free heap %u < %u bytes required.",
                 (unsigned)freeHeap, (unsigned)PRELOAD_MIN_FREE_HEAP);
        }
        return;
    }

    isPreloading = true;
    xTaskNotifyGive(loaderTaskHandle);
}

void FighterEngine::loaderTaskFunc(void* param) {
    FighterEngine* self = (FighterEngine*)param;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (self->m_taskShouldExit) break;
        self->runBackgroundPreload();
    }
    self->loaderTaskHandle = nullptr;
    self->m_loaderStopped.store(true, std::memory_order_release);
    vTaskDelete(NULL);
}

void FighterEngine::runBackgroundPreload() {
    if (m_taskShouldExit || millis() < retryDelayEnd) {
        isPreloading = false;
        return;
    }

    freeFighter(nextP1);
    freeFighter(nextP2);

    static constexpr uint32_t PRELOAD_MIN_FREE_HEAP = 30 * 1024;
    static constexpr uint32_t PRELOAD_MIN_FREE_DMA = 16 * 1024;
    static constexpr uint32_t PRELOAD_MIN_FREE_PSRAM = 1024 * 1024; // 1 MB safety reserve

    if (ESP.getFreeHeap() < PRELOAD_MIN_FREE_HEAP ||
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < PRELOAD_MIN_FREE_DMA ||
        (m_hasPsram && ESP.getFreePsram() < PRELOAD_MIN_FREE_PSRAM) ||
        numAvailableFighters < 2 || !m_roster) {
        LOGW("FighterEngine", "Aborting background preload: insufficient memory or empty roster (heap: %u, dma: %u, psram: %u)",
             (unsigned)ESP.getFreeHeap(),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)(m_hasPsram ? ESP.getFreePsram() : 0));
        isPreloading = false;
        retryDelayEnd = millis() + 10000;
        return;
    }

    auto renderedHeight = [](int ground_y, int head_y, int declaredHeight) -> int {
        int h = ground_y - head_y;
        if (h > 0) return h;
        return declaredHeight > 0 ? declaredHeight : 32;
    };

    String dir = getFightersDir();

    auto loadAnimThreadSafe = [&](FgtAnimation& anim, const String& path) -> bool {
        if (m_taskShouldExit) return false;
        if (ESP.getFreeHeap() < PRELOAD_MIN_FREE_HEAP ||
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) < PRELOAD_MIN_FREE_DMA ||
            (m_hasPsram && ESP.getFreePsram() < PRELOAD_MIN_FREE_PSRAM)) {
            LOGW("FighterEngine", "Preload cut short: memory below floor (heap: %u, dma: %u, psram: %u)",
                 (unsigned)ESP.getFreeHeap(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
                 (unsigned)(m_hasPsram ? ESP.getFreePsram() : 0));
            return false;
        }
        bool res = loadFighterAnim(anim, path.c_str());
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield SD bus and CPU to Core 1 rendering
        return res;
    };

    bool matchFoundAndLoaded = false;

    // Try up to 4 distinct fighter pairings in case one fighter has missing files on SD
    for (int attempt = 0; attempt < 4 && !m_taskShouldExit; attempt++) {
        freeFighter(nextP1);
        freeFighter(nextP2);

        int p1Idx = esp_random() % numAvailableFighters;
        const FighterMeta& p1Meta = m_roster[p1Idx];

        int h1 = renderedHeight(p1Meta.ground_y, p1Meta.head_y, p1Meta.height);
        float bestRatio = 0.0f;
        int bestIdx = -1;

        // Check 40 candidate opponents entirely in PSRAM (0 SD access, <5 microseconds!)
        for (int i = 0; i < 40; i++) {
            int candIdx = esp_random() % numAvailableFighters;
            if (candIdx == p1Idx) continue;
            const FighterMeta& candMeta = m_roster[candIdx];
            if (strcmp(candMeta.name, p1Meta.name) == 0) continue;

            int h2 = renderedHeight(candMeta.ground_y, candMeta.head_y, candMeta.height);
            if (h1 > 0 && h2 > 0) {
                float ratio = (h2 <= h1) ? ((float)h2 / (float)h1)
                                         : ((float)h1 / (float)h2);
                if (ratio > bestRatio) {
                    bestRatio = ratio;
                    bestIdx = candIdx;
                }
                if (ratio >= 0.80f) {
                    break;
                }
            }
        }

        if (bestIdx < 0) {
            LOGW("FighterEngine", "Preload attempt [%d/4]: no suitable opponent found for %s", attempt + 1, p1Meta.name);
            continue;
        }

        const FighterMeta& p2Meta = m_roster[bestIdx];

        nextP1.name = p1Meta.name;
        nextP1.height = p1Meta.height;
        nextP1.ground_y = p1Meta.ground_y;
        nextP1.head_y = p1Meta.head_y;
        nextP1.origin_x = p1Meta.origin_x;
        nextP1.width_px = p1Meta.width_px;

        nextP2.name = p2Meta.name;
        nextP2.height = p2Meta.height;
        nextP2.ground_y = p2Meta.ground_y;
        nextP2.head_y = p2Meta.head_y;
        nextP2.origin_x = p2Meta.origin_x;
        nextP2.width_px = p2Meta.width_px;

        int h2 = renderedHeight(p2Meta.ground_y, p2Meta.head_y, p2Meta.height);
        LOGI("FighterEngine", "Preload candidate [%d/4]: P1 [%s] (H:%d) vs P2 [%s] (H:%d, ratio: %.2f)",
             attempt + 1, nextP1.name.c_str(), h1, nextP2.name.c_str(), h2, bestRatio);

        // P1 Required Animations: walk, attack, hit, win
        if (!loadAnimThreadSafe(nextP1.animWalk, dir + "/" + nextP1.name + "/walk.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed walk for %s (attempt %d/4)", nextP1.name.c_str(), attempt + 1);
            continue;
        }
        loadAnimThreadSafe(nextP1.animStand, dir + "/" + nextP1.name + "/stand.fgt");

        if (!loadAnimThreadSafe(nextP1.animAttack, dir + "/" + nextP1.name + "/attack.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed attack for %s (attempt %d/4)", nextP1.name.c_str(), attempt + 1);
            continue;
        }
        if (!loadAnimThreadSafe(nextP1.animHit, dir + "/" + nextP1.name + "/hit.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed hit for %s (attempt %d/4)", nextP1.name.c_str(), attempt + 1);
            continue;
        }
        if (!loadAnimThreadSafe(nextP1.animWin, dir + "/" + nextP1.name + "/win.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed win for %s (attempt %d/4)", nextP1.name.c_str(), attempt + 1);
            continue;
        }

        int t1[3] = {1, 2, 3};
        for(int i=0; i<3; i++) { int r = esp_random() % 3; int temp=t1[i]; t1[i]=t1[r]; t1[r]=temp; }
        for(int i=0; i<3; i++) {
            if (loadAnimThreadSafe(nextP1.animSpecial, dir + "/" + nextP1.name + "/special" + String(t1[i]) + ".fgt")) break;
        }
        for(int i=0; i<3; i++) {
            if (loadAnimThreadSafe(nextP1.animSuper, dir + "/" + nextP1.name + "/super" + String(t1[i]) + ".fgt")) break;
        }
        loadAnimThreadSafe(nextP1.animFall, dir + "/" + nextP1.name + "/fall.fgt");

        // P2 Required Animations: walk, attack, hit, win
        if (!loadAnimThreadSafe(nextP2.animWalk, dir + "/" + nextP2.name + "/walk.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed walk for %s (attempt %d/4)", nextP2.name.c_str(), attempt + 1);
            continue;
        }
        loadAnimThreadSafe(nextP2.animStand, dir + "/" + nextP2.name + "/stand.fgt");

        if (!loadAnimThreadSafe(nextP2.animAttack, dir + "/" + nextP2.name + "/attack.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed attack for %s (attempt %d/4)", nextP2.name.c_str(), attempt + 1);
            continue;
        }
        if (!loadAnimThreadSafe(nextP2.animHit, dir + "/" + nextP2.name + "/hit.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed hit for %s (attempt %d/4)", nextP2.name.c_str(), attempt + 1);
            continue;
        }
        if (!loadAnimThreadSafe(nextP2.animWin, dir + "/" + nextP2.name + "/win.fgt")) {
            LOGW("FighterEngine", "Preload cut short: failed win for %s (attempt %d/4)", nextP2.name.c_str(), attempt + 1);
            continue;
        }

        int t2[3] = {1, 2, 3};
        for(int i=0; i<3; i++) { int r = esp_random() % 3; int temp=t2[i]; t2[i]=t2[r]; t2[r]=temp; }
        for(int i=0; i<3; i++) {
            if (loadAnimThreadSafe(nextP2.animSpecial, dir + "/" + nextP2.name + "/special" + String(t2[i]) + ".fgt")) break;
        }
        for(int i=0; i<3; i++) {
            if (loadAnimThreadSafe(nextP2.animSuper, dir + "/" + nextP2.name + "/super" + String(t2[i]) + ".fgt")) break;
        }
        loadAnimThreadSafe(nextP2.animFall, dir + "/" + nextP2.name + "/fall.fgt");

        computeStandBounds(nextP1);
        computeStandBounds(nextP2);
        isNextReady.store(true, std::memory_order_release);
        LOGI("FighterEngine", "Background preload completed on Core 0: %s (front:%d, back:%d) vs %s (front:%d, back:%d)",
             nextP1.name.c_str(), nextP1.frontExtent, nextP1.backExtent,
             nextP2.name.c_str(), nextP2.frontExtent, nextP2.backExtent);
        matchFoundAndLoaded = true;
        break;
    }

    if (!matchFoundAndLoaded) {
        LOGW("FighterEngine", "Preload exhausted all 4 pairing attempts (missing animations on SD). Retrying in 10s...");
        freeFighter(nextP1);
        freeFighter(nextP2);
        retryDelayEnd = millis() + 10000;
    }

    isPreloading = false;
}

void FighterEngine::computeStandBounds(FighterPlayer& p) {
    int h = p.height > 0 ? p.height : 32;
    p.frontExtent = max(6, (h * 35) / 100);
    p.backExtent = max(6, (h * 25) / 100);

    const FgtAnimation& anim = p.animStand.loaded ? p.animStand : p.animWalk;
    if (!anim.loaded || anim.width == 0 || anim.height == 0) return;

    if (anim.psramBuffer) {
        int minX = anim.width;
        int maxX = -1;
        const uint8_t* ptr = anim.psramBuffer;
        for (int y = 0; y < anim.height; y++) {
            for (int x = 0; x < anim.width; x++) {
                uint16_t color = ptr[0] | (ptr[1] << 8);
                ptr += 2;
                if (color != anim.transparentColor) {
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                }
            }
        }
        if (maxX >= minX) {
            p.frontExtent = max(4, maxX - p.origin_x);
            p.backExtent = max(4, p.origin_x - minX);
        }
    } else {
        String path = anim.filepath;
        if (path.length() > 0) {
            SdLockGuard guard(pdMS_TO_TICKS(1500));
            if (guard && sd.exists(path.c_str())) {
                FsFile f = sd.open(path.c_str(), FILE_OPEN_READ);
                if (f) {
                f.seek(anim.pixelsOffset);
                int minX = anim.width;
                int maxX = -1;
                uint8_t lineBuf[256];
                for (int y = 0; y < anim.height; y++) {
                    int toRead = min((int)sizeof(lineBuf), (int)(anim.width * 2));
                    int n = f.read(lineBuf, toRead);
                    for (int x = 0; x < n / 2; x++) {
                        uint16_t color = lineBuf[x * 2] | (lineBuf[x * 2 + 1] << 8);
                        if (color != anim.transparentColor) {
                            if (x < minX) minX = x;
                            if (x > maxX) maxX = x;
                        }
                    }
                    if ((int)(anim.width * 2) > toRead) {
                        f.seek(f.position() + (anim.width * 2 - toRead));
                    }
                }
                f.close();
                if (maxX >= minX) {
                    p.frontExtent = max(4, maxX - p.origin_x);
                    p.backExtent = max(4, p.origin_x - minX);
                }
            }
        }
    }
}
}

static void movePlayer(FighterPlayer& dest, FighterPlayer& src) {
    dest.name = src.name;
    dest.height = src.height;
    dest.ground_y = src.ground_y;
    dest.head_y = src.head_y;
    dest.origin_x = src.origin_x;
    dest.width_px = src.width_px;
    dest.frontExtent = src.frontExtent;
    dest.backExtent = src.backExtent;
    dest.animStand = src.animStand;
    dest.animWalk = src.animWalk;
    dest.animAttack = src.animAttack;
    dest.animHit = src.animHit;
    dest.animWin = src.animWin;
    dest.animSpecial = src.animSpecial;
    dest.animSuper = src.animSuper;
    dest.animFall = src.animFall;
    dest.state = src.state;
    dest.currentFrameBuffer = src.currentFrameBuffer;
    dest.currentBufferSize = src.currentBufferSize;
    dest.x = src.x;
    dest.y = src.y;
    dest.direction = src.direction;
    dest.currentFrame = src.currentFrame;
    dest.lastFrameTime = src.lastFrameTime;
    dest.hasHit = src.hasHit;
    dest.isDead = src.isDead;

    src.animStand = FgtAnimation();
    src.animWalk = FgtAnimation();
    src.animAttack = FgtAnimation();
    src.animHit = FgtAnimation();
    src.animWin = FgtAnimation();
    src.animSpecial = FgtAnimation();
    src.animSuper = FgtAnimation();
    src.animFall = FgtAnimation();
    src.currentFrameBuffer = nullptr;
    src.currentBufferSize = 0;
}

struct FighterGeometry {
    Rect arena;
    Rect hud;
    int16_t groundY;
    int16_t p1SpawnX;
    int16_t p2SpawnX;
    int scale;
    bool isTate;
};

class FighterGeometryAdapter {
public:
    static FighterGeometry calculate(const DisplayGeometry& geometry, bool is32pxDir,
                                     int p1HeadY, int p1GroundY, int p1Width,
                                     int p2HeadY, int p2GroundY, int p2Width) {
        FighterGeometry fg;
        int screenW = geometry.width;
        int screenH = geometry.height;
        fg.isTate = (geometry.layoutClass == LayoutClass::PORTRAIT || geometry.layoutClass == LayoutClass::TALL);

        fg.scale = 1;
        if (!fg.isTate && screenH >= 64 && is32pxDir) {
            fg.scale = screenH / 32;
        } else if (fg.isTate && screenW >= 96 && is32pxDir) {
            fg.scale = screenW / 64;
        }
        if (fg.scale < 1) fg.scale = 1;

        fg.arena = Rect{ 0, 0, (uint16_t)screenW, (uint16_t)screenH };
        fg.hud = Rect{ 0, 0, (uint16_t)screenW, (uint16_t)(fg.isTate ? 14 : 6) };

        if (fg.isTate) {
            fg.groundY = screenH - 1;
            fg.p1SpawnX = -(p1Width * fg.scale);
            fg.p2SpawnX = screenW;
        } else {
            // Both fighters share a single ground line. That line is derived from the
            // TALLEST of the two sprites so that its head always lands just below the
            // top edge of the panel; the shorter fighter then simply stands on the same
            // line, lower down. Anchoring on P1 only (the historical behaviour) pushed
            // P2's head off the top whenever P2 was the taller sprite.
            //
            // Sprite height above the ground line is (ground_y - head_y); this is the
            // exact metric used when drawing, so it is the only safe one to use here.
            int h1 = p1GroundY - p1HeadY;
            int h2 = p2GroundY - p2HeadY;
            int tallest = (h1 > h2) ? h1 : h2;
            if (tallest < 0) tallest = 0;

            // Exactly one pixel of clearance above the tallest head, at any scale.
            // groundY is expressed in SCREEN pixels here, just like the TATE branch.
            //
            // The resulting ground line is deliberately NOT clamped to the panel
            // height: when the sprites are taller than the display, the feet must fall
            // below the bottom edge so the overflow is cropped at the legs. Clamping it
            // to the bottom of the panel would crop the heads instead, which is never
            // acceptable.
            fg.groundY = (int16_t)(1 + tallest * fg.scale);

            fg.p1SpawnX = -(p1Width * fg.scale);
            fg.p2SpawnX = screenW;
        }
        return fg;
    }
};

void FighterEngine::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    if (!active) return;
    int targetHeight = (geometry.height <= 32) ? 32 : (m_hasPsram ? 64 : 32);
    if (cachedScaleClass != targetHeight) {
        cachedFightersDir = "";
    }
    bool is32 = getFightersDir().endsWith("32");
    FighterGeometry fg = FighterGeometryAdapter::calculate(geometry, is32,
                                                           p1.head_y, p1.ground_y, p1.width_px,
                                                           p2.head_y, p2.ground_y, p2.width_px);
    // Symmetric placement: both fighters are anchored by their feet on the shared
    // ground line, which the adapter already sized for the tallest of the two.
    // groundY is in screen pixels in both orientations.
    p1.y = fg.groundY - (p1.ground_y * fg.scale);
    p2.y = fg.groundY - (p2.ground_y * fg.scale);
}

void FighterEngine::startFight() {
    if (millis() < retryDelayEnd) return;
    if (numAvailableFighters < 2) return;
    
    if (isNextReady.load(std::memory_order_acquire)) {
        freeFighter(p1);
        freeFighter(p2);

        movePlayer(p1, nextP1);
        movePlayer(p2, nextP2);
        isNextReady.store(false, std::memory_order_release);

        loadDir = getFightersDir();
        int screenW = matrix ? matrix->width() : 128;
        int screenH = matrix ? matrix->height() : 32;

        DisplayGeometry geom;
        geom.width = screenW;
        geom.height = screenH;
        geom.layoutClass = DisplayGeometry::classify(screenW, screenH);

        bool is32 = loadDir.endsWith("32");
        FighterGeometry fg = FighterGeometryAdapter::calculate(geom, is32,
                                                               p1.head_y, p1.ground_y, p1.width_px,
                                                               p2.head_y, p2.ground_y, p2.width_px);

        // Feet of both fighters land on the same ground line, sized for the tallest
        // sprite so neither head can escape the top of the panel. groundY is in
        // screen pixels in both orientations.
        p1.direction = 1;
        p1.x = fg.p1SpawnX;
        p1.y = fg.groundY - (p1.ground_y * fg.scale);

        p2.direction = -1;
        p2.x = fg.p2SpawnX;
        p2.y = fg.groundY - (p2.ground_y * fg.scale);
        
        setPlayerState(p1, FIGHTER_WALK);
        setPlayerState(p2, FIGHTER_WALK);
        p1.hasHit = false; p2.hasHit = false; p1.isDead = false; p2.isDead = false;
        fightStartTime = millis(); fightEndTime = 0; faceoffStartTime = 0; lastMoveTime = millis();
        LOGI("FighterEngine", "🥊 Match -> [P1: %s] (H:%d) vs [P2: %s] (H:%d) [%dx%d %s]", 
             p1.name.c_str(), p1.height, p2.name.c_str(), p2.height, screenW, screenH, fg.isTate ? "TATE" : "LANDSCAPE");
        active = true;
    } else {
        // Not ready yet (e.g. at boot): trigger preload
        triggerBackgroundPreload();
    }
}

void FighterEngine::stop() {
    active = false;
    freeFighter(p1);
    freeFighter(p2);
}

void FighterEngine::setPlayerState(FighterPlayer& p, FighterState newState) {
    p.state = newState;
    p.currentFrame = 0;
    p.lastFrameTime = millis();
    
    FgtAnimation* anim = nullptr;
    if (newState == FIGHTER_STAND) anim = p.animStand.loaded ? &p.animStand : &p.animWalk;
    else if (newState == FIGHTER_WALK) anim = &p.animWalk;
    else if (newState == FIGHTER_ATTACK) anim = &p.animAttack;
    else if (newState == FIGHTER_HIT) anim = &p.animHit;
    else if (newState == FIGHTER_WIN) anim = &p.animWin;
    else if (newState == FIGHTER_SPECIAL) anim = &p.animSpecial;
    else if (newState == FIGHTER_SUPER) anim = &p.animSuper;
    else if (newState == FIGHTER_FALL) anim = &p.animFall;
    
    // Reset frame cache for all animations when changing state so new frames are freshly read
    p.animStand.cachedFrameIndex = -1;
    p.animWalk.cachedFrameIndex = -1;
    p.animAttack.cachedFrameIndex = -1;
    p.animHit.cachedFrameIndex = -1;
    p.animWin.cachedFrameIndex = -1;
    p.animSpecial.cachedFrameIndex = -1;
    p.animSuper.cachedFrameIndex = -1;
    p.animFall.cachedFrameIndex = -1;
}

bool FighterEngine::loop() {
    if (millis() < retryDelayEnd) return true;
    
    if (millis() < hitStopUntilMillis) return true;
    
    if (!active) {
        startFight();
        if (!active) return true;
    }
    
    uint32_t now = millis();
    extern ConfigLoader config;
    uint32_t speed = (config.system.idle_fighter_speed >= 25 && config.system.idle_fighter_speed <= 200) ? config.system.idle_fighter_speed : 100;
    
    // Update frames
    FgtAnimation* anim1 = nullptr;
    if (p1.state == FIGHTER_STAND) anim1 = p1.animStand.loaded ? &p1.animStand : &p1.animWalk;
    else if (p1.state == FIGHTER_WALK) anim1 = &p1.animWalk;
    else if (p1.state == FIGHTER_ATTACK) anim1 = &p1.animAttack;
    else if (p1.state == FIGHTER_HIT) anim1 = &p1.animHit;
    else if (p1.state == FIGHTER_WIN) anim1 = &p1.animWin;
    else if (p1.state == FIGHTER_SPECIAL) anim1 = &p1.animSpecial;
    else if (p1.state == FIGHTER_SUPER) anim1 = &p1.animSuper;
    else if (p1.state == FIGHTER_FALL) anim1 = &p1.animFall;
    
    if (anim1 && anim1->loaded && anim1->frameDelays && (p1.currentFrame < anim1->numFrames)) {
        uint32_t delay = (anim1->frameDelays[p1.currentFrame] * 100) / speed;
        if (p1.state != FIGHTER_STAND && p1.state != FIGHTER_WIN) {
            delay = (delay * 3) / 4; // faster combat animation
            uint32_t maxCombat = (70 * 100) / speed;
            if (delay > maxCombat) delay = maxCombat;
        }
        uint32_t minDelay = (25 * 100) / speed;
        if (delay < minDelay) delay = minDelay;
        if (now - p1.lastFrameTime >= delay) {
            p1.currentFrame++;
            p1.lastFrameTime = now;
            if (p1.currentFrame >= anim1->numFrames) {
                if (p1.state == FIGHTER_WALK || p1.state == FIGHTER_STAND) p1.currentFrame = 0; // Loop walk & stand
                else if (p1.state == FIGHTER_ATTACK || p1.state == FIGHTER_SPECIAL || p1.state == FIGHTER_SUPER) setPlayerState(p1, FIGHTER_WIN);
                else if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL) { p1.currentFrame = anim1->numFrames - 1; p1.isDead = true; } // Stay on last hit frame
                else if (p1.state == FIGHTER_WIN) { p1.currentFrame = anim1->numFrames - 1; } // Stay on last win frame
            }
        }
    }
    
    FgtAnimation* anim2 = nullptr;
    if (p2.state == FIGHTER_STAND) anim2 = p2.animStand.loaded ? &p2.animStand : &p2.animWalk;
    else if (p2.state == FIGHTER_WALK) anim2 = &p2.animWalk;
    else if (p2.state == FIGHTER_ATTACK) anim2 = &p2.animAttack;
    else if (p2.state == FIGHTER_HIT) anim2 = &p2.animHit;
    else if (p2.state == FIGHTER_WIN) anim2 = &p2.animWin;
    else if (p2.state == FIGHTER_SPECIAL) anim2 = &p2.animSpecial;
    else if (p2.state == FIGHTER_SUPER) anim2 = &p2.animSuper;
    else if (p2.state == FIGHTER_FALL) anim2 = &p2.animFall;
    
    if (anim2 && anim2->loaded && anim2->frameDelays && (p2.currentFrame < anim2->numFrames)) {
        uint32_t delay = (anim2->frameDelays[p2.currentFrame] * 100) / speed;
        if (p2.state != FIGHTER_STAND && p2.state != FIGHTER_WIN) {
            delay = (delay * 3) / 4; // faster combat animation
            uint32_t maxCombat = (70 * 100) / speed;
            if (delay > maxCombat) delay = maxCombat;
        }
        uint32_t minDelay = (25 * 100) / speed;
        if (delay < minDelay) delay = minDelay;
        if (now - p2.lastFrameTime >= delay) {
            p2.currentFrame++;
            p2.lastFrameTime = now;
            if (p2.currentFrame >= anim2->numFrames) {
                if (p2.state == FIGHTER_WALK || p2.state == FIGHTER_STAND) p2.currentFrame = 0; // Loop walk & stand
                else if (p2.state == FIGHTER_ATTACK || p2.state == FIGHTER_SPECIAL || p2.state == FIGHTER_SUPER) setPlayerState(p2, FIGHTER_WIN);
                else if (p2.state == FIGHTER_HIT || p2.state == FIGHTER_FALL) { p2.currentFrame = anim2->numFrames - 1; p2.isDead = true; } // Stay on last hit frame
                else if (p2.state == FIGHTER_WIN) { p2.currentFrame = anim2->numFrames - 1; } // Stay on last win frame
            }
        }
    }
    
    // Combat Logic (Approach & Engagement)
    if ((p1.state == FIGHTER_WALK || p1.state == FIGHTER_STAND) && 
        (p2.state == FIGHTER_WALK || p2.state == FIGHTER_STAND) && 
        !p1.isDead && !p2.isDead) {

        int screenW = matrix ? matrix->width() : 128;
        int screenH = matrix ? matrix->height() : 32;
        bool isTate = (screenW < 48 || screenH > (screenW * 3) / 2);
        int scale = 1;
        if (!isTate && screenH >= 64 && loadDir.endsWith("32")) {
            scale = screenH / 32;
        } else if (isTate && screenW >= 96 && loadDir.endsWith("32")) {
            scale = screenW / 64;
        }

        // Combat engagement spacing:
        // Gap is the space in pixels between the closest front pixels of the two combatants.
        // In Tate (vertical): compact gap (2px) to guarantee zero overlap on narrow screens.
        // In Landscape (horizontal): natural faceoff gap (4px).
        int gap = isTate ? 2 : ((screenW >= 128) ? 4 : 3);
        gap = max(1, gap * scale);

        // Distance between spines so their front body hulls meet with exactly `gap` between them:
        int engage_dist = (p1.frontExtent + p2.frontExtent) * scale + gap;
        int centerX = screenW / 2;

        int p1OriginOffset = p1.origin_x * scale;
        int p2OriginOffset = (max(1, p2.width_px) - 1 - p2.origin_x) * scale;
        int p1_target_x = centerX - (engage_dist / 2) - p1OriginOffset;
        int p2_target_x = centerX + (engage_dist / 2) - p2OriginOffset;

        // Move towards center target
        uint32_t stepInterval = (20 * 100) / speed;
        if (stepInterval < 5) stepInterval = 5;
        uint32_t elapsed = now - lastMoveTime;
        if (elapsed >= stepInterval) {
            int steps = (elapsed / stepInterval);
            lastMoveTime += steps * stepInterval;

            for (int s = 0; s < steps; s++) {
                if (p1.x < p1_target_x) {
                    p1.x += max(1, scale);
                    if (p1.x > p1_target_x) p1.x = p1_target_x;
                }
                if (p2.x > p2_target_x) {
                    p2.x -= max(1, scale);
                    if (p2.x < p2_target_x) p2.x = p2_target_x;
                }
            }
        }

        // Switch to standing idle stance when reached center target
        if (p1.x >= p1_target_x && p1.state == FIGHTER_WALK) {
            setPlayerState(p1, FIGHTER_STAND);
        }
        if (p2.x <= p2_target_x && p2.state == FIGHTER_WALK) {
            setPlayerState(p2, FIGHTER_STAND);
        }

        // Check if both combatants are ready at center target
        bool p1Ready = (p1.x >= p1_target_x);
        bool p2Ready = (p2.x <= p2_target_x);

        if (p1Ready && p2Ready) {
            if (faceoffStartTime == 0) {
                faceoffStartTime = now;
            }

            uint32_t faceoffDuration = (350 * 100) / speed;
            if (now - faceoffStartTime >= faceoffDuration) {
                // Fight! Random winner
                FighterPlayer* attacker = ((esp_random() % 2) == 0) ? &p1 : &p2;
                FighterPlayer* target = (attacker == &p1) ? &p2 : &p1;

                FighterState atkState = FIGHTER_ATTACK;
                FighterState tgtState = FIGHTER_HIT;
                bool isHeavy = false;

                int rnd = esp_random() % 100;
                if (attacker->animSuper.loaded && rnd < 50) {
                    atkState = FIGHTER_SUPER;
                    tgtState = target->animFall.loaded ? FIGHTER_FALL : FIGHTER_HIT;
                    isHeavy = true;
                } else if (attacker->animSpecial.loaded && rnd < 80) {
                    atkState = FIGHTER_SPECIAL;
                    tgtState = target->animFall.loaded ? FIGHTER_FALL : FIGHTER_HIT;
                    isHeavy = true;
                }

                setPlayerState(*attacker, atkState);
                setPlayerState(*target, tgtState);

                if (isHeavy) {
                    hitStopUntilMillis = millis() + 60;
                    shakeRemainingFrames = 6;
                }
            }
        }
    }
    
    // Dynamic Movement during special/super/fall
    int scale = (matrix->height() >= 64 && loadDir.endsWith("32")) ? (matrix->height() / 32) : 1;
    int moveAmt = 2 * scale;
    if (p1.state == FIGHTER_SPECIAL || p1.state == FIGHTER_SUPER) p1.x += p1.direction * moveAmt;
    if (p1.state == FIGHTER_FALL) p1.x -= p1.direction * moveAmt;
    if (p2.state == FIGHTER_SPECIAL || p2.state == FIGHTER_SUPER) p2.x += p2.direction * moveAmt;
    if (p2.state == FIGHTER_FALL) p2.x -= p2.direction * moveAmt;
    
    // End sequence
    if (fightEndTime == 0 && (p1.isDead || p2.isDead)) {
        fightEndTime = now;
    }
    
    if (fightEndTime > 0 && now - fightEndTime > 1200) {
        active = false;
        freeFighter(p1);
        freeFighter(p2);
        extern ConfigLoader config;
        ConfigSnapshotGuard guard = config.acquireSnapshot();
        retryDelayEnd = now + (guard->system.idle_fighter_interval * 1000);
        triggerBackgroundPreload();
    }
    return true;
}

void FighterEngine::drawPlayer(FighterPlayer& p, int offsetY) {
    FgtAnimation* anim = nullptr;
    if (p.state == FIGHTER_STAND) anim = p.animStand.loaded ? &p.animStand : &p.animWalk;
    else if (p.state == FIGHTER_WALK) anim = &p.animWalk;
    else if (p.state == FIGHTER_ATTACK) anim = &p.animAttack;
    else if (p.state == FIGHTER_HIT) anim = &p.animHit;
    else if (p.state == FIGHTER_WIN) anim = &p.animWin;
    else if (p.state == FIGHTER_SPECIAL) anim = &p.animSpecial;
    else if (p.state == FIGHTER_SUPER) anim = &p.animSuper;
    else if (p.state == FIGHTER_FALL) anim = &p.animFall;
    
    if (!anim || !anim->loaded || !anim->psramBuffer) return;
    
    if (p.currentFrame >= anim->numFrames) return;
    
    int frameSize = anim->width * anim->height * 2;
    uint8_t* ptr = nullptr;
    
    if ((size_t)(p.currentFrame + 1) * (size_t)frameSize <= anim->totalPixelsSize) {
        ptr = anim->psramBuffer + (p.currentFrame * frameSize);
    }
    
    if (!ptr) return;
    
    bool invert = (p.state == FIGHTER_SUPER && p.currentFrame < 2);
    
    int screenW = matrix ? matrix->width() : 128;
    int screenH = matrix ? matrix->height() : 32;
    bool isTate = (screenW < 48 || screenH > (screenW * 3) / 2);
    int scale = 1;
    if (!isTate && screenH >= 64 && loadDir.endsWith("32")) {
        scale = screenH / 32;
    } else if (isTate && screenW >= 96 && loadDir.endsWith("32")) {
        scale = screenW / 64;
    }
    
    for (int y = 0; y < anim->height; y++) {
        for (int x = 0; x < anim->width; x++) {
            uint16_t color = ptr[0] | (ptr[1] << 8);
            ptr += 2;
            
            if (color != anim->transparentColor) {
                if (invert) color = ~color;
                
                int drawX = p.x + (x * scale);
                // Flip horizontally if facing left
                if (p.direction == -1) {
                    drawX = p.x + ((anim->width - 1 - x) * scale);
                }
                
                int drawY = p.y + (y * scale) + offsetY;
                
                for (int dy = 0; dy < scale; dy++) {
                    for (int dx = 0; dx < scale; dx++) {
                        if (drawX + dx >= 0 && drawX + dx < matrix->width() && drawY + dy >= 0 && drawY + dy < matrix->height()) {
                            matrix->drawPixel(drawX + dx, drawY + dy, color);
                        }
                    }
                }
            }
        }
    }
}

void FighterEngine::draw() {
    if (!active || !matrix) return;

    int globalOffsetY = 0;
    if (shakeRemainingFrames > 0) {
        globalOffsetY = random(-2, 3);
        shakeRemainingFrames--;
    }

    int screenW = matrix->width();
    int screenH = matrix->height();
    bool isTateMode = (screenH > (screenW * 3) / 2 || screenW < 48);

    // Draw the dead player first (background), then the winner (foreground)
    if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL || p1.isDead) {
        drawPlayer(p1, globalOffsetY);
        drawPlayer(p2, globalOffsetY);
    } else {
        drawPlayer(p2, globalOffsetY);
        drawPlayer(p1, globalOffsetY);
    }

    // Responsive Arcade HUD in Tate mode (only 64x256+) / Large horizontal screens
    bool showHud = isTateMode ? (screenW >= 64) : (screenH >= 32);
    if (showHud) {
        bool showTags = (isTateMode && screenW >= 64 && screenH >= 200) || (!isTateMode && screenH >= 64);
        int barW = min(20, (screenW - 16) / 2);
        if (barW > 2) {
            // Player 1 Health Bar (Left)
            uint16_t p1Color = (p1.isDead) ? matrix->color565(180, 20, 20) : matrix->color565(30, 220, 60);
            matrix->drawRect(2, 2, barW, 4, matrix->color565(50, 50, 60));
            matrix->fillRect(3, 3, p1.isDead ? 1 : (barW - 2), 2, p1Color);

            // VS Badge in Center
            int vsX = (screenW / 2) - 1;
            matrix->drawPixel(vsX, 3, matrix->color565(255, 60, 60));
            matrix->drawPixel(vsX + 1, 3, matrix->color565(255, 60, 60));
            matrix->drawPixel(vsX, 4, matrix->color565(255, 220, 0));
            matrix->drawPixel(vsX + 1, 4, matrix->color565(255, 220, 0));

            // Player 2 Health Bar (Right)
            uint16_t p2Color = (p2.isDead) ? matrix->color565(180, 20, 20) : matrix->color565(30, 220, 60);
            int p2BarX = screenW - 2 - barW;
            matrix->drawRect(p2BarX, 2, barW, 4, matrix->color565(50, 50, 60));
            matrix->fillRect(p2BarX + 1, 3, p2.isDead ? 1 : (barW - 2), 2, p2Color);
        }

        // On ultra-tall screens (64x256), draw MUGEN arcade banner under health bars
        if (showTags) {
            matrix->setFont(nullptr);
            matrix->setTextSize(1);
            matrix->setTextColor(matrix->color565(255, 215, 0));
            String p1Tag = p1.name.substring(0, 3);
            p1Tag.toUpperCase();
            matrix->setCursor(2, 8);
            matrix->print(p1Tag);

            matrix->setTextColor(matrix->color565(0, 200, 255));
            String p2Tag = p2.name.substring(0, 3);
            p2Tag.toUpperCase();
            int p2TagX = max(2, screenW - (int)(p2Tag.length() * 6) - 2);
            matrix->setCursor(p2TagX, 8);
            matrix->print(p2Tag);
        }
    }
}
