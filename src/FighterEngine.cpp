#include <SD.h>
#include "FighterEngine.h"
#include "SDUtils.h"

#define MAX_FIGHTER_FRAME_SIZE 20480

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
    
    File f = SD.open(indexPath);
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
    Serial.printf("FighterEngine: Loaded %d fighters (Fast Offset mode: %d bytes RAM)\n", numAvailableFighters, numAvailableFighters * 4);
}

String FighterEngine::getRandomFighterName() {
    if (numAvailableFighters == 0 || !fighterOffsets) return "";
    
    String indexPath = getFightersDir() + "/index.txt";
    File f = SD.open(indexPath);
    if (!f) return "";
    
    int targetLine = random(0, numAvailableFighters);
    f.seek(fighterOffsets[targetLine]);
    String result = f.readStringUntil('\n');
    result.trim();
    
    f.close();
    return result;
}

bool FighterEngine::loadFighterAnim(FgtAnimation& anim, const char* filepath) {
    File f = SD.open(filepath, FILE_READ);
    if (!f) {
        Serial.printf("FighterEngine Error: Could not open file %s\n", filepath);
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
    if (frameSize > MAX_FIGHTER_FRAME_SIZE) {
        Serial.printf("FighterEngine Error: Frame too big! %d bytes for %s\n", frameSize, filepath);
        free(anim.frameDelays);
        f.close();
        return false;
    }
    
    f.close();
    
    anim.loaded = true;
    return true;
}

void FighterEngine::freeAnim(FgtAnimation& anim) {
    if (anim.loaded) {
        if (anim.frameDelays) { free(anim.frameDelays); anim.frameDelays = nullptr; }
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
}

void FighterEngine::startFight() {
    if (millis() < retryDelayEnd) return;
    if (numAvailableFighters < 2) return;
    
    freeFighter(p1);
    freeFighter(p2);
    
    p1.name = getRandomFighterName();
    do {
        p2.name = getRandomFighterName();
    } while (p1.name == p2.name && numAvailableFighters > 1);
    
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
            else currentLoadState = LOAD_P2_WALK;
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
            else {
                // Done! Finish setup
                p1.direction = 1; p1.x = -10; p1.y = 0;
                p2.direction = -1; p2.x = matrix->width() - p2.animWalk.width + 10;
                if (p2.x > matrix->width()) p2.x = matrix->width();
                p2.y = 0;
                setPlayerState(p1, FIGHTER_WALK);
                setPlayerState(p2, FIGHTER_WALK);
                p1.hasHit = false; p2.hasHit = false; p1.isDead = false; p2.isDead = false;
                fightStartTime = millis(); fightEndTime = 0; lastMoveTime = millis();
                active = true;
                currentLoadState = LOAD_IDLE;
            }
            break;
        case LOAD_FINISH:
            Serial.printf("FighterEngine Error: Failed to start fight\n");
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
        }
        p.activeFile = SD.open(anim->filepath, FILE_READ);
    }
}

void FighterEngine::loop() {
    if (millis() < retryDelayEnd) return;
    
    if (currentLoadState != LOAD_IDLE) {
        processLoadState();
        return;
    }
    
    if (!active) {
        startFight();
        return;
    }
    
    uint32_t now = millis();
    
    // Update frames
    FgtAnimation* anim1 = nullptr;
    if (p1.state == FIGHTER_WALK) anim1 = &p1.animWalk;
    else if (p1.state == FIGHTER_ATTACK) anim1 = &p1.animAttack;
    else if (p1.state == FIGHTER_HIT) anim1 = &p1.animHit;
    else if (p1.state == FIGHTER_WIN) anim1 = &p1.animWin;
    
    if (anim1 && now - p1.lastFrameTime > (anim1->frameDelays[p1.currentFrame] * 2.5)) {
        p1.currentFrame++;
        p1.lastFrameTime = now;
        if (p1.currentFrame >= anim1->numFrames) {
            if (p1.state == FIGHTER_WALK) p1.currentFrame = 0; // Loop walk
            else if (p1.state == FIGHTER_ATTACK) setPlayerState(p1, FIGHTER_WIN);
            else if (p1.state == FIGHTER_HIT) { p1.currentFrame = anim1->numFrames - 1; p1.isDead = true; } // Stay on last hit frame
            else if (p1.state == FIGHTER_WIN) { p1.currentFrame = anim1->numFrames - 1; } // Stay on last win frame
        }
    }
    
    FgtAnimation* anim2 = nullptr;
    if (p2.state == FIGHTER_WALK) anim2 = &p2.animWalk;
    else if (p2.state == FIGHTER_ATTACK) anim2 = &p2.animAttack;
    else if (p2.state == FIGHTER_HIT) anim2 = &p2.animHit;
    else if (p2.state == FIGHTER_WIN) anim2 = &p2.animWin;
    
    if (anim2 && now - p2.lastFrameTime > (anim2->frameDelays[p2.currentFrame] * 2.5)) {
        p2.currentFrame++;
        p2.lastFrameTime = now;
        if (p2.currentFrame >= anim2->numFrames) {
            if (p2.state == FIGHTER_WALK) p2.currentFrame = 0; // Loop walk
            else if (p2.state == FIGHTER_ATTACK) setPlayerState(p2, FIGHTER_WIN);
            else if (p2.state == FIGHTER_HIT) { p2.currentFrame = anim2->numFrames - 1; p2.isDead = true; } // Stay on last hit frame
            else if (p2.state == FIGHTER_WIN) { p2.currentFrame = anim2->numFrames - 1; }
        }
    }
    
    // Combat Logic
    if (p1.state == FIGHTER_WALK && p2.state == FIGHTER_WALK) {
        // Move towards each other
        uint32_t elapsed = now - lastMoveTime;
        if (elapsed >= 35) { // Speed throttle (1 pixel per 35ms -> ~28 FPS)
            int pixelsToMove = elapsed / 35;
            p1.x += pixelsToMove;
            p2.x -= pixelsToMove;
            lastMoveTime += pixelsToMove * 35;
        }
        
        // Collision detection (simple distance)
        int dist = p2.x - (p1.x + p1.animWalk.width);
        if (dist <= 0) {
            // Fight! Random winner
            if (random(2) == 0) {
                setPlayerState(p1, FIGHTER_ATTACK);
                setPlayerState(p2, FIGHTER_HIT);
            } else {
                setPlayerState(p2, FIGHTER_ATTACK);
                setPlayerState(p1, FIGHTER_HIT);
            }
        }
    }
    
    // End sequence
    if (fightEndTime == 0 && (p1.isDead || p2.isDead)) {
        fightEndTime = now;
    }
    
    if (fightEndTime > 0 && now - fightEndTime > 2000) {
        active = false;
    }
}

void FighterEngine::drawPlayer(FighterPlayer& p) {
    FgtAnimation* anim = nullptr;
    if (p.state == FIGHTER_WALK) anim = &p.animWalk;
    else if (p.state == FIGHTER_ATTACK) anim = &p.animAttack;
    else if (p.state == FIGHTER_HIT) anim = &p.animHit;
    else if (p.state == FIGHTER_WIN) anim = &p.animWin;
    
    if (!anim || !anim->loaded) return;
    
    if (p.currentFrame >= anim->numFrames) return;
    
    int frameSize = anim->width * anim->height * 2;
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
    
    uint8_t* ptr = p.currentFrameBuffer;
    if (!ptr) return;
    
    int scale = 1;
    if (matrix->height() >= 64 && anim->height <= 32) {
        scale = 2; // Scale up 2x if we had to fallback to 32px fighters on 64px screen
    }
    
    for (int y = 0; y < anim->height; y++) {
        for (int x = 0; x < anim->width; x++) {
            uint16_t color = ptr[0] | (ptr[1] << 8);
            ptr += 2;
            
            if (color != anim->transparentColor) {
                int drawX = p.x + (x * scale);
                // Flip horizontally if facing left
                if (p.direction == -1) {
                    drawX = p.x + ((anim->width - 1 - x) * scale);
                }
                
                int drawY = p.y + (y * scale);
                
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int finalX = drawX + sx;
                        int finalY = drawY + sy;
                        if (finalX >= 0 && finalX < matrix->width() && finalY >= 0 && finalY < matrix->height()) {
                            matrix->drawPixel(finalX, finalY, color);
                        }
                    }
                }
            }
        }
    }
}

void FighterEngine::draw() {
    if (!active) return;
    // Draw the dead player first (background), then the winner (foreground)
    if (p1.state == FIGHTER_HIT || p1.isDead) {
        drawPlayer(p1);
        drawPlayer(p2);
    } else {
        drawPlayer(p2);
        drawPlayer(p1);
    }
}
