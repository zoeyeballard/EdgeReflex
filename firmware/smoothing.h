//*****************************************************************************
// smoothing.h - Confidence smoothing stage for agentic temporal reasoning.
//*****************************************************************************

#ifndef __SMOOTHING_H__
#define __SMOOTHING_H__

#include <stdint.h>
#include "inference_task.h"

typedef struct
{
    uint32_t count;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t mean_cycles;
} ConfidenceSmoothingTimingStats_t;

void ConfidenceSmoothingStep(HarClass_t cls, uint32_t timestamp_ms);
void ConfidenceSmoothingTimingGetStats(ConfidenceSmoothingTimingStats_t *pStats);

#endif // __SMOOTHING_H__
