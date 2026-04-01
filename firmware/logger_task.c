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
#include "sensor_task.h"
#include "uart_task.h"
#include "logger_task.h"
#include "priorities.h"

#define LOGGER_TASK_STACK       400
#define LOG_INTERVAL_RESULTS    10   // print summary every N inferences
#define LOG_CSV_ENABLE          1
#define LOG_HIST_ENABLE         1
#define LOG_HIST_INTERVAL       1000U
#define LOGGER_HIST_MAX_BINS    128U
#define TASK_CSV_ENABLE         1

#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DWT_SNAPSHOT()  (DWT_CYCCNT)

typedef struct
{
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} LoggerTimingState_t;

static LoggerTimingState_t g_sLoggerTiming = {0U, 0U, UINT32_MAX, 0U, 0ULL};

static void LoggerTimingRecord(uint32_t cycles)
{
    taskENTER_CRITICAL();
    g_sLoggerTiming.count++;
    g_sLoggerTiming.last = cycles;
    g_sLoggerTiming.sum += cycles;

    if (cycles < g_sLoggerTiming.min)
    {
        g_sLoggerTiming.min = cycles;
    }
    if (cycles > g_sLoggerTiming.max)
    {
        g_sLoggerTiming.max = cycles;
    }
    taskEXIT_CRITICAL();
}

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
    uint32_t t0;
    uint32_t t1;
    uint32_t bins_to_dump;
    uint32_t hist[LOGGER_HIST_MAX_BINS];
    InferenceWcetStats_t stats;
    SensorTaskTimingStats_t sensor_stats;
    UARTTaskTimingStats_t uart_stats;
    LoggerTaskTimingStats_t logger_stats;
    TickType_t t0_ms;

    (void)pvParameters;
    t0_ms = xTaskGetTickCount();

    #if LOG_CSV_ENABLE
    xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
    UARTprintf("WCET_HEADER,count,warmup_discarded,last,min,max,max_unpreempted,mean,p99,bound,overhead,bin_width,hist_overflow,preempted,unpreempted,uptime_ms\n");
    #if TASK_CSV_ENABLE
    UARTprintf("TASK_HEADER,count,sensor_last,sensor_min,sensor_max,sensor_mean,uart_last,uart_min,uart_max,uart_mean,logger_last,logger_min,logger_max,logger_mean,uptime_ms\n");
    #endif
    #if LOG_HIST_ENABLE
    UARTprintf("WCET_HIST_HEADER,count,bin_idx,bin_lo,bin_hi,bin_count\n");
    #endif
    xSemaphoreGive(g_pUARTSemaphore);
    #endif

    while (1)
    {
        // Block until inference task signals a result is ready
        xSemaphoreTake(g_xTimingSem, portMAX_DELAY);
        t0 = DWT_SNAPSHOT();

        HarClass_t cls = g_eLastClass;

        n++;

        if ((n % LOG_INTERVAL_RESULTS) == 0)
        {
            InferenceWcetGetStats(&stats);

            xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
            UARTprintf("[LOGGER] n=%u wd=%u last=%u min=%u max=%u max_np=%u mean=%u p99=%u bound=%u oh=%u pre=%u unpre=%u class=%s\n",
                       stats.count,
                       stats.warmup_discarded,
                       stats.last_cycles,
                       stats.min_cycles,
                       stats.max_cycles,
                       stats.max_unpreempted_cycles,
                       stats.mean_cycles,
                       stats.p99_cycles,
                       stats.approx_bound_cycles,
                       stats.dwt_overhead_cycles,
                       stats.preempted_count,
                       stats.unpreempted_count,
                       CLASS_NAMES[cls]);

            #if LOG_CSV_ENABLE
            UARTprintf("WCET_CSV,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                       stats.count,
                       stats.warmup_discarded,
                       stats.last_cycles,
                       stats.min_cycles,
                       stats.max_cycles,
                       stats.max_unpreempted_cycles,
                       stats.mean_cycles,
                       stats.p99_cycles,
                       stats.approx_bound_cycles,
                       stats.dwt_overhead_cycles,
                       stats.hist_bin_width_cycles,
                       stats.hist_overflow,
                       stats.preempted_count,
                       stats.unpreempted_count,
                       (uint32_t)(xTaskGetTickCount() - t0_ms));

            #if TASK_CSV_ENABLE
            SensorTaskTimingGetStats(&sensor_stats);
            UARTTaskTimingGetStats(&uart_stats);
            LoggerTaskTimingGetStats(&logger_stats);
            UARTprintf("TASK_CSV,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
                       stats.count,
                       sensor_stats.last_cycles,
                       sensor_stats.min_cycles,
                       sensor_stats.max_cycles,
                       sensor_stats.mean_cycles,
                       uart_stats.last_cycles,
                       uart_stats.min_cycles,
                       uart_stats.max_cycles,
                       uart_stats.mean_cycles,
                       logger_stats.last_cycles,
                       logger_stats.min_cycles,
                       logger_stats.max_cycles,
                       logger_stats.mean_cycles,
                       (uint32_t)(xTaskGetTickCount() - t0_ms));
            #endif

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

        t1 = DWT_SNAPSHOT();
        LoggerTimingRecord(t1 - t0);
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

void LoggerTaskTimingGetStats(LoggerTaskTimingStats_t *pStats)
{
    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    pStats->count = g_sLoggerTiming.count;
    pStats->last_cycles = g_sLoggerTiming.last;
    pStats->min_cycles = (g_sLoggerTiming.count == 0U) ? 0U : g_sLoggerTiming.min;
    pStats->max_cycles = g_sLoggerTiming.max;
    pStats->mean_cycles = (g_sLoggerTiming.count == 0U) ? 0U : (uint32_t)(g_sLoggerTiming.sum / g_sLoggerTiming.count);
    taskEXIT_CRITICAL();
}