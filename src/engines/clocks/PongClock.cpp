#include "PongClock.h"
#include "../../core/ConfigLoader.h"
#include <stdlib.h>

PongClock::PongClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config) : ClockFace(display, config), lastMinute(-1), lastHour(-1), forceMissLeft(false), forceMissRight(false), lastFrameTime(0) {
    storedTime = {0, 0, 0};
    ball_size = max(2, (int)(matrix->height() / 16));
    pad_w = max(2, (int)(matrix->width() / 32));
    pad_h = max(8, (int)(matrix->height() / 3.0f));
    
    p1_y = matrix->height() / 2.0f;
    p2_y = p1_y;
    resetBall(true);
}

void PongClock::resetBall(bool leftServed) {
    ball_y = matrix->height() / 2.0f;
    ball_dy = (((float)rand() / RAND_MAX) * 3.0f - 1.5f); // -1.5 to 1.5
    
    float base_dx = max(1.5f, matrix->width() / 40.0f);
    if (leftServed) {
        ball_x = pad_w + 2;
        ball_dx = base_dx;
    } else {
        ball_x = matrix->width() - pad_w - 3;
        ball_dx = -base_dx;
    }
}

void PongClock::draw(const TimeData& t) {
    storedTime = t;
}

void PongClock::drawScores() {
    matrix->setFont(NULL);
    int gfxSize = (engineConfig ? engineConfig->getInt("clock_size", engineConfig->getInt("size", 1)) : 1);
    if (gfxSize < 1) gfxSize = 1;
    matrix->setTextSize(gfxSize);
    
    char scoreLeft[3];
    char scoreRight[3];
    sprintf(scoreLeft, "%02d", storedTime.hours);
    sprintf(scoreRight, "%02d", storedTime.minutes);
    
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
    
    int center = matrix->width() / 2;
    int yOffset = max(4, (matrix->height() / 8)); // Scaled margin
    
    // Draw left score
    matrix->setTextColor(matrix->color565(255, 255, 255));
    matrix->setCursor(center - bw - 8, yOffset - by);
    matrix->print(scoreLeft);
    
    // Draw right score
    matrix->setCursor(center + 8, yOffset - by);
    matrix->print(scoreRight);
}

void PongClock::update() {
    if (lastMinute == -1) {
        lastMinute = storedTime.minutes;
        lastHour = storedTime.hours;
    } else {
        if (lastMinute != storedTime.minutes) {
            forceMissLeft = true;
            lastMinute = storedTime.minutes;
        }
        if (lastHour != storedTime.hours) {
            forceMissRight = true;
            lastHour = storedTime.hours;
        }
    }

    // No internal throttle, rely on main loop 60 FPS
        
    // Physics update
    ball_x += ball_dx;
    ball_y += ball_dy;
    
    // Top/Bottom bounce
    if (ball_y <= 0) {
        ball_y = 0;
        ball_dy *= -1;
    } else if (ball_y >= matrix->height() - ball_size) {
        ball_y = matrix->height() - ball_size;
        ball_dy *= -1;
    }
    
    float ai_speed = matrix->height() / 20.0f;
    // P1 AI (Left)
    float target_p1 = ball_y - (pad_h / 2);
    if (ball_dx < 0) {
        if (forceMissLeft) {
            if (ball_y > matrix->height() / 2) target_p1 = 0;
            else target_p1 = matrix->height() - pad_h;
        }
        if (p1_y < target_p1) p1_y += min(ai_speed, target_p1 - p1_y);
        if (p1_y > target_p1) p1_y -= min(ai_speed, p1_y - target_p1);
    }
    
    // P2 AI (Right)
    float target_p2 = ball_y - (pad_h / 2);
    if (ball_dx > 0) {
        if (forceMissRight) {
            if (ball_y > matrix->height() / 2) target_p2 = 0;
            else target_p2 = matrix->height() - pad_h;
        }
        if (p2_y < target_p2) p2_y += min(ai_speed, target_p2 - p2_y);
        if (p2_y > target_p2) p2_y -= min(ai_speed, p2_y - target_p2);
    }
    
    // Clamp
    if (p1_y < 0) p1_y = 0;
    if (p1_y > matrix->height() - pad_h) p1_y = matrix->height() - pad_h;
    if (p2_y < 0) p2_y = 0;
    if (p2_y > matrix->height() - pad_h) p2_y = matrix->height() - pad_h;
    
    // Paddle Collisions
    if (ball_dx < 0 && ball_x <= pad_w + ball_size) {
        if (ball_y >= p1_y - ball_size && ball_y <= p1_y + pad_h) {
            ball_x = pad_w + ball_size + 1;
            ball_dx = abs(ball_dx);
        }
    } else if (ball_dx > 0 && ball_x >= matrix->width() - pad_w - ball_size) {
        if (ball_y >= p2_y - ball_size && ball_y <= p2_y + pad_h) {
            ball_x = matrix->width() - pad_w - ball_size - 1;
            ball_dx = -abs(ball_dx);
        }
    }
    
    // Scoring
    if (ball_x < -10) {
        resetBall(true);
        forceMissLeft = false;
    } else if (ball_x > matrix->width() + 10) {
        resetBall(false);
        forceMissRight = false;
    }

    
    // Draw
    uint16_t white = matrix->color565(255, 255, 255);
    uint16_t grey = matrix->color565(100, 100, 100);
    
    // Draw middle line
    int center = matrix->width() / 2;
    for (int y = 0; y < matrix->height(); y += 4) {
        matrix->drawPixel(center, y, grey);
        matrix->drawPixel(center, y+1, grey);
    }
    
    drawScores();
    
    // Draw paddles
    matrix->fillRect(0, (int)p1_y, pad_w, pad_h, white);
    matrix->fillRect(matrix->width() - pad_w, (int)p2_y, pad_w, pad_h, white);
    
    // Draw ball
    matrix->fillRect((int)ball_x, (int)ball_y, ball_size, ball_size, white);
}
