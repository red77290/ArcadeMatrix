#pragma once
#include <Arduino.h>
class MatrixPanel_I2S_DMA;
#include "../../include/core/EngineContract.h"
#include "../api/IWeatherProvider.h"
#include "dashboard/DashboardData.h"
#include "dashboard/DashboardGeometry.h"
#include "dashboard/DashboardDataProvider.h"
#include "dashboard/ClockWidget.h"
#include "dashboard/WorldClockWidget.h"
#include "dashboard/ClimateWidget.h"
#include "dashboard/MarketWidget.h"
#include "dashboard/SysInfoWidget.h"
#include "dashboard/DashboardCommon.h"

/**
 * @class DashboardEngine
 * @brief Ultra-premium Real-Time Multi-Widget Dashboard engine with responsive dual-orientation layout.
 */
class DashboardEngine : public IEngine {
public:
    DashboardEngine();
    ~DashboardEngine() override;

    EngineError initialize(EngineContext* context, const EngineConfig* config) override;
    void activate() override;
    void update(EngineContext* context) override;
    void render(EngineContext* context) override;
    void deactivate() override;
    void onConfigChanged(const EngineConfig* config) override;
    void onDisplayGeometryChanged(const DisplayGeometry& geometry) override;

    bool isRealtime() const override { return true; }

private:
    MatrixPanel_I2S_DMA* matrix;
    DashboardConfigParams m_config;
    DisplayGeometry m_geometry;
    DashboardLayout m_cachedLayout;
    bool m_layoutDirty;

    DashboardDataProvider m_dataProvider;
    String m_weatherApiKey;
    String m_weatherCity;
    String m_weatherUnits;
};

class DashboardEngineDescriptorHandler : public IEngineDescriptorHandler {
public:
    EngineDescriptor getDescriptor() const override;
};
