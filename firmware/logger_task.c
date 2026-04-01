//*****************************************************************************
// logger_task.c - Cycle counter / timing logger.
//
// Sits at the lowest application priority (just above the FreeRTOS idle task).
// Waits on g_xTimingSem, which the inference task posts after every result.
// Accumulates min/max/average inference cycle counts and prints a summary
// every LOG_INTERVAL_RESULTS inferences.
//
// This is where the profiling data you'll use in the HAR pipeline lives.
// Output format is parseable by the Python side:
//   [LOGGER] n=10  last=1234  min=1100  max=1400  avg=1220  class=WALKING
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "inference_task.h"
#include "logger_task.h"
#include "priorities.h"

#define LOGGER_TASK_STACK       200
#define LOG_INTERVAL_RESULTS    10   // print summary every N inferences

extern SemaphoreHandle_t g_pUARTSemaphore;

static const char * const CLASS_NAMES[] = {
    "UNKNOWN", "WALKING", "RUNNING", "SITTING", "STANDING"
};

//*****************************************************************************
// LoggerTask
//*****************************************************************************
static void LoggerTask(void *pvParameters)
{
    uint32_t n        = 0;
    uint32_t cyc_min  = UINT32_MAX;
    uint32_t cyc_max  = 0;
    uint64_t cyc_sum  = 0;

    while (1)
    {
        // Block until inference task signals a result is ready
        xSemaphoreTake(g_xTimingSem, portMAX_DELAY);

        uint32_t cycles = g_uiLastCycles;
        HarClass_t cls  = g_eLastClass;

        n++;
        cyc_sum += cycles;
        if (cycles < cyc_min) cyc_min = cycles;
        if (cycles > cyc_max) cyc_max = cycles;

        if ((n % LOG_INTERVAL_RESULTS) == 0)
        {
            uint32_t avg = (uint32_t)(cyc_sum / n);

            xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
            UARTprintf("[LOGGER] n=%u  last=%u  min=%u  max=%u  avg=%u  class=%s\n",
                       n, cycles, cyc_min, cyc_max, avg, CLASS_NAMES[cls]);
            xSemaphoreGive(g_pUARTSemaphore);
        }
    }
}

//*****************************************************************************
// LoggerTaskInit
//*****************************************************************************
uint32_t LoggerTaskInit(void)
{
    if (xTaskCreate(LoggerTask, "LOGGER", LOGGER_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + PRIORITY_LOGGER_TASK, NULL) != pdTRUE)
    {
        return 1;
    }
    return 0;
}