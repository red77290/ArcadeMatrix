#include "RotationTransitionFX.h"
#include <math.h>

RotationTransitionFX::RotationTransitionFX()
    : _active(false), _apexApplied(false), _fromRot(0), _toRot(0),
      _configuredEffect(RotationEffect::PARTICLE_VORTEX),
      _activeEffect(RotationEffect::PARTICLE_VORTEX),
      _durationMs(400), _startTime(0) {
    memset(_particles, 0, sizeof(_particles));
}

RotationEffect RotationTransitionFX::parseEffect(const String& name) {
    String n = name;
    n.toLowerCase();
    n.trim();
    if (n == "vortex" || n == "particle_vortex" || n == "particles") return RotationEffect::PARTICLE_VORTEX;
    if (n == "glitch" || n == "cyber_glitch") return RotationEffect::CYBER_GLITCH;
    if (n == "slide" || n == "smooth_slide") return RotationEffect::SMOOTH_SLIDE;
    if (n == "zoom" || n == "tunnel_zoom") return RotationEffect::TUNNEL_ZOOM;
    if (n == "matrix" || n == "matrix_rain") return RotationEffect::MATRIX_RAIN;
    if (n == "wipe") return RotationEffect::WIPE;
    if (n == "curtain") return RotationEffect::CURTAIN;
    if (n == "dissolve") return RotationEffect::DISSOLVE;
    if (n == "checker" || n == "checkerboard") return RotationEffect::CHECKER;
    if (n == "shutter" || n == "blinds") return RotationEffect::SHUTTER;
    if (n == "random") return RotationEffect::RANDOM;
    return RotationEffect::NONE;
}

String RotationTransitionFX::effectToString(RotationEffect effect) {
    switch (effect) {
        case RotationEffect::PARTICLE_VORTEX: return "vortex";
        case RotationEffect::CYBER_GLITCH: return "glitch";
        case RotationEffect::SMOOTH_SLIDE: return "slide";
        case RotationEffect::TUNNEL_ZOOM: return "zoom";
        case RotationEffect::MATRIX_RAIN: return "matrix";
        case RotationEffect::RANDOM: return "random";
        default: return "none";
    }
}

uint16_t RotationTransitionFX::getRandomArcadeColor() {
    static const uint16_t PALETTE[] = {
        0x07FF, // Cyan
        0xF81F, // Magenta
        0xFFE0, // Yellow
        0x07E0, // Bright Green
        0x001F, // Electric Blue
        0xFD20, // Neon Orange
        0xFFFF  // White
    };
    return PALETTE[random(0, sizeof(PALETTE) / sizeof(PALETTE[0]))];
}

void RotationTransitionFX::initParticles(int16_t w, int16_t h, bool clockwise) {
    float cx = w / 2.0f;
    float cy = h / 2.0f;
    float maxDist = sqrtf(cx * cx + cy * cy);

    for (size_t i = 0; i < MAX_PARTICLES; i++) {
        float angle = (float)random(0, 360) * (3.14159265f / 180.0f);
        _particles[i].angle = angle;
        _particles[i].dist = (float)random((int)(maxDist * 0.3f), (int)maxDist);
        _particles[i].speed = (float)random(20, 60) / 10.0f;
        if (!clockwise) _particles[i].speed = -_particles[i].speed;
        _particles[i].x = cx + cosf(angle) * _particles[i].dist;
        _particles[i].y = cy + sinf(angle) * _particles[i].dist;
        _particles[i].vx = 0;
        _particles[i].vy = 0;
        _particles[i].color = getRandomArcadeColor();
        _particles[i].life = (uint8_t)random(15, 30);
        _particles[i].maxLife = _particles[i].life;
    }
}

void RotationTransitionFX::start(uint8_t fromRot, uint8_t toRot, RotationEffect effect, uint32_t durationMs, int16_t w, int16_t h) {
    if (effect == RotationEffect::NONE || durationMs == 0) {
        _active = false;
        return;
    }

    _fromRot = fromRot % 4;
    _toRot = toRot % 4;
    _configuredEffect = effect;
    _durationMs = durationMs;
    _startTime = millis();
    _apexApplied = false;
    _holding = false;
    _active = true;

    if (_configuredEffect == RotationEffect::RANDOM) {
        // 1..5 are the orientation effects, 7..11 the slot ones; 6 is RANDOM itself, so skip it.
        uint8_t r = random(1, 11);
        if (r >= 6) r++;
        _activeEffect = static_cast<RotationEffect>(r);
    } else {
        _activeEffect = _configuredEffect;
    }

    // Direction (clockwise vs counter-clockwise)
    int diff = (_toRot - _fromRot + 4) % 4;
    bool clockwise = (diff == 1 || diff == 2);
    initParticles(w, h, clockwise);
}

void RotationTransitionFX::stop() {
    _active = false;
    _apexApplied = false;
}

bool RotationTransitionFX::render(Adafruit_GFX* display, void (*onApexReached)(uint8_t targetRot)) {
    if (!_active || !display) return false;

    uint32_t now = millis();
    // While held, keep the clock at the midpoint so the panel stays covered. A covered panel is
    // nearly black, so the wait needs something moving on it or it reads as a longer blank than
    // the one the transition was added to hide.
    bool drawWaitPulse = false;
    if (_holding && (now - _startTime) >= _durationMs / 2) {
        _startTime = now - _durationMs / 2;
        drawWaitPulse = true;
    }
    uint32_t elapsed = now - _startTime;

    if (elapsed >= _durationMs) {
        if (!_apexApplied && onApexReached) {
            onApexReached(_toRot);
            _apexApplied = true;
        }
        _active = false;
        display->fillScreen(0x0000);
        return false;
    }

    float progress = (float)elapsed / (float)_durationMs;

    // Apply hardware rotation at apex (50% mark)
    if (progress >= 0.5f && !_apexApplied) {
        if (onApexReached) {
            onApexReached(_toRot);
        }
        _apexApplied = true;
    }

    int16_t w = display->width();
    int16_t h = display->height();

    if (drawWaitPulse) {
        // A band sweeping the covered panel: the sign looks busy rather than switched off.
        display->fillScreen(0x0000);
        const uint16_t glowCol = getRandomArcadeColor();
        int16_t bandW = (w >= 128) ? 24 : 12;
        int16_t span = w + bandW * 2;
        int16_t head = (int16_t)(((now / 4) % (uint32_t)span) - bandW);
        for (int16_t i = 0; i < bandW; i++) {
            int16_t x = head + i;
            if (x < 0 || x >= w) continue;
            uint8_t fade = (uint8_t)(255 - (i * 255 / bandW));
            uint16_t col = (fade > 160) ? glowCol : ((fade > 80) ? 0x4208 : 0x2104);
            display->drawFastVLine(x, h / 2 - h / 8, h / 4, col);
        }
        return true;
    }

    switch (_activeEffect) {
        case RotationEffect::WIPE:     renderWipe(display, progress, w, h); return true;
        case RotationEffect::CURTAIN:  renderCurtain(display, progress, w, h); return true;
        case RotationEffect::DISSOLVE: renderDissolve(display, progress, w, h); return true;
        case RotationEffect::CHECKER:  renderChecker(display, progress, w, h); return true;
        case RotationEffect::SHUTTER:  renderShutter(display, progress, w, h); return true;
        case RotationEffect::PARTICLE_VORTEX:
            renderVortex(display, progress, w, h);
            break;
        case RotationEffect::CYBER_GLITCH:
            renderGlitch(display, progress, w, h);
            break;
        case RotationEffect::SMOOTH_SLIDE:
            renderSlide(display, progress, w, h);
            break;
        case RotationEffect::TUNNEL_ZOOM:
            renderZoom(display, progress, w, h);
            break;
        case RotationEffect::MATRIX_RAIN:
            renderMatrixRain(display, progress, w, h);
            break;
        default:
            renderVortex(display, progress, w, h);
            break;
    }

    return true;
}

void RotationTransitionFX::renderVortex(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    float cx = w / 2.0f;
    float cy = h / 2.0f;

    // Phase 1 (0.0 -> 0.5): Inward collapse & spiral
    // Phase 2 (0.5 -> 1.0): Outward radial burst & settle
    float phaseProgress = (progress < 0.5f) ? (progress / 0.5f) : ((progress - 0.5f) / 0.5f);
    bool expanding = (progress >= 0.5f);

    for (size_t i = 0; i < MAX_PARTICLES; i++) {
        float factor = expanding ? phaseProgress : (1.0f - phaseProgress);
        float currentDist = _particles[i].dist * factor;
        float currentAngle = _particles[i].angle + (progress * 8.0f * (_particles[i].speed > 0 ? 1.0f : -1.0f));

        int16_t px = (int16_t)(cx + cosf(currentAngle) * currentDist);
        int16_t py = (int16_t)(cy + sinf(currentAngle) * currentDist);

        if (px >= 0 && px < w && py >= 0 && py < h) {
            display->drawPixel(px, py, _particles[i].color);
            // Draw a subtle trail point
            float trailDist = currentDist * 1.15f;
            float trailAngle = currentAngle - 0.2f;
            int16_t tx = (int16_t)(cx + cosf(trailAngle) * trailDist);
            int16_t ty = (int16_t)(cy + sinf(trailAngle) * trailDist);
            if (tx >= 0 && tx < w && ty >= 0 && ty < h) {
                display->drawPixel(tx, ty, 0x18C3); // Dark cyan trail
            }
        }
    }

    // Core vortex energy pulse
    int16_t coreRadius = (int16_t)((1.0f - fabsf(progress - 0.5f) * 2.0f) * (h / 3.0f));
    if (coreRadius > 1) {
        display->drawCircle((int16_t)cx, (int16_t)cy, coreRadius, 0x07FF); // Cyan
        if (coreRadius > 3) {
            display->drawCircle((int16_t)cx, (int16_t)cy, coreRadius - 2, 0xFFFF); // White inner
        }
    }
}

void RotationTransitionFX::renderGlitch(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);

    float intensity = 1.0f - fabsf(progress - 0.5f) * 2.0f; // Peaks at 0.5
    int numSlices = (int)(intensity * (h / 3)) + 2;

    for (int i = 0; i < numSlices; i++) {
        int16_t y = random(0, h);
        int16_t sliceH = random(1, 4);
        int16_t shift = (int16_t)(random(-16, 17) * intensity);
        uint16_t col = (i % 2 == 0) ? 0xF81F : 0x07FF; // Magenta or Cyan

        display->fillRect(shift, y, w, sliceH, col);
        // White noise pixels
        for (int p = 0; p < (int)(w * 0.25f); p++) {
            int16_t nx = random(0, w);
            int16_t ny = random(0, h);
            display->drawPixel(nx, ny, 0xFFFF);
        }
    }
}

void RotationTransitionFX::renderSlide(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);

    // Directional beam swipe
    float pos = progress * (w + h);
    int16_t beamX = (int16_t)(progress * (w + 20) - 10);

    for (int16_t y = 0; y < h; y += 2) {
        int16_t offset = (int16_t)(sinf((y + pos) * 0.2f) * 6.0f);
        display->drawLine(beamX + offset - 8, y, beamX + offset, y, 0x001F);  // Blue tail
        display->drawLine(beamX + offset, y, beamX + offset + 4, y, 0x07FF);     // Cyan core
        display->drawLine(beamX + offset + 4, y, beamX + offset + 6, y, 0xFFFF); // White tip
    }
}

void RotationTransitionFX::renderZoom(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    float cx = w / 2.0f;
    float cy = h / 2.0f;

    float scale = (progress < 0.5f) ? (1.0f - progress * 1.8f) : ((progress - 0.5f) * 1.8f + 0.1f);
    if (scale < 0.05f) scale = 0.05f;

    for (int r = 1; r <= 3; r++) {
        float boxW = (w * scale) * (r / 3.0f);
        float boxH = (h * scale) * (r / 3.0f);
        int16_t x = (int16_t)(cx - boxW / 2.0f);
        int16_t y = (int16_t)(cy - boxH / 2.0f);
        uint16_t col = (r == 3) ? 0x07FF : ((r == 2) ? 0xF81F : 0xFFE0);
        display->drawRect(x, y, (int16_t)boxW, (int16_t)boxH, col);
    }
}

void RotationTransitionFX::renderMatrixRain(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);

    int numCols = w / 6;
    for (int c = 0; c < numCols; c++) {
        int16_t colX = c * 6 + 1;
        float speed = ((c % 3) + 1) * 1.5f;
        int16_t headY = (int16_t)(fmodf(progress * h * speed * 2.0f, (float)(h + 12)) - 6);

        // Head pixel
        if (headY >= 0 && headY < h) {
            display->drawPixel(colX, headY, 0xFFFF); // White head
        }
        // Falling trail
        for (int t = 1; t <= 5; t++) {
            int16_t ty = headY - t;
            if (ty >= 0 && ty < h) {
                uint16_t greenShade = (t == 1) ? 0x07E0 : ((t <= 3) ? 0x04E0 : 0x0280);
                display->drawPixel(colX, ty, greenShade);
            }
        }
    }
}


// ---------------------------------------------------------------------------
// Slot transitions: the first half covers the panel, the second half reveals it
// again, so whatever is being loaded behind has time to arrive.
// ---------------------------------------------------------------------------

static inline uint16_t fxAccent(uint8_t step) {
    static const uint16_t palette[] = { 0x07FF, 0x07E0, 0xFD20, 0xF81F, 0xFFE0 };
    return palette[step % 5];
}

void RotationTransitionFX::renderWipe(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    bool closing = progress < 0.5f;
    float t = closing ? (progress * 2.0f) : (1.0f - (progress - 0.5f) * 2.0f);
    int16_t edge = (int16_t)(t * w);
    if (edge > 0) display->fillRect(closing ? 0 : w - edge, 0, edge, h, 0x2104);   // near-black veil
    int16_t barX = closing ? edge : w - edge;
    display->drawFastVLine(barX, 0, h, fxAccent(0));
    display->drawFastVLine(barX + (closing ? -1 : 1), 0, h, 0x0410);
}

void RotationTransitionFX::renderCurtain(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    bool closing = progress < 0.5f;
    float t = closing ? (progress * 2.0f) : (1.0f - (progress - 0.5f) * 2.0f);
    int16_t half = (int16_t)(t * (w / 2 + 1));
    if (half > 0) {
        display->fillRect(0, 0, half, h, 0x2104);
        display->fillRect(w - half, 0, half, h, 0x2104);
        display->drawFastVLine(half - 1, 0, h, fxAccent(1));
        display->drawFastVLine(w - half, 0, h, fxAccent(1));
    }
}

void RotationTransitionFX::renderDissolve(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    bool closing = progress < 0.5f;
    float t = closing ? (progress * 2.0f) : (1.0f - (progress - 0.5f) * 2.0f);
    // A fixed lattice thinned by `t`: cheap, and it reads as the picture breaking up.
    int step = 2;
    uint16_t colour = fxAccent(2);
    for (int16_t y = 0; y < h; y += step) {
        for (int16_t x = 0; x < w; x += step) {
            uint8_t nibble = (uint8_t)(((x * 7) ^ (y * 13)) & 0x0F);
            if (nibble < (uint8_t)(t * 16.0f)) display->drawPixel(x + (y & 1), y, colour);
        }
    }
}

void RotationTransitionFX::renderChecker(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    bool closing = progress < 0.5f;
    float t = closing ? (progress * 2.0f) : (1.0f - (progress - 0.5f) * 2.0f);
    int16_t cell = (h >= 32) ? 8 : 4;
    int cols = (w + cell - 1) / cell, rows = (h + cell - 1) / cell;
    int total = cols * rows;
    int filled = (int)(t * total);
    for (int i = 0; i < total; i++) {
        int idx = ((i * 7) + ((i % 3) * 11)) % total;   // scattered order, no allocation
        if (idx >= filled) continue;
        int16_t cx = (i % cols) * cell, cy = (i / cols) * cell;
        display->fillRect(cx, cy, cell - 1, cell - 1, (i & 1) ? 0x2104 : fxAccent(3));
    }
}

void RotationTransitionFX::renderShutter(Adafruit_GFX* display, float progress, int16_t w, int16_t h) {
    display->fillScreen(0x0000);
    bool closing = progress < 0.5f;
    float t = closing ? (progress * 2.0f) : (1.0f - (progress - 0.5f) * 2.0f);
    int16_t slat = (h >= 32) ? 8 : 4;
    int16_t fill = (int16_t)(t * slat);
    if (fill <= 0) return;
    for (int16_t y = 0; y < h; y += slat) {
        display->fillRect(0, y, w, fill, 0x2104);
        display->drawFastHLine(0, y + fill - 1, w, fxAccent(4));
    }
}
