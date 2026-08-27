#include "../core/SDUtils.h"
#include "FighterEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"

extern SemaphoreHandle_t sdMutex;

FighterEngine::FighterEngine() : matrix(nullptr) {}

EngineError FighterEngine::initialize(EngineContext* context, const EngineConfig* config) {
    matrix = context ? context->getMatrix() : nullptr;
    m_hasPsram = context ? context->hasPsram() : false;
    initialize();
    return EngineError::OK;
}

void FighterEngine::activate() {
    startFight();
}

void FighterEngine::update(EngineContext* context) {
    loop();
}

void FighterEngine::render(EngineContext* context) {
    draw();
}

void FighterEngine::deactivate() {
    stop();
}

void FighterEngine::onConfigChanged(const EngineConfig* engineConfig) {
    // Config now comes from global config.system
}


FighterEngine::~FighterEngine() {
    if (loaderTaskHandle) {
        vTaskDelete(loaderTaskHandle);
        loaderTaskHandle = nullptr;
    }
    if (fighterOffsets) free(fighterOffsets);
    freeFighter(p1);
    freeFighter(p2);
    freeFighter(nextP1);
    freeFighter(nextP2);
}

void FighterEngine::initialize() {
    loadRoster();
    triggerBackgroundPreload();
}

String FighterEngine::getFightersDir() {
    if (matrix && matrix->height() <= 32) return "/fighters_32";
    if (!m_hasPsram) {
        return "/fighters_32";
    }
    if (sd.exists("/fighters_64/index.txt")) {
        return "/fighters_64";
    }
    return "/fighters_32";
}

void FighterEngine::loadRoster() {
    numAvailableFighters = 0;
    String indexPath = getFightersDir() + "/index.txt";
    
    FsFile f;
    if (sd.exists(indexPath)) {
        f = sd.open(indexPath, FILE_OPEN_READ);
    }
    if (!f) {
        Serial.println("FighterEngine: No index.txt found!");
        return;
    }
    
    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !isMacJunk(line)) {
            numAvailableFighters++;
        }
    }
    
    if (numAvailableFighters > 0) {
        if (fighterOffsets) free(fighterOffsets);
        fighterOffsets = (uint32_t*)malloc(numAvailableFighters * sizeof(uint32_t));
        
        f.seek(0);
        int idx = 0;
        while (f.available() && idx < numAvailableFighters) {
            uint32_t pos = f.position();
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() > 0 && !isMacJunk(line)) {
                fighterOffsets[idx++] = pos;
            }
        }
    }
    
    f.close();
    LOGI("FighterEngine", "Loaded %d fighters (Fast Offset mode: %d bytes RAM)", numAvailableFighters, numAvailableFighters * 4);
}

bool FighterEngine::getRandomFighter(FighterPlayer& p) {
    if (numAvailableFighters == 0 || !fighterOffsets) return false;
    
    String indexPath = getFightersDir() + "/index.txt";
    FsFile f = sd.open(indexPath, FILE_OPEN_READ);
    if (!f) return false;
    
    int targetLine = esp_random() % numAvailableFighters;
    f.seek(fighterOffsets[targetLine]);
    String result = f.readStringUntil('\n');
    result.trim();
    f.close();
    
    // Format: name,height,ground_y,origin_x,width,head_y
    int comma1 = result.indexOf(',');
    int comma2 = result.indexOf(',', comma1 + 1);
    int comma3 = result.indexOf(',', comma2 + 1);
    int comma4 = result.indexOf(',', comma3 + 1);
    int comma5 = result.indexOf(',', comma4 + 1);

    if (comma1 > 0) {
        p.name = result.substring(0, comma1);
        p.height = result.substring(comma1 + 1, comma2 > 0 ? comma2 : result.length()).toInt();
        p.ground_y = comma2 > 0 ? result.substring(comma2 + 1, comma3 > 0 ? comma3 : result.length()).toInt() : 0;
        p.origin_x = comma3 > 0 ? result.substring(comma3 + 1, comma4 > 0 ? comma4 : result.length()).toInt() : 0;
        p.width_px = comma4 > 0 ? result.substring(comma4 + 1, comma5 > 0 ? comma5 : result.length()).toInt() : 32;
        p.head_y = comma5 > 0 ? result.substring(comma5 + 1).toInt() : 0;
        return true;
    }
    
    return false;
}

bool FighterEngine::loadFighterAnim(FgtAnimation& anim, const char* filepath) {
    if (!sd.exists(filepath)) return false;
    
    FsFile f = sd.open(filepath, FILE_OPEN_READ);
    if (!f) {
        LOGE("FighterEngine", "Could not open file: %s", filepath);
        return false;
    }
    
    char magic[3];
    if (f.read((uint8_t*)magic, 3) != 3 || magic[0] != 'F' || magic[1] != 'G' || magic[2] != 'T') {
        f.close();
        return false;
    }
    
    uint8_t version = f.read();
    if (version != 1) {
        f.close();
        return false;
    }
    
    f.read((uint8_t*)&anim.width, 2);
    f.read((uint8_t*)&anim.height, 2);
    uint16_t fileNumFrames = 0;
    f.read((uint8_t*)&fileNumFrames, 2);
    f.read((uint8_t*)&anim.transparentColor, 2);
    
    if (fileNumFrames == 0 || anim.width == 0 || anim.height == 0) {
        f.close();
        return false;
    }
    
    uint16_t maxFrames = 40;
    anim.numFrames = (fileNumFrames > maxFrames) ? maxFrames : fileNumFrames;
    
    anim.frameDelays = (uint16_t*)malloc(anim.numFrames * 2);
    if (!anim.frameDelays) {
        f.close();
        return false;
    }
    f.read((uint8_t*)anim.frameDelays, anim.numFrames * 2);
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
        f.close();
        return false;
    }
    
    if (m_hasPsram) {
        size_t freePsram = ESP.getFreePsram();
        size_t safetyHeadroom = 1048576; // 1 MB safety reserve
        if (freePsram <= safetyHeadroom || anim.totalPixelsSize > (freePsram - safetyHeadroom)) {
            LOGW("FighterEngine", "Animation too large (%d bytes, free PSRAM: %u) for %s", anim.totalPixelsSize, (uint32_t)freePsram, filepath);
            free(anim.frameDelays);
            f.close();
            return false;
        }
        anim.psramBuffer = (uint8_t*)heap_caps_malloc(anim.totalPixelsSize, MALLOC_CAP_SPIRAM);
        if (anim.psramBuffer) {
            size_t toRead = anim.totalPixelsSize;
            size_t offset = 0;
            while (toRead > 0) {
                size_t chunk = (toRead > 65536) ? 65536 : toRead;
                size_t r = f.read(anim.psramBuffer + offset, chunk);
                if (r == 0) break;
                offset += r;
                toRead -= r;
            }
        } else {
            LOGW("FighterEngine", "PSRAM alloc failed for %d bytes (%s). Skipping fighter.", anim.totalPixelsSize, filepath);
            free(anim.frameDelays);
            f.close();
            return false;
        }
    }
    
    f.close();
    anim.loaded = true;
    return true;
}

void FighterEngine::freeAnim(FgtAnimation& anim) {
    if (anim.loaded) {
        if (anim.frameDelays) { free(anim.frameDelays); anim.frameDelays = nullptr; }
        if (anim.psramBuffer) { heap_caps_free(anim.psramBuffer); anim.psramBuffer = nullptr; }
        anim.loaded = false;
    }
}

void FighterEngine::freeFighter(FighterPlayer& p) {
    if (p.activeFile) p.activeFile.close();
    if (p.currentFrameBuffer) {
        if (m_hasPsram) heap_caps_free(p.currentFrameBuffer);
        else free(p.currentFrameBuffer);
        p.currentFrameBuffer = nullptr;
        p.currentBufferSize = 0;
    }
    freeAnim(p.animWalk);
    freeAnim(p.animAttack);
    freeAnim(p.animHit);
    freeAnim(p.animWin);
    freeAnim(p.animSpecial);
    freeAnim(p.animSuper);
    freeAnim(p.animFall);
}

void FighterEngine::triggerBackgroundPreload() {
    if (isNextReady || isPreloading || numAvailableFighters < 2) return;
    isPreloading = true;
    xTaskCreatePinnedToCore(loaderTaskFunc, "FgtLoader", 4096, this, 1, &loaderTaskHandle, 0);
}

void FighterEngine::loaderTaskFunc(void* param) {
    FighterEngine* self = (FighterEngine*)param;
    self->runBackgroundPreload();
    self->loaderTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void FighterEngine::runBackgroundPreload() {
    freeFighter(nextP1);
    freeFighter(nextP2);

    bool gotP1 = false;
    if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100))) {
        gotP1 = getRandomFighter(nextP1);
        xSemaphoreGive(sdMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    if (!gotP1) {
        isPreloading = false;
        return;
    }

    int h1 = nextP1.height > 0 ? nextP1.height : ((nextP1.ground_y - nextP1.head_y) > 0 ? (nextP1.ground_y - nextP1.head_y) : 32);
    bool found = false;
    
    struct SimpleMeta {
        String name = "";
        int height = 0;
        int ground_y = 0;
        int head_y = 0;
        int origin_x = 0;
        int width_px = 0;
    } bestMeta, candMeta;
    float bestRatio = 0.0f;

    for (int i = 0; i < 40; i++) {
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100))) {
            FighterPlayer tempP;
            if (getRandomFighter(tempP)) {
                candMeta.name = tempP.name;
                candMeta.height = tempP.height;
                candMeta.ground_y = tempP.ground_y;
                candMeta.head_y = tempP.head_y;
                candMeta.origin_x = tempP.origin_x;
                candMeta.width_px = tempP.width_px;

                if (candMeta.name != nextP1.name) {
                    int h2 = candMeta.height > 0 ? candMeta.height : ((candMeta.ground_y - candMeta.head_y) > 0 ? (candMeta.ground_y - candMeta.head_y) : 32);
                    if (h1 > 0 && h2 > 0) {
                        float ratio = (float)h2 / (float)h1;
                        // P2 must be same height or up to 20% smaller (never taller than P1)
                        if (h2 <= h1) {
                            if (ratio > bestRatio) {
                                bestRatio = ratio;
                                bestMeta = candMeta;
                            }
                            if (ratio >= 0.80f) {
                                nextP2.name = candMeta.name;
                                nextP2.height = candMeta.height;
                                nextP2.ground_y = candMeta.ground_y;
                                nextP2.head_y = candMeta.head_y;
                                nextP2.origin_x = candMeta.origin_x;
                                nextP2.width_px = candMeta.width_px;
                                found = true;
                            }
                        }
                    }
                }
            }
            xSemaphoreGive(sdMutex);
            if (found) break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (!found && bestMeta.name.length() > 0) {
        nextP2.name = bestMeta.name;
        nextP2.height = bestMeta.height;
        nextP2.ground_y = bestMeta.ground_y;
        nextP2.head_y = bestMeta.head_y;
        nextP2.origin_x = bestMeta.origin_x;
        nextP2.width_px = bestMeta.width_px;
    }

    String dir = getFightersDir();
    bool ok = true;

    auto loadAnimThreadSafe = [&](FgtAnimation& anim, const String& path) -> bool {
        bool res = false;
        if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(200))) {
            res = loadFighterAnim(anim, path.c_str());
            xSemaphoreGive(sdMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10)); // Breathe! Yield SD bus and CPU to Core 1 rendering
        return res;
    };

    ok &= loadAnimThreadSafe(nextP1.animWalk, dir + "/" + nextP1.name + "/walk.fgt");
    ok &= loadAnimThreadSafe(nextP1.animAttack, dir + "/" + nextP1.name + "/attack.fgt");
    ok &= loadAnimThreadSafe(nextP1.animHit, dir + "/" + nextP1.name + "/hit.fgt");
    ok &= loadAnimThreadSafe(nextP1.animWin, dir + "/" + nextP1.name + "/win.fgt");

    int t1[3] = {1, 2, 3};
    for(int i=0; i<3; i++) { int r = esp_random() % 3; int temp=t1[i]; t1[i]=t1[r]; t1[r]=temp; }
    for(int i=0; i<3; i++) {
        if (loadAnimThreadSafe(nextP1.animSpecial, dir + "/" + nextP1.name + "/special" + String(t1[i]) + ".fgt")) break;
    }
    for(int i=0; i<3; i++) {
        if (loadAnimThreadSafe(nextP1.animSuper, dir + "/" + nextP1.name + "/super" + String(t1[i]) + ".fgt")) break;
    }
    loadAnimThreadSafe(nextP1.animFall, dir + "/" + nextP1.name + "/fall.fgt");

    ok &= loadAnimThreadSafe(nextP2.animWalk, dir + "/" + nextP2.name + "/walk.fgt");
    ok &= loadAnimThreadSafe(nextP2.animAttack, dir + "/" + nextP2.name + "/attack.fgt");
    ok &= loadAnimThreadSafe(nextP2.animHit, dir + "/" + nextP2.name + "/hit.fgt");
    ok &= loadAnimThreadSafe(nextP2.animWin, dir + "/" + nextP2.name + "/win.fgt");

    int t2[3] = {1, 2, 3};
    for(int i=0; i<3; i++) { int r = esp_random() % 3; int temp=t2[i]; t2[i]=t2[r]; t2[r]=temp; }
    for(int i=0; i<3; i++) {
        if (loadAnimThreadSafe(nextP2.animSpecial, dir + "/" + nextP2.name + "/special" + String(t2[i]) + ".fgt")) break;
    }
    for(int i=0; i<3; i++) {
        if (loadAnimThreadSafe(nextP2.animSuper, dir + "/" + nextP2.name + "/super" + String(t2[i]) + ".fgt")) break;
    }
    loadAnimThreadSafe(nextP2.animFall, dir + "/" + nextP2.name + "/fall.fgt");

    if (ok) {
        isNextReady = true;
        LOGI("FighterEngine", "Background preload completed on Core 0: %s vs %s", nextP1.name.c_str(), nextP2.name.c_str());
    } else {
        freeFighter(nextP1);
        freeFighter(nextP2);
        LOGW("FighterEngine", "Background preload failed for %s vs %s", nextP1.name.c_str(), nextP2.name.c_str());
    }
    isPreloading = false;
}

static void movePlayer(FighterPlayer& dest, FighterPlayer& src) {
    dest.name = src.name;
    dest.height = src.height;
    dest.ground_y = src.ground_y;
    dest.head_y = src.head_y;
    dest.origin_x = src.origin_x;
    dest.width_px = src.width_px;
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

void FighterEngine::startFight() {
    if (millis() < retryDelayEnd) return;
    if (numAvailableFighters < 2) return;
    
    if (isNextReady) {
        freeFighter(p1);
        freeFighter(p2);

        movePlayer(p1, nextP1);
        movePlayer(p2, nextP2);
        isNextReady = false;

        loadDir = getFightersDir();
        int scale = (matrix && matrix->height() >= 64 && loadDir.endsWith("32")) ? (matrix->height() / 32) : 1;

        // Place P1 at 1 pixel from the top of the screen
        p1.direction = 1; 
        p1.x = -p1.width_px * scale; 
        p1.y = (1 - p1.head_y) * scale;
        
        // Align ground line to P1's physical feet, place P2 on same ground line
        int ground_y_screen = 1 - p1.head_y + p1.ground_y;
        p2.direction = -1; 
        p2.x = matrix ? matrix->width() : 128;
        p2.y = (ground_y_screen - p2.ground_y) * scale;
        
        setPlayerState(p1, FIGHTER_WALK);
        setPlayerState(p2, FIGHTER_WALK);
        p1.hasHit = false; p2.hasHit = false; p1.isDead = false; p2.isDead = false;
        fightStartTime = millis(); fightEndTime = 0; lastMoveTime = millis();
        LOGI("FighterEngine", "🥊 Match -> [P1: %s] (H:%d) vs [P2: %s] (H:%d)", p1.name.c_str(), p1.height, p2.name.c_str(), p2.height);
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
    if (newState == FIGHTER_WALK) anim = &p.animWalk;
    else if (newState == FIGHTER_ATTACK) anim = &p.animAttack;
    else if (newState == FIGHTER_HIT) anim = &p.animHit;
    else if (newState == FIGHTER_WIN) anim = &p.animWin;
    else if (newState == FIGHTER_SPECIAL) anim = &p.animSpecial;
    else if (newState == FIGHTER_SUPER) anim = &p.animSuper;
    else if (newState == FIGHTER_FALL) anim = &p.animFall;
    
    if (p.activeFile) p.activeFile.close();

    // Reset frame cache for all animations when changing state so new frames are freshly read
    p.animWalk.cachedFrameIndex = -1;
    p.animAttack.cachedFrameIndex = -1;
    p.animHit.cachedFrameIndex = -1;
    p.animWin.cachedFrameIndex = -1;
    p.animSpecial.cachedFrameIndex = -1;
    p.animSuper.cachedFrameIndex = -1;
    p.animFall.cachedFrameIndex = -1;

    if (anim && anim->loaded && !anim->psramBuffer) {
        int newSize = anim->width * anim->height * 2;
        if (newSize > p.currentBufferSize) {
            if (p.currentFrameBuffer) {
                if (m_hasPsram) heap_caps_free(p.currentFrameBuffer);
                else free(p.currentFrameBuffer);
            }
            if (m_hasPsram) {
                p.currentFrameBuffer = (uint8_t*)heap_caps_malloc(newSize, MALLOC_CAP_SPIRAM);
            } else {
                p.currentFrameBuffer = (uint8_t*)malloc(newSize);
            }
            p.currentBufferSize = newSize;
        }
        p.activeFile = sd.open(anim->filepath.c_str(), FILE_OPEN_READ);
    }
}

bool FighterEngine::loop() {
    if (millis() < retryDelayEnd) return true;
    
    if (millis() < hitStopUntilMillis) return true;
    
    if (!active) {
        startFight();
        if (!active) return true;
    }
    
    uint32_t now = millis();
    
    // Update frames
    FgtAnimation* anim1 = nullptr;
    if (p1.state == FIGHTER_WALK) anim1 = &p1.animWalk;
    else if (p1.state == FIGHTER_ATTACK) anim1 = &p1.animAttack;
    else if (p1.state == FIGHTER_HIT) anim1 = &p1.animHit;
    else if (p1.state == FIGHTER_WIN) anim1 = &p1.animWin;
    else if (p1.state == FIGHTER_SPECIAL) anim1 = &p1.animSpecial;
    else if (p1.state == FIGHTER_SUPER) anim1 = &p1.animSuper;
    else if (p1.state == FIGHTER_FALL) anim1 = &p1.animFall;
    
    if (anim1 && anim1->loaded && anim1->frameDelays && (p1.currentFrame < anim1->numFrames)) {
        uint32_t delay = anim1->frameDelays[p1.currentFrame];
        if (delay < 30) delay = 30;
        if (now - p1.lastFrameTime >= delay) {
            p1.currentFrame++;
            p1.lastFrameTime = now;
            if (p1.currentFrame >= anim1->numFrames) {
                if (p1.state == FIGHTER_WALK) p1.currentFrame = 0; // Loop walk
                else if (p1.state == FIGHTER_ATTACK || p1.state == FIGHTER_SPECIAL || p1.state == FIGHTER_SUPER) setPlayerState(p1, FIGHTER_WIN);
                else if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL) { p1.currentFrame = anim1->numFrames - 1; p1.isDead = true; } // Stay on last hit frame
                else if (p1.state == FIGHTER_WIN) { p1.currentFrame = anim1->numFrames - 1; } // Stay on last win frame
            }
        }
    }
    
    FgtAnimation* anim2 = nullptr;
    if (p2.state == FIGHTER_WALK) anim2 = &p2.animWalk;
    else if (p2.state == FIGHTER_ATTACK) anim2 = &p2.animAttack;
    else if (p2.state == FIGHTER_HIT) anim2 = &p2.animHit;
    else if (p2.state == FIGHTER_WIN) anim2 = &p2.animWin;
    else if (p2.state == FIGHTER_SPECIAL) anim2 = &p2.animSpecial;
    else if (p2.state == FIGHTER_SUPER) anim2 = &p2.animSuper;
    else if (p2.state == FIGHTER_FALL) anim2 = &p2.animFall;
    
    if (anim2 && anim2->loaded && anim2->frameDelays && (p2.currentFrame < anim2->numFrames)) {
        uint32_t delay = anim2->frameDelays[p2.currentFrame];
        if (delay < 30) delay = 30;
        if (now - p2.lastFrameTime >= delay) {
            p2.currentFrame++;
            p2.lastFrameTime = now;
            if (p2.currentFrame >= anim2->numFrames) {
                if (p2.state == FIGHTER_WALK) p2.currentFrame = 0; // Loop walk
                else if (p2.state == FIGHTER_ATTACK || p2.state == FIGHTER_SPECIAL || p2.state == FIGHTER_SUPER) setPlayerState(p2, FIGHTER_WIN);
                else if (p2.state == FIGHTER_HIT || p2.state == FIGHTER_FALL) { p2.currentFrame = anim2->numFrames - 1; p2.isDead = true; } // Stay on last hit frame
                else if (p2.state == FIGHTER_WIN) { p2.currentFrame = anim2->numFrames - 1; } // Stay on last win frame
            }
        }
    }
    
    // Combat Logic
    if (p1.state == FIGHTER_WALK && p2.state == FIGHTER_WALK) {
        // Move towards each other
        uint32_t elapsed = now - lastMoveTime;
        if (elapsed >= 35) { // Speed throttle (1 pixel per 35ms -> ~28 FPS)
            int scale = (matrix->height() >= 64 && loadDir.endsWith("32")) ? (matrix->height() / 32) : 1;
            int pixelsToMove = (elapsed / 35) * scale;
            p1.x += pixelsToMove;
            p2.x -= pixelsToMove;
            lastMoveTime += (elapsed / 35) * 35; // Correctly advance time
        }
        
        // Collision detection (simple distance using origins)
        int scale = (matrix->height() >= 64 && loadDir.endsWith("32")) ? (matrix->height() / 32) : 1;
        int p1_world_origin = p1.x + (p1.origin_x * scale);
        int p2_world_origin = p2.x + ((p2.width_px - p2.origin_x) * scale);
        int dist = p2_world_origin - p1_world_origin;
        
        // Engaging distance based on sprite size, not screen width
        int engage_dist = 18 * scale;
        
        if (dist <= engage_dist) {
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
                hitStopUntilMillis = millis() + 150;
                shakeRemainingFrames = 10;
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
    
    if (fightEndTime > 0 && now - fightEndTime > 2000) {
        active = false;
        freeFighter(p1);
        freeFighter(p2);
        extern ConfigLoader config;
        retryDelayEnd = now + (config.system.idle_fighter_interval * 1000);
        triggerBackgroundPreload();
    }
    return true;
}

void FighterEngine::drawPlayer(FighterPlayer& p, int offsetY) {
    FgtAnimation* anim = nullptr;
    if (p.state == FIGHTER_WALK) anim = &p.animWalk;
    else if (p.state == FIGHTER_ATTACK) anim = &p.animAttack;
    else if (p.state == FIGHTER_HIT) anim = &p.animHit;
    else if (p.state == FIGHTER_WIN) anim = &p.animWin;
    else if (p.state == FIGHTER_SPECIAL) anim = &p.animSpecial;
    else if (p.state == FIGHTER_SUPER) anim = &p.animSuper;
    else if (p.state == FIGHTER_FALL) anim = &p.animFall;
    
    if (!anim || !anim->loaded) return;
    
    if (p.currentFrame >= anim->numFrames) return;
    
    int frameSize = anim->width * anim->height * 2;
    uint8_t* ptr = nullptr;
    
    if (anim->psramBuffer) {
        ptr = anim->psramBuffer + (p.currentFrame * frameSize);
    } else {
        if (p.currentFrame != anim->cachedFrameIndex) {
            if (p.activeFile && p.currentFrameBuffer) {
                uint32_t offset = anim->pixelsOffset + (p.currentFrame * frameSize);
                p.activeFile.seek(offset);
                p.activeFile.read(p.currentFrameBuffer, frameSize);
                anim->cachedFrameIndex = p.currentFrame;
            } else {
                return;
            }
        }
        ptr = p.currentFrameBuffer;
    }
    
    if (!ptr) return;
    
    bool invert = (p.state == FIGHTER_SUPER && p.currentFrame < 2);
    
    int scale = (matrix->height() >= 64 && loadDir.endsWith("32")) ? (matrix->height() / 32) : 1;
    
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
    if (!active) return;
    
    int globalOffsetY = 0;
    if (shakeRemainingFrames > 0) {
        globalOffsetY = random(-2, 3);
        shakeRemainingFrames--;
    }
    
    // Draw the dead player first (background), then the winner (foreground)
    if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL || p1.isDead) {
        drawPlayer(p1, globalOffsetY);
        drawPlayer(p2, globalOffsetY);
    } else {
        drawPlayer(p2, globalOffsetY);
        drawPlayer(p1, globalOffsetY);
    }
}
