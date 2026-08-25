#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t sdMutex;
#include <mutex>
extern std::mutex configMutex;
