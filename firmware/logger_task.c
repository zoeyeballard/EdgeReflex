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

#define LOGGER_TASK_STACK       400
#define LOG_INTERVAL_RESULTS    10   // print summary every N inferences
#define LOG_CSV_ENABLE          1
#define LOG_HIST_ENABLE         1
#define LOG_HIST_INTERVAL       1000U
#define LOGGER_HIST_MAX_BINS    128U

extern SemaphoreHandle_t g_pUARTSemaphore;

static const char * const CLASS_NAMES[] = {
    "UNKNOWN", "WALKING", "RUNNING", "SITTING", "STANDING"
};

//*****************************************************************************
// LoggerTask
//*****************************************************************************
static void LoggerTask(void *pvParameters)
{
    uint32_t n = 0;
    uint32_t i;
    uint32_t bins_to_dump;
    uint32_t hist[LOGGER_HIST_MAX_BINS];
    InferenceWcetStats_t stats;
    TickType_t t0_ms;

    (void)pvParameters;
    t0_ms = xTaskGetTickCount();

    #if LOG_CSV_ENABLE
    xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
    UARTprintf("WCET_HEADER,count,last,min,max,mean,p99,bound,overhead,bin_width,hist_overflow,uptime_ms\n");
    #if LOG_HIST_ENABLE
    UARTprintf("WCET_HIST_HEADER,count,bin_idx,bin_lo,bin_hi,bin_count\n");
    #endif
    xSemaphoreGive(g_pUARTSemaphore);
    #endif

    while (1)
    {
        // Block until inference task signals a result is ready
        xSemaphoreTake(g_xTimingSem, portMAX_DELAY);

        HarClass_t cls = g_eLastClass;

        n++;

        if ((n % LOG_INTERVAL_RESULTS) == 0)
        {
            InferenceWcetGetStats(&stats);

            xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
            UARTprintf("[LOGGER] n=%u last=%u min=%u max=%u mean=%u p99=%u bound=%u oh=%u class=%s\n",
                       stats.count,
                       stats.last_cycles,
                       stats.min_cycles,
                       stats.max_cycles,
                       stats.mean_cycles,
                       stats.p99_cycles,
                       stats.approx_bound_cycles,
                       stats.dwt_overhead_cycles,
                       CLASS_NAMES[cls]);

            #if LOG_CSV_ENABLE
            UARTprintf("WCET_CSV,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                       stats.count,
                       stats.last_cycles,
                       stats.min_cycles,
                       stats.max_cycles,
                       stats.mean_cycles,
                       stats.p99_cycles,
                       stats.approx_bound_cycles,
                       stats.dwt_overhead_cycles,
                       stats.hist_bin_width_cycles,
                       stats.hist_overflow,
                       (uint32_t)(xTaskGetTickCount() - t0_ms));

            #if LOG_HIST_ENABLE
            if ((stats.count % LOG_HIST_INTERVAL) == 0U)
            {
                bins_to_dump = InferenceWcetCopyHistogram(hist, LOGGER_HIST_MAX_BINS);
                for (i = 0U; i < bins_to_dump; i++)
                {
                    UARTprintf("WCET_HIST,%u,%u,%u,%u,%u\n",
                               stats.count,
                               i,
                               i * stats.hist_bin_width_cycles,
                               ((i + 1U) * stats.hist_bin_width_cycles) - 1U,
                               hist[i]);
                }
            }
            #endif
            #endif

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