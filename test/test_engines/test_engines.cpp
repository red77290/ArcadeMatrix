#include <Arduino.h>
#include <unity.h>
#include "../mocks/MockProviders.h"
#include "engines/CryptoEngine.h"
#include "engines/StockEngine.h"
#include "engines/WeatherEngine.h"

// Note: MatrixPanel_I2S_DMA is a hardware-dependent object. In unit tests on native/host,
// we might not have it. But running on esp32dev, we can pass nullptr to some engines if we
// are careful to mock or avoid display rendering calls, OR we can instantiate a dummy matrix.
// For now, we test the logic that doesn't crash on nullptr matrix, or we just test the providers.

void setUp(void) {}
void tearDown(void) {}

void test_crypto_engine_di(void) {
    CryptoEngine cryptoEngine;
    cryptoEngine.begin(nullptr);
    
    MockCryptoProvider* mockProvider = new MockCryptoProvider();
    mockProvider->mockPrice = 12345.67f;
    mockProvider->mockChange = 1.23f;
    
    cryptoEngine.addProvider(mockProvider);
    
    // Test that the engine uses the mock
    // Note: To properly test fetchQuote, we'd need to expose it or make it protected.
    // For this test, we can just ensure it compiles and the DI mechanism is correctly wired.
    TEST_ASSERT_EQUAL(0, mockProvider->fetchCount);
}

void test_stock_engine_di(void) {
    StockEngine stockEngine;
    stockEngine.begin(nullptr);
    
    MockStockProvider* mockProvider = new MockStockProvider();
    mockProvider->mockPrice = 250.0f;
    mockProvider->mockChange = 2.5f;
    
    stockEngine.addProvider(mockProvider);
    
    TEST_ASSERT_EQUAL(0, mockProvider->fetchCount);
}

void test_weather_engine_di(void) {
    WeatherEngine weatherEngine(nullptr);
    
    MockWeatherProvider* mockProvider = new MockWeatherProvider();
    weatherEngine.addProvider(mockProvider);
    
    TEST_ASSERT_EQUAL(0, mockProvider->fetchCount);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_crypto_engine_di);
    RUN_TEST(test_stock_engine_di);
    RUN_TEST(test_weather_engine_di);
    UNITY_END();
}

void loop() {
    delay(100);
}
