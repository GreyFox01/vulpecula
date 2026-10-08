#pragma once
#include <stdint.h>
#include <stdlib.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define IRAM_ATTR
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
static inline QueueHandle_t xQueueCreate(int,int){return (QueueHandle_t)1;}
static inline int xQueueSend(QueueHandle_t,const void*,int){return pdTRUE;}
static inline int xQueueReceive(QueueHandle_t,void*,int){return 0;}
static inline void xQueueReset(QueueHandle_t){}
static inline void vTaskDelay(int){}
static inline int xTaskCreate(void(*)(void*),const char*,int,void*,int,TaskHandle_t*){return pdTRUE;}
typedef void* SemaphoreHandle_t;
#define portMAX_DELAY 0xFFFFFFFF
static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){return (SemaphoreHandle_t)1;}
static inline int xSemaphoreTakeRecursive(SemaphoreHandle_t,unsigned){return 1;}
static inline int xSemaphoreGiveRecursive(SemaphoreHandle_t){return 1;}
