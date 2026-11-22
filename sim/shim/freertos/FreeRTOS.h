// Simulador: FreeRTOS con hilos de C++.
#pragma once
#include <cstdint>
#include <mutex>

using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (TickType_t(ms))

struct SimSemaphore;
using SemaphoreHandle_t = SimSemaphore*;
struct SimQueue;
using QueueHandle_t = SimQueue*;
using TaskHandle_t = void*;
using TaskFunction_t = void (*)(void*);

SemaphoreHandle_t xSemaphoreCreateMutex();
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t s);

QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t ticks);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle, BaseType_t core);

struct portMUX_TYPE {
  std::mutex m;
};
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->m.lock()
#define portEXIT_CRITICAL(mux) (mux)->m.unlock()
