//*****************************************************************************
// inference_task.h - HAR inference task.
//*****************************************************************************

#ifndef __INFERENCE_TASK_H__
#define __INFERENCE_TASK_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "semphr.h"

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

extern uint32_t InferenceTaskInit(void);

#endif // __INFERENCE_TASK_H__