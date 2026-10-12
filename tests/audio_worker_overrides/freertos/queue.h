#pragma once
#include "FreeRTOS.h"
#include <deque>
#include <vector>
struct TestAudioQueue {
    size_t capacity;
    size_t item_size;
    std::deque<std::vector<uint8_t>> items;
};
using QueueHandle_t = TestAudioQueue*;
QueueHandle_t xQueueCreate(size_t capacity, size_t item_size);
BaseType_t xQueueReceive(QueueHandle_t queue, void* item, TickType_t ticks);
BaseType_t xQueueSend(QueueHandle_t queue, const void* item, TickType_t ticks);
void vQueueDelete(QueueHandle_t queue);