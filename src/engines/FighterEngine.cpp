#include "../core/SDUtils.h"
#include "FighterEngine.h"
#include <ArduinoJson.h>
#include "../core/SDUtils.h"
#include "../core/Logger.h"
#include "../core/ConfigLoader.h"

#define MAX_FIGHTER_FRAME_SIZE 98304

FighterEngine::FighterEngine(MatrixPanel_I2S_DMA* display) : matrix(display) {
}

FighterEngine::~FighterEngine() {
    if (fighterOffsets) free(fighterOffsets);
    freeFighter(p1);
    freeFighter(p2);
}

void FighterEngine::initialize() {
    loadRoster();
}

String FighterEngine::getFightersDir() {
    if (matrix->height() <= 32) return "/fighters_32";
    if (!psramFound()) {
        Serial.println("FighterEngine: No PSRAM found. Forcing /fighters_32 to avoid OOM!");
        return "/fighters_32";
    }
    return "/fighters_64";
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
    
    int targetLine = random(0, numAvailableFighters);
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
    f.read((uint8_t*)&anim.numFrames, 2);
    f.read((uint8_t*)&anim.transparentColor, 2);
    
    if (anim.numFrames == 0 || anim.width == 0 || anim.height == 0) {
        f.close();
        return false;
    }
    
    anim.frameDelays = (uint16_t*)malloc(anim.numFrames * 2);
    f.read((uint8_t*)anim.frameDelays, anim.numFrames * 2);
    anim.filepath = String(filepath);
    anim.pixelsOffset = f.position();
    anim.cachedFrameIndex = -1;
    
    int frameSize = anim.width * anim.height * 2;
    anim.totalPixelsSize = frameSize * anim.numFrames;
    if (frameSize > MAX_FIGHTER_FRAME_SIZE) {
        LOGE("FighterEngine", "Frame too big! %d bytes for %s", frameSize, filepath);
        free(anim.frameDelays);
        f.close();
        return false;
    }
    
    if (psramFound()) {
        anim.psramBuffer = (uint8_t*)heap_caps_malloc(anim.totalPixelsSize, MALLOC_CAP_SPIRAM);
        if (anim.psramBuffer) {
            size_t toRead = anim.totalPixelsSize;
                        size_t offset = 0;
                        while (toRead > 0) {
                            size_t chunk = (toRead > 8192) ? 8192 : toRead;
                            size_t r = f.read(anim.psramBuffer + offset, chunk);
                            if (r == 0) break;
                            offset += r;
                            toRead -= r;
                        }
        } else {
            LOGW("FighterEngine", "PSRAM alloc failed for %d bytes (%s). Falling back to SD.", anim.totalPixelsSize, filepath);
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
        free(p.currentFrameBuffer);
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

void FighterEngine::startFight() {
    if (millis() < retryDelayEnd) return;
    if (numAvailableFighters < 2) return;
    
    freeFighter(p1);
    freeFighter(p2);
    
    if (!getRandomFighter(p1)) return;
    
    bool found = false;
    for (int i = 0; i < 20; i++) {
        if (getRandomFighter(p2)) {
            // User constraint: Opponent must be max 20% smaller, and NEVER taller than P1.
            // Original height is stored in p.ground_y.
            if (p2.name != p1.name && p2.ground_y >= p1.ground_y * 0.8 && p2.ground_y <= p1.ground_y) {
                found = true;
                break;
            }
        }
    }
    
    if (!found) {
        // Fallback
        int attempts = 0;
        do {
            getRandomFighter(p2);
            attempts++;
        } while (p1.name == p2.name && attempts < 10);
    }
    
    loadDir = getFightersDir();
    currentLoadState = LOAD_INIT;
    active = true; // Set active to true to prevent multiple startFight calls
}

void FighterEngine::processLoadState() {
    switch (currentLoadState) {
        case LOAD_INIT:
            currentLoadState = LOAD_P1_WALK;
            break;
        case LOAD_P1_WALK:
            if (!loadFighterAnim(p1.animWalk, (loadDir + "/" + p1.name + "/walk.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P1_ATTACK;
            break;
        case LOAD_P1_ATTACK:
            if (!loadFighterAnim(p1.animAttack, (loadDir + "/" + p1.name + "/attack.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P1_HIT;
            break;
        case LOAD_P1_HIT:
            if (!loadFighterAnim(p1.animHit, (loadDir + "/" + p1.name + "/hit.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P1_WIN;
            break;
        case LOAD_P1_WIN:
            if (!loadFighterAnim(p1.animWin, (loadDir + "/" + p1.name + "/win.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P1_SPECIAL;
            break;
        case LOAD_P1_SPECIAL: {
            int t[3] = {1, 2, 3};
            for(int i=0; i<3; i++) { int r = random(3); int temp=t[i]; t[i]=t[r]; t[r]=temp; }
            for(int i=0; i<3; i++) {
                if (loadFighterAnim(p1.animSpecial, (loadDir + "/" + p1.name + "/special" + String(t[i]) + ".fgt").c_str())) break;
            }
            currentLoadState = LOAD_P1_SUPER;
            break;
        }
        case LOAD_P1_SUPER: {
            int t[3] = {1, 2, 3};
            for(int i=0; i<3; i++) { int r = random(3); int temp=t[i]; t[i]=t[r]; t[r]=temp; }
            for(int i=0; i<3; i++) {
                if (loadFighterAnim(p1.animSuper, (loadDir + "/" + p1.name + "/super" + String(t[i]) + ".fgt").c_str())) break;
            }
            currentLoadState = LOAD_P1_FALL;
            break;
        }
        case LOAD_P1_FALL:
            loadFighterAnim(p1.animFall, (loadDir + "/" + p1.name + "/fall.fgt").c_str());
            currentLoadState = LOAD_P2_WALK;
            break;
        case LOAD_P2_WALK:
            if (!loadFighterAnim(p2.animWalk, (loadDir + "/" + p2.name + "/walk.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P2_ATTACK;
            break;
        case LOAD_P2_ATTACK:
            if (!loadFighterAnim(p2.animAttack, (loadDir + "/" + p2.name + "/attack.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P2_HIT;
            break;
        case LOAD_P2_HIT:
            if (!loadFighterAnim(p2.animHit, (loadDir + "/" + p2.name + "/hit.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P2_WIN;
            break;
        case LOAD_P2_WIN:
            if (!loadFighterAnim(p2.animWin, (loadDir + "/" + p2.name + "/win.fgt").c_str())) currentLoadState = LOAD_FINISH;
            else currentLoadState = LOAD_P2_SPECIAL;
            break;
        case LOAD_P2_SPECIAL: {
            int t[3] = {1, 2, 3};
            for(int i=0; i<3; i++) { int r = random(3); int temp=t[i]; t[i]=t[r]; t[r]=temp; }
            for(int i=0; i<3; i++) {
                if (loadFighterAnim(p2.animSpecial, (loadDir + "/" + p2.name + "/special" + String(t[i]) + ".fgt").c_str())) break;
            }
            currentLoadState = LOAD_P2_SUPER;
            break;
        }
        case LOAD_P2_SUPER: {
            int t[3] = {1, 2, 3};
            for(int i=0; i<3; i++) { int r = random(3); int temp=t[i]; t[i]=t[r]; t[r]=temp; }
            for(int i=0; i<3; i++) {
                if (loadFighterAnim(p2.animSuper, (loadDir + "/" + p2.name + "/super" + String(t[i]) + ".fgt").c_str())) break;
            }
            currentLoadState = LOAD_P2_FALL;
            break;
        }
        case LOAD_P2_FALL:
            loadFighterAnim(p2.animFall, (loadDir + "/" + p2.name + "/fall.fgt").c_str());
            
            // Done! Finish setup
            {
                int scale = (matrix->height() >= 64 && getFightersDir().endsWith("32")) ? (matrix->height() / 32) : 1;
                // Align characters so their ground line touches the bottom of the screen
                int ground_screen_y = matrix->height() - 1;

                p1.direction = 1; 
                p1.x = -p1.width_px * scale; 
                p1.y = ground_screen_y - (p1.ground_y * scale);
                
                p2.direction = -1; 
                p2.x = matrix->width();
                p2.y = ground_screen_y - (p2.ground_y * scale);
                
                setPlayerState(p1, FIGHTER_WALK);
                setPlayerState(p2, FIGHTER_WALK);
                p1.hasHit = false; p2.hasHit = false; p1.isDead = false; p2.isDead = false;
                fightStartTime = millis(); fightEndTime = 0; lastMoveTime = millis();
                active = true;
                currentLoadState = LOAD_IDLE;
            }
            break;
        case LOAD_FINISH:
            LOGE("FighterEngine", "Failed to start fight for %s vs %s", p1.name.c_str(), p2.name.c_str());
            active = false;
            retryDelayEnd = millis() + 5000;
            currentLoadState = LOAD_IDLE;
            break;
        default:
            break;
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
    if (anim && anim->loaded) {
        int newSize = anim->width * anim->height * 2;
        if (newSize > p.currentBufferSize) {
            if (p.currentFrameBuffer) free(p.currentFrameBuffer);
            p.currentFrameBuffer = (uint8_t*)malloc(newSize);
            p.currentBufferSize = newSize;
            
            // Force redraw since buffer was reallocated
            p.animWalk.cachedFrameIndex = -1;
            p.animAttack.cachedFrameIndex = -1;
            p.animHit.cachedFrameIndex = -1;
            p.animWin.cachedFrameIndex = -1;
            p.animSpecial.cachedFrameIndex = -1;
            p.animSuper.cachedFrameIndex = -1;
            p.animFall.cachedFrameIndex = -1;
        }
        p.activeFile = sd.open(anim->filepath.c_str(), FILE_OPEN_READ);
    }
}

bool FighterEngine::loop() {
    if (millis() < retryDelayEnd) return true;
    
    if (currentLoadState != LOAD_IDLE) {
        processLoadState();
        return true;
    }
    
    if (millis() < hitStopUntilMillis) return true;
    
    if (!active) {
        startFight();
        return true;
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
    
    if (anim1 && anim1->loaded && anim1->frameDelays && (p1.currentFrame < anim1->numFrames) && now - p1.lastFrameTime > (anim1->frameDelays[p1.currentFrame] * 2.5)) {
        p1.currentFrame++;
        p1.lastFrameTime = now;
        if (p1.currentFrame >= anim1->numFrames) {
            if (p1.state == FIGHTER_WALK) p1.currentFrame = 0; // Loop walk
            else if (p1.state == FIGHTER_ATTACK || p1.state == FIGHTER_SPECIAL || p1.state == FIGHTER_SUPER) setPlayerState(p1, FIGHTER_WIN);
            else if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL) { p1.currentFrame = anim1->numFrames - 1; p1.isDead = true; } // Stay on last hit frame
            else if (p1.state == FIGHTER_WIN) { p1.currentFrame = anim1->numFrames - 1; } // Stay on last win frame
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
    
    if (anim2 && anim2->loaded && anim2->frameDelays && (p2.currentFrame < anim2->numFrames) && now - p2.lastFrameTime > (anim2->frameDelays[p2.currentFrame] * 2.5)) {
        p2.currentFrame++;
        p2.lastFrameTime = now;
        if (p2.currentFrame >= anim2->numFrames) {
            if (p2.state == FIGHTER_WALK) p2.currentFrame = 0; // Loop walk
            else if (p2.state == FIGHTER_ATTACK || p2.state == FIGHTER_SPECIAL || p2.state == FIGHTER_SUPER) setPlayerState(p2, FIGHTER_WIN);
            else if (p2.state == FIGHTER_HIT || p2.state == FIGHTER_FALL) { p2.currentFrame = anim2->numFrames - 1; p2.isDead = true; } // Stay on last hit frame
            else if (p2.state == FIGHTER_WIN) { p2.currentFrame = anim2->numFrames - 1; }
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
        int p1_world_origin = p1.x + p1.origin_x;
        int p2_world_origin = p2.x + (p2.width_px - p2.origin_x);
        int dist = p2_world_origin - p1_world_origin;
        int engage_dist = (int)(matrix->width() * 0.4f);
        
        if (dist <= engage_dist) {
            // Fight! Random winner
            FighterPlayer* attacker = (random(2) == 0) ? &p1 : &p2;
            FighterPlayer* target = (attacker == &p1) ? &p2 : &p1;
            
            FighterState atkState = FIGHTER_ATTACK;
            FighterState tgtState = FIGHTER_HIT;
            bool isHeavy = false;
            
            int rnd = random(100);
            if (attacker->animSuper.loaded && rnd < 20) {
                atkState = FIGHTER_SUPER;
                tgtState = target->animFall.loaded ? FIGHTER_FALL : FIGHTER_HIT;
                isHeavy = true;
            } else if (attacker->animSpecial.loaded && rnd < 50) {
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
        extern ConfigLoader config;
        active = false;
        retryDelayEnd = now + (config.idle.fighter_interval_sec * 1000);
    }
    return true;
}

void FighterEngine::drawPlayer(FighterPlayer& p) {
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
    
    int offsetY = 0;
    if (shakeRemainingFrames > 0) offsetY = random(-2, 3);
    
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
    
    if (shakeRemainingFrames > 0) shakeRemainingFrames--;
    
    // Draw the dead player first (background), then the winner (foreground)
    if (p1.state == FIGHTER_HIT || p1.state == FIGHTER_FALL || p1.isDead) {
        drawPlayer(p1);
        drawPlayer(p2);
    } else {
        drawPlayer(p2);
        drawPlayer(p1);
    }
}
