//*****************************************************************************
// logger_task.h - Cycle counter / timing logger task.
//*****************************************************************************

#ifndef __LOGGER_TASK_H__
#define __LOGGER_TASK_H__

#include <stdint.h>

typedef struct
{
	uint32_t count;
	uint32_t last_cycles;
	uint32_t min_cycles;
	uint32_t max_cycles;
	uint32_t mean_cycles;
} LoggerTaskTimingStats_t;

extern uint32_t LoggerTaskInit(void);
void LoggerTaskTimingGetStats(LoggerTaskTimingStats_t *pStats);

#endif // __LOGGER_TASK_H__