#include "PongClock.h"
#include "ConfigLoader.h"
#include <stdlib.h>

extern ConfigLoader config;

PongClock::PongClock(MatrixPanel_I2S_DMA* display) : ClockFace(display), lastMinute(-1), forceMiss(false), lastFrameTime(0) {
    storedTime = {0, 0, 0};
    ball_size = 2;
    pad_w = 2;
    pad_h = 12;
    
    p1_y = (matrix->height() - pad_h) / 2;
    p2_y = p1_y;
    resetBall(true);
}

void PongClock::resetBall(bool leftServed) {
    ball_y = matrix->height() / 2;
    ball_dy = ((float)rand() / RAND_MAX) * 2.0f - 1.0f; // -1.0 to 1.0
    
    if (leftServed) {
        ball_x = pad_w + 1;
        ball_dx = 1.5f;
    } else {
        ball_x = matrix->width() - pad_w - 3;
        ball_dx = -1.5f;
    }
}

void PongClock::draw(const TimeData& t) {
    storedTime = t;
}

void PongClock::drawScores() {
    matrix->setFont(NULL);
    matrix->setTextSize(1);
    
    char scoreLeft[3];
    char scoreRight[3];
    sprintf(scoreLeft, "%02d", storedTime.hours);
    sprintf(scoreRight, "%02d", storedTime.minutes);
    
    int16_t bx, by;
    uint16_t bw, bh;
    matrix->getTextBounds("88", 0, 0, &bx, &by, &bw, &bh);
    
    int center = matrix->width() / 2;
    
    // Draw left score
    matrix->setTextColor(matrix->color565(255, 255, 255));
    matrix->setCursor(center - bw - 8, 4);
    matrix->print(scoreLeft);
    
    // Draw right score
    matrix->setCursor(center + 8, 4);
    matrix->print(scoreRight);
}

void PongClock::update() {
    if (lastMinute == -1) {
        lastMinute = storedTime.minutes;
    } else if (lastMinute != storedTime.minutes) {
        forceMiss = true;
        lastMinute = storedTime.minutes;
    }

    if (millis() - lastFrameTime > 20) { // ~50 FPS
        lastFrameTime = millis();
        
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
        
        // P1 AI (Left)
        float target_p1 = ball_y - (pad_h / 2);
        if (ball_dx < 0) {
            if (p1_y < target_p1) p1_y += 1.0f;
            if (p1_y > target_p1) p1_y -= 1.0f;
        }
        
        // P2 AI (Right)
        float target_p2 = ball_y - (pad_h / 2);
        if (ball_dx > 0) {
            if (forceMiss) {
                if (ball_y > matrix->height() / 2) target_p2 = 0;
                else target_p2 = matrix->height() - pad_h;
            }
            if (p2_y < target_p2) p2_y += 1.0f;
            if (p2_y > target_p2) p2_y -= 1.0f;
        }
        
        // Clamp
        if (p1_y < 0) p1_y = 0;
        if (p1_y > matrix->height() - pad_h) p1_y = matrix->height() - pad_h;
        if (p2_y < 0) p2_y = 0;
        if (p2_y > matrix->height() - pad_h) p2_y = matrix->height() - pad_h;
        
        // Paddle Collisions
        if (ball_dx < 0 && ball_x <= pad_w) {
            if (ball_y >= p1_y - ball_size && ball_y <= p1_y + pad_h) {
                ball_x = pad_w;
                ball_dx *= -1.05f;
                ball_dy += (ball_y - (p1_y + pad_h/2)) * 0.15f;
            }
        } else if (ball_dx > 0 && ball_x >= matrix->width() - pad_w - ball_size) {
            if (ball_y >= p2_y - ball_size && ball_y <= p2_y + pad_h) {
                ball_x = matrix->width() - pad_w - ball_size;
                ball_dx *= -1.05f;
                ball_dy += (ball_y - (p2_y + pad_h/2)) * 0.15f;
            }
        }
        
        // Speed cap
        if (ball_dx > 3.0f) ball_dx = 3.0f;
        if (ball_dx < -3.0f) ball_dx = -3.0f;
        if (ball_dy > 2.5f) ball_dy = 2.5f;
        if (ball_dy < -2.5f) ball_dy = -2.5f;
        
        // Scoring
        if (ball_x < -10) {
            resetBall(true);
            forceMiss = false;
        } else if (ball_x > matrix->width() + 10) {
            resetBall(false);
            forceMiss = false;
        }
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
