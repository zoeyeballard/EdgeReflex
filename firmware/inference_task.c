//*****************************************************************************
// inference_task.c - HAR inference task.
//
// Blocks on g_xSensorQueue waiting for a complete SensorWindow_t.
// Runs inference (stub: returns a cycling class label for now).
// Records DWT cycle count for the inference call.
// Posts g_xTimingSem so the logger can record the result.
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "sensor_task.h"
#include "inference_task.h"
#include "priorities.h"

//*****************************************************************************
// DWT (Data Watchpoint and Trace) cycle counter registers.
// ARM Cortex-M4 specific - gives exact CPU cycle counts at no runtime cost.
//*****************************************************************************
#define DWT_CTRL    (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DEM_CR      (*((volatile uint32_t *)0xE000EDFC))
#define DEM_CR_TRCENA   (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

static void DWT_Init(void)
{
    DEM_CR    |= DEM_CR_TRCENA;      // enable trace subsystem
    DWT_CYCCNT = 0;                  // reset counter
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA; // start counting
}

#define DWT_SNAPSHOT()  (DWT_CYCCNT)

//*****************************************************************************
// Shared results (written here, read by logger / UART task)
//*****************************************************************************
volatile HarClass_t g_eLastClass  = HAR_CLASS_UNKNOWN;
volatile uint32_t   g_uiLastCycles = 0;

SemaphoreHandle_t   g_xTimingSem;

#define INFERENCE_TASK_STACK  512   // words - inference needs more stack

extern SemaphoreHandle_t g_pUARTSemaphore;

static const char * const CLASS_NAMES[] = {
    "UNKNOWN", "WALKING", "RUNNING", "SITTING", "STANDING"
};

//*****************************************************************************
// run_inference_stub - replace with your real model call.
// Currently just cycles through class labels so you can see output flowing.
//*****************************************************************************
static HarClass_t run_inference_stub(const SensorWindow_t *pWindow)
{
    (void)pWindow;  // suppress unused warning until real model is wired in
    static uint32_t call_count = 0;
    return (HarClass_t)((call_count++ % (HAR_CLASS_COUNT - 1)) + 1);
}

//*****************************************************************************
// InferenceTask
//*****************************************************************************
static void InferenceTask(void *pvParameters)
{
    SensorWindow_t window;
    uint32_t t0, t1;

    while (1)
    {
        // Block indefinitely until sensor task posts a window
        xQueueReceive(g_xSensorQueue, &window, portMAX_DELAY);

        // --- Timed inference block ---
        t0 = DWT_SNAPSHOT();
        g_eLastClass = run_inference_stub(&window);
        t1 = DWT_SNAPSHOT();
        // --- End timed block ---

        g_uiLastCycles = t1 - t0;

        xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
        UARTprintf("[INFER] class=%s  cycles=%u  window_ts=%u ms\n",
                   CLASS_NAMES[g_eLastClass],
                   g_uiLastCycles,
                   window.timestamp_ms);
        xSemaphoreGive(g_pUARTSemaphore);

        // Signal logger that a new result + timing are ready
        xSemaphoreGive(g_xTimingSem);
    }
}

//*****************************************************************************
// InferenceTaskInit
//*****************************************************************************
uint32_t InferenceTaskInit(void)
{
    DWT_Init();

    g_xTimingSem = xSemaphoreCreateBinary();
    if (g_xTimingSem == NULL)
    {
        return 1;
    }

    if (xTaskCreate(InferenceTask, "INFER", INFERENCE_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + PRIORITY_INFERENCE_TASK, NULL) != pdTRUE)
    {
        return 1;
    }

    return 0;
}