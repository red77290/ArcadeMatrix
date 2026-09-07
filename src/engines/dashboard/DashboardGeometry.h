#pragma once
#include "DashboardData.h"

/**
 * @class DashboardLayoutCalculator
 * @brief Pure geometry calculator implementing dual-orientation responsive layouts.
 */
class DashboardLayoutCalculator {
public:
    static DashboardLayout calculate(const DisplayGeometry& geometry, const DashboardConfigParams& config);
};
