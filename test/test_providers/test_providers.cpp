#include <Arduino.h>
#include <unity.h>
#include "api/CoinGeckoProvider.h"
#include "api/BinanceProvider.h"
#include "api/OpenWeatherMapProvider.h"
#include "api/YahooFinanceProvider.h"

void setUp(void) {}
void tearDown(void) {}

void test_parse_coingecko_primary(void) {
    String payload = "[{\"id\":\"bitcoin\",\"symbol\":\"btc\",\"name\":\"Bitcoin\",\"current_price\":61234.56,\"price_change_percentage_24h\":2.45}]";
    float price = 0.0f;
    float change = 0.0f;
    String imageUrl = "";
    
    CoinGeckoProvider provider;
    bool success = provider.parsePrimary(payload, price, change, imageUrl);
    
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_FLOAT(61234.56f, price);
    TEST_ASSERT_EQUAL_FLOAT(2.45f, change);
}

void test_parse_coingecko_simple(void) {
    String payload = "{\"ergo\":{\"usd\":1.23,\"usd_24h_change\":-5.12}}";
    float price = 0.0f;
    float change = 0.0f;
    
    CoinGeckoProvider provider;
    bool success = provider.parseSimple(payload, "ergo", price, change);
    
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_FLOAT(1.23f, price);
    TEST_ASSERT_EQUAL_FLOAT(-5.12f, change);
}

void test_parse_binance(void) {
    String payload = "{\"symbol\":\"BTCUSDT\",\"lastPrice\":\"62000.00\",\"priceChangePercent\":\"1.5\"}";
    float price = 0.0f;
    float change = 0.0f;
    
    BinanceProvider provider;
    bool success = provider.parsePayload(payload, price, change);
    
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_FLOAT(62000.0f, price);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, change);
}

void test_parse_yahoo_finance(void) {
    String payload = "{\"chart\":{\"result\":[{\"meta\":{\"regularMarketPrice\":150.25,\"previousClose\":148.00}}]}}";
    float price = 0.0f;
    float change = 0.0f;
    
    YahooFinanceProvider provider;
    bool success = provider.parsePayload(payload, price, change);
    
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL_FLOAT(150.25f, price);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.52f, change);
}

void test_parse_openweathermap(void) {
    String payload = "{\"list\":["
                     "{\"main\":{\"temp\":22.5},\"weather\":[{\"main\":\"Clear\",\"icon\":\"01d\"}]},"
                     "{\"main\":{\"temp\":23.0},\"weather\":[{\"main\":\"Clouds\",\"icon\":\"02d\"}]},"
                     "{\"main\":{\"temp\":24.0},\"weather\":[{\"main\":\"Rain\",\"icon\":\"10d\"}]},"
                     "{\"main\":{\"temp\":25.0},\"weather\":[{\"main\":\"Snow\",\"icon\":\"13d\"}]},"
                     "{\"main\":{\"temp\":26.0},\"weather\":[{\"main\":\"Clear\",\"icon\":\"01d\"}]},"
                     "{\"main\":{\"temp\":27.0},\"weather\":[{\"main\":\"Clouds\",\"icon\":\"02d\"}]},"
                     "{\"main\":{\"temp\":28.0},\"weather\":[{\"main\":\"Rain\",\"icon\":\"10d\"}]},"
                     "{\"main\":{\"temp\":29.0},\"weather\":[{\"main\":\"Snow\",\"icon\":\"13d\"}]},"
                     "{\"main\":{\"temp\":30.0},\"weather\":[{\"main\":\"Clear\",\"icon\":\"01d\"}]}"
                     "]}";
    
    WeatherData forecasts[3];
    int numForecasts = 0;
    
    OpenWeatherMapProvider provider;
    bool success = provider.parsePayload(payload, forecasts, 3, numForecasts, "en", false, 0);
    
    TEST_ASSERT_TRUE(success);
    TEST_ASSERT_EQUAL(3, numForecasts);
    TEST_ASSERT_EQUAL_FLOAT(22.5f, forecasts[0].temp);
    TEST_ASSERT_EQUAL_STRING("Clear", forecasts[0].description.c_str());
    TEST_ASSERT_EQUAL_STRING("01d", forecasts[0].iconCode.c_str());
    TEST_ASSERT_EQUAL_STRING("TODAY", forecasts[0].label.c_str());
}

void test_parse_coingecko_malformed(void) {
    String badPayload = "{\"invalid_json\": true}";
    float price = 0.0f;
    float change = 0.0f;
    String imageUrl = "";

    CoinGeckoProvider provider;
    bool success = provider.parsePrimary(badPayload, price, change, imageUrl);
    TEST_ASSERT_FALSE(success);
}

void test_parse_binance_malformed(void) {
    String badPayload = "{\"symbol\":\"BTCUSDT\", \"error\":\"Rate limited\"}";
    float price = 0.0f;
    float change = 0.0f;

    BinanceProvider provider;
    bool success = provider.parsePayload(badPayload, price, change);
    TEST_ASSERT_FALSE(success);
}

void test_parse_yahoo_malformed(void) {
    String badPayload = "{\"chart\":{\"result\":[]}}";
    float price = 0.0f;
    float change = 0.0f;

    YahooFinanceProvider provider;
    bool success = provider.parsePayload(badPayload, price, change);
    TEST_ASSERT_FALSE(success);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_parse_coingecko_primary);
    RUN_TEST(test_parse_coingecko_simple);
    RUN_TEST(test_parse_coingecko_malformed);
    RUN_TEST(test_parse_binance);
    RUN_TEST(test_parse_binance_malformed);
    RUN_TEST(test_parse_yahoo_finance);
    RUN_TEST(test_parse_yahoo_malformed);
    RUN_TEST(test_parse_openweathermap);
    UNITY_END();
}

void loop() {
    delay(100);
}
