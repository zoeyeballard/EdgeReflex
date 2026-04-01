//*****************************************************************************
// sensor_task.h - Sensor acquisition task (IMU / ADC placeholder).
//*****************************************************************************

#ifndef __SENSOR_TASK_H__
#define __SENSOR_TASK_H__

#include <stdint.h>
#include "FreeRTOS.h"
#include "queue.h"

//*****************************************************************************
// Sample window sent from sensor task to inference task via xSensorQueue.
// 50 samples @ ~20 ms each = 1-second window. Adjust WINDOW_SIZE to taste.
//*****************************************************************************
#define SENSOR_AXES         6       // ax, ay, az, gx, gy, gz
#define WINDOW_SIZE         50      // samples per inference window

typedef struct
{
    int16_t  data[WINDOW_SIZE][SENSOR_AXES];  // raw ADC / IMU counts
    uint32_t timestamp_ms;                    // tick when window closed
} SensorWindow_t;

typedef struct
{
    uint32_t count;
    uint32_t last_cycles;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint32_t mean_cycles;
} SensorTaskTimingStats_t;

//*****************************************************************************
// Queue handle - defined in sensor_task.c, used by inference_task.c
//*****************************************************************************
extern QueueHandle_t g_xSensorQueue;

extern uint32_t SensorTaskInit(void);
void SensorTaskTimingGetStats(SensorTaskTimingStats_t *pStats);

#endif // __SENSOR_TASK_H__