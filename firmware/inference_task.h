//*****************************************************************************
// inference_task.h - HAR inference task.
//*****************************************************************************

#ifndef __INFERENCE_TASK_H__
#define __INFERENCE_TASK_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "agent_state.h"

//*****************************************************************************
// Semaphore posted by inference task each time a result is ready.
// Logger task waits on this to record timing data.
//*****************************************************************************
extern SemaphoreHandle_t g_xTimingSem;

//*****************************************************************************
// Last inference result - written by inference task, read by UART/logger.
// In a real system, protect with a mutex if reads aren't atomic.
//*****************************************************************************
typedef enum
{
    HAR_CLASS_UNKNOWN = 0,
    HAR_CLASS_WALKING,
    HAR_CLASS_RUNNING,
    HAR_CLASS_SITTING,
    HAR_CLASS_STANDING,
    HAR_CLASS_COUNT
} HarClass_t;

extern volatile HarClass_t  g_eLastClass;
extern volatile uint32_t    g_uiLastCycles;   // DWT cycles for last inference
extern volatile uint8_t     g_uiLastConfidence;
extern volatile AgentState_t g_sAgentState;

typedef struct
{
    uint32_t count;
    uint32_t warmup_discarded;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t max_unpreempted_cycles;
    uint32_t mean_cycles;
    uint32_t p99_cycles;
    uint32_t approx_bound_cycles;
    uint32_t dwt_overhead_cycles;
    uint32_t hist_bin_width_cycles;
    uint32_t hist_bins;
    uint32_t hist_overflow;
    uint32_t preempted_count;
    uint32_t unpreempted_count;
} InferenceWcetStats_t;

void InferenceWcetGetStats(InferenceWcetStats_t *pStats);
uint32_t InferenceWcetCopyHistogram(uint32_t *pBins, uint32_t maxBins);

extern uint32_t InferenceTaskInit(void);

#endif // __INFERENCE_TASK_H__