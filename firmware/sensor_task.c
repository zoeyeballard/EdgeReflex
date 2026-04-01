//*****************************************************************************
// sensor_task.c - Sensor acquisition task.
//
// Currently a stub: fills windows with a synthetic counter so the rest of
// the pipeline can be tested before the physical IMU arrives.
// Replace the "--- STUB ---" block with real SPI/I2C reads later.
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "driverlib/sysctl.h"
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "sensor_task.h"
#include "priorities.h"

#define SENSOR_TASK_STACK   256         // words
#define SAMPLE_PERIOD_MS    20          // 50 Hz  (change to 10 for 100 Hz)
#define SENSOR_LOG_WINDOWS  0           // keep 0 for WCET campaigns

#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DWT_SNAPSHOT()  (DWT_CYCCNT)

typedef struct
{
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} SensorTimingState_t;

static SensorTimingState_t g_sSensorTiming = {0U, 0U, UINT32_MAX, 0U, 0ULL};

static void SensorTimingRecord(uint32_t cycles)
{
    taskENTER_CRITICAL();
    g_sSensorTiming.count++;
    g_sSensorTiming.last = cycles;
    g_sSensorTiming.sum += cycles;

    if (cycles < g_sSensorTiming.min)
    {
        g_sSensorTiming.min = cycles;
    }
    if (cycles > g_sSensorTiming.max)
    {
        g_sSensorTiming.max = cycles;
    }
    taskEXIT_CRITICAL();
}

QueueHandle_t g_xSensorQueue;
#define SENSOR_QUEUE_DEPTH  4           // buffer up to 4 unprocessed windows

extern SemaphoreHandle_t g_pUARTSemaphore;

//*****************************************************************************
// SensorTask - runs at 50 Hz, accumulates WINDOW_SIZE samples, then posts
// a complete SensorWindow_t to g_xSensorQueue for the inference task.
//*****************************************************************************
static void SensorTask(void *pvParameters)
{
    SensorWindow_t  window;
    uint32_t        sample_idx = 0;
    uint32_t        window_count = 0;
    uint32_t        t0;
    uint32_t        t1;
    TickType_t      xLastWake = xTaskGetTickCount();

    while (1)
    {
        t0 = DWT_SNAPSHOT();

        // --- STUB: replace with real IMU read over SPI/I2C ---
        window.data[sample_idx][0] = (int16_t)(sample_idx * 10);   // ax
        window.data[sample_idx][1] = (int16_t)(sample_idx * 11);   // ay
        window.data[sample_idx][2] = (int16_t)(sample_idx * 12);   // az
        window.data[sample_idx][3] = (int16_t)(sample_idx * 13);   // gx
        window.data[sample_idx][4] = (int16_t)(sample_idx * 14);   // gy
        window.data[sample_idx][5] = (int16_t)(sample_idx * 15);   // gz
        // --- END STUB ---

        sample_idx++;

        if (sample_idx >= WINDOW_SIZE)
        {
            sample_idx = 0;
            window.timestamp_ms = xTaskGetTickCount();

            // Post to inference queue; drop window if inference is falling behind
            if (xQueueSend(g_xSensorQueue, &window, 0) != pdPASS)
            {
                xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
                UARTprintf("[SENSOR] WARNING: queue full, window %u dropped\n",
                           window_count);
                xSemaphoreGive(g_pUARTSemaphore);
            }
            else
            {
                #if SENSOR_LOG_WINDOWS
                xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
                UARTprintf("[SENSOR] window %u posted @ %u ms\n",
                           window_count, window.timestamp_ms);
                xSemaphoreGive(g_pUARTSemaphore);
                #endif
            }
            window_count++;
        }

        t1 = DWT_SNAPSHOT();
        SensorTimingRecord(t1 - t0);

        vTaskDelayUntil(&xLastWake, pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

//*****************************************************************************
// SensorTaskInit - called from main() before vTaskStartScheduler()
//*****************************************************************************
uint32_t SensorTaskInit(void)
{
    g_xSensorQueue = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(SensorWindow_t));
    if (g_xSensorQueue == NULL)
    {
        return 1;
    }

    if (xTaskCreate(SensorTask, "SENSOR", SENSOR_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + PRIORITY_SENSOR_TASK, NULL) != pdTRUE)
    {
        return 1;
    }

    return 0;
}

void SensorTaskTimingGetStats(SensorTaskTimingStats_t *pStats)
{
    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    pStats->count = g_sSensorTiming.count;
    pStats->last_cycles = g_sSensorTiming.last;
    pStats->min_cycles = (g_sSensorTiming.count == 0U) ? 0U : g_sSensorTiming.min;
    pStats->max_cycles = g_sSensorTiming.max;
    pStats->mean_cycles = (g_sSensorTiming.count == 0U) ? 0U : (uint32_t)(g_sSensorTiming.sum / g_sSensorTiming.count);
    taskEXIT_CRITICAL();
}