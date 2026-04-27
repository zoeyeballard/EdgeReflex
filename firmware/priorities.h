//*****************************************************************************
// priorities.h - Task priorities for HAR firmware.
//
// Higher number = higher priority in FreeRTOS.
// Sensor acq runs highest (time-critical), logger runs lowest (background).
//*****************************************************************************

#ifndef __PRIORITIES_H__
#define __PRIORITIES_H__

#define PRIORITY_LOGGER_TASK     1   // Lowest  - idle-time profiling
#define PRIORITY_UART_TASK       4   // Highest - must not miss samples
#define PRIORITY_INFERENCE_TASK  2   // Low     - TX log output + RX replay
#define PRIORITY_SENSOR_TASK     3   // Medium  - runs when window is ready

/* Backward compatibility for legacy demo tasks still in the build. */
#define PRIORITY_LED_TASK        PRIORITY_UART_TASK
#define PRIORITY_SWITCH_TASK     PRIORITY_INFERENCE_TASK

#endif // __PRIORITIES_H__