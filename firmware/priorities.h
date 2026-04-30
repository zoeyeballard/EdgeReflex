//*****************************************************************************
// priorities.h - Task priorities for HAR firmware.
//
// Higher number = higher priority in FreeRTOS.
// Sensor acq runs highest (time-critical), logger runs lowest (background).
//*****************************************************************************

#ifndef __PRIORITIES_H__
#define __PRIORITIES_H__

#define PRIORITY_FEEDBACK_TASK   1   // Lowest  - closed-loop feedback stub
#define PRIORITY_LOGGER_TASK     2   // Low     - idle-time profiling
#define PRIORITY_INFERENCE_TASK  3   // Medium  - inference + smoothing stage
#define PRIORITY_SENSOR_TASK     4   // High    - runs when window is ready
#define PRIORITY_UART_TASK       5   // Highest - must not miss samples

/* Backward compatibility for legacy demo tasks still in the build. */
#define PRIORITY_LED_TASK        PRIORITY_UART_TASK
#define PRIORITY_SWITCH_TASK     PRIORITY_INFERENCE_TASK

#endif // __PRIORITIES_H__