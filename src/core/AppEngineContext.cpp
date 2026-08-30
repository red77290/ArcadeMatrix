#include "AppEngineContext.h"
#include "DisplayOrientationManager.h"

DisplayGeometry AppEngineContext::getGeometry() const {
    return displayOrientationManager.getGeometry();
}
