//*****************************************************************************
// smoothing.c - 3-step EMA smoothing for class confidence.
//*****************************************************************************

#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"

#include "agent_state.h"
#include "inference_task.h"
#include "smoothing.h"

#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DWT_SNAPSHOT()  (DWT_CYCCNT)

#define SMOOTHING_DENOM              3U
#define SMOOTHING_NUMERATOR_PREV     2U
#define SMOOTHING_WORK_ITERS         50000U

typedef struct
{
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} SmoothingTimingState_t;

static SmoothingTimingState_t g_sSmoothingTiming = {0U, 0U, UINT32_MAX, 0U, 0ULL};
static int16_t g_iEmaState[NUM_CLASSES] = {0, 0, 0, 0};
static volatile uint32_t g_uiSmoothingWorkSink = 0U;

static void SmoothingTimingRecord(uint32_t cycles)
{
    taskENTER_CRITICAL();
    g_sSmoothingTiming.count++;
    g_sSmoothingTiming.last = cycles;
    g_sSmoothingTiming.sum += cycles;

    if (cycles < g_sSmoothingTiming.min)
    {
        g_sSmoothingTiming.min = cycles;
    }
    if (cycles > g_sSmoothingTiming.max)
    {
        g_sSmoothingTiming.max = cycles;
    }
    taskEXIT_CRITICAL();
}

static void SmoothingBusyWork(uint32_t seed)
{
    uint32_t i;
    uint32_t acc = seed;

    for (i = 0U; i < SMOOTHING_WORK_ITERS; i++)
    {
        acc ^= ((acc << 5) + (acc >> 2) + 0x9E3779B9UL + i);
    }

    g_uiSmoothingWorkSink = acc;
}

void ConfidenceSmoothingStep(HarClass_t cls, uint32_t timestamp_ms)
{
    uint32_t i;
    uint32_t t0;
    uint32_t t1;
    uint32_t class_idx = 0U;
    uint32_t max_val = 0U;
    int8_t raw_probs[NUM_CLASSES] = {0, 0, 0, 0};

    if ((cls >= HAR_CLASS_WALKING) && (cls <= HAR_CLASS_STANDING))
    {
        class_idx = (uint32_t)(cls - HAR_CLASS_WALKING);
        raw_probs[class_idx] = 100;
    }

    t0 = DWT_SNAPSHOT();

    for (i = 0U; i < NUM_CLASSES; i++)
    {
        int16_t prev = g_iEmaState[i];
        int16_t cur = (int16_t)raw_probs[i];
        int16_t ema = (int16_t)((SMOOTHING_NUMERATOR_PREV * prev + cur + 1) / SMOOTHING_DENOM);

        g_iEmaState[i] = ema;
        g_sAgentState.prev_probs[i] = (int8_t)ema;

        if ((uint32_t)ema > max_val)
        {
            max_val = (uint32_t)ema;
        }
    }

    g_sAgentState.confidence = (uint8_t)max_val;
    g_sAgentState.last_update_ms = timestamp_ms;
    g_uiLastConfidence = g_sAgentState.confidence;

    SmoothingBusyWork(max_val + (uint32_t)cls);

    t1 = DWT_SNAPSHOT();
    SmoothingTimingRecord(t1 - t0);
}

void ConfidenceSmoothingTimingGetStats(ConfidenceSmoothingTimingStats_t *pStats)
{
    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    pStats->count = g_sSmoothingTiming.count;
    pStats->last_cycles = g_sSmoothingTiming.last;
    pStats->min_cycles = (g_sSmoothingTiming.count == 0U) ? 0U : g_sSmoothingTiming.min;
    pStats->max_cycles = g_sSmoothingTiming.max;
    pStats->mean_cycles = (g_sSmoothingTiming.count == 0U) ? 0U : (uint32_t)(g_sSmoothingTiming.sum / g_sSmoothingTiming.count);
    taskEXIT_CRITICAL();
}
