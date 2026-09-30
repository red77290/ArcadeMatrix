#include "CastleClock.h"
#include "CastleAssets.h"
#include "ClockwiseScene.h"
#include <math.h>

namespace {
// Positions and angles as they are in the original face.
constexpr int HAND_LEN = 10;
constexpr int PIVOT_X = 32;
constexpr int PIVOT_Y = 28;
constexpr uint16_t HAND_COLOR = 0xB58C;
constexpr float HOUR_OFFSET = -30.0f;
constexpr float MIN_OFFSET = -6.0f;
}  // namespace

CastleClock::CastleClock(MatrixPanel_I2S_DMA* display, const EngineConfig* config)
    : ClockFace(display, config) {
    storedTime = { 0, 0, 0 };
}

void CastleClock::draw(const TimeData& t) {
    storedTime = t;
}

void CastleClock::drawHand(float angle, int length, uint16_t color, int panelW, int panelH) {
    int d = ClockwiseScene::divisor(panelH);
    int x0 = ClockwiseScene::mapX(PIVOT_X, panelW, panelH);
    int y0 = ClockwiseScene::mapY(PIVOT_Y, panelH);
    int x1 = x0 + (int)(sinf(angle) * length / d);
    int y1 = y0 + (int)(cosf(angle) * length / d);
    matrix->drawLine(x0, y0, x1, y1, color);
}

void CastleClock::update() {
    if (!matrix) return;
    const int w = matrix->width();
    const int h = matrix->height();

    if (lastMinute != storedTime.minutes) {
        lastMinute = storedTime.minutes;
        m_dirty = 2;
    }
    if (m_dirty == 0) { m_hasFrame = false; return; }
    m_dirty--;
    m_hasFrame = true;

    ClockwiseScene::drawBackground(matrix, _CLOCK_TOWER, w, h);

    float extra = (HOUR_OFFSET * storedTime.minutes) / 60.0f;
    float hourAngle = ((storedTime.hours * HOUR_OFFSET) + 180.0f + extra) * 3.14159f / 180.0f;
    float minAngle = ((storedTime.minutes * MIN_OFFSET) + 180.0f) * 3.14159f / 180.0f;
    drawHand(minAngle, HAND_LEN, HAND_COLOR, w, h);
    drawHand(hourAngle, HAND_LEN - 3, HAND_COLOR, w, h);
}

void CastleClock::onDisplayGeometryChanged(const DisplayGeometry& geometry) {
    m_dirty = 2;
    lastMinute = -1;
}
