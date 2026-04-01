//*****************************************************************************
// uart_task.h - UART comms task: TX log drain + RX replay injection.
//*****************************************************************************

#ifndef __UART_TASK_H__
#define __UART_TASK_H__

#include <stdint.h>

typedef struct
{
	uint32_t count;
	uint32_t last_cycles;
	uint32_t min_cycles;
	uint32_t max_cycles;
	uint32_t mean_cycles;
} UARTTaskTimingStats_t;

extern uint32_t UARTTaskInit(void);
void UARTTaskTimingGetStats(UARTTaskTimingStats_t *pStats);

#endif // __UART_TASK_H__