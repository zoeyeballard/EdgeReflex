//*****************************************************************************
// feedback_task.h - Agentic feedback task with hysteresis and timing stats.
//*****************************************************************************

#ifndef __FEEDBACK_TASK_H__
#define __FEEDBACK_TASK_H__

#include <stdint.h>

typedef struct
{
    uint32_t count;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t mean_cycles;
} FeedbackTaskTimingStats_t;

uint32_t FeedbackTaskInit(void);
void FeedbackTaskTimingGetStats(FeedbackTaskTimingStats_t *pStats);

#endif // __FEEDBACK_TASK_H__
