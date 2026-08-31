#include <Arduino.h>
#include "core/AppRuntime.h"

#if !defined(PIO_UNIT_TESTING)
void setup() {
    app.initialize();
}

void loop() {
    app.update();
}
#endif
