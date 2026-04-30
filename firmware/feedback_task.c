//*****************************************************************************
// feedback_task.c - Agentic closed-loop feedback task.
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"
#include "FreeRTOS.h"
#include "task.h"

#include "agent_state.h"
#include "inference_task.h"
#include "feedback_task.h"
#include "priorities.h"

#define FEEDBACK_TASK_STACK        512
#define FEEDBACK_PERIOD_MS         100U
#define FEEDBACK_CONFIRM_STEPS     3U
#define FEEDBACK_MIN_CONFIDENCE    55U
#define FEEDBACK_WORK_ITERS        340000U

#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DWT_SNAPSHOT()  (DWT_CYCCNT)

typedef struct
{
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} FeedbackTimingState_t;

static FeedbackTimingState_t g_sFeedbackTiming = {0U, 0U, UINT32_MAX, 0U, 0ULL};
static volatile uint32_t g_uiFeedbackWorkSink = 0U;

static void FeedbackTimingRecord(uint32_t cycles)
{
    taskENTER_CRITICAL();
    g_sFeedbackTiming.count++;
    g_sFeedbackTiming.last = cycles;
    g_sFeedbackTiming.sum += cycles;

    if (cycles < g_sFeedbackTiming.min)
    {
        g_sFeedbackTiming.min = cycles;
    }
    if (cycles > g_sFeedbackTiming.max)
    {
        g_sFeedbackTiming.max = cycles;
    }
    taskEXIT_CRITICAL();
}

static void FeedbackBusyCompute(HarClass_t cls, uint8_t confidence)
{
    uint32_t i;
    uint32_t acc = ((uint32_t)cls << 16) | confidence;

    for (i = 0U; i < FEEDBACK_WORK_ITERS; i++)
    {
        acc ^= ((acc << 7) + (acc >> 3) + 0x7F4A7C15UL + i);
    }

    g_uiFeedbackWorkSink = acc;
}

static void FeedbackLedInit(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF))
    {
    }

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0U);
}

static void FeedbackTask(void *pvParameters)
{
    TickType_t xLastWakeTime;
    HarClass_t candidate = HAR_CLASS_UNKNOWN;
    uint8_t confirm_count = 0U;
    uint32_t t0;
    uint32_t t1;

    (void)pvParameters;
    xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        HarClass_t cls;
        uint8_t confidence;
        bool trigger;

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(FEEDBACK_PERIOD_MS));
        t0 = DWT_SNAPSHOT();

        cls = g_eLastClass;
        confidence = g_uiLastConfidence;

        if ((cls >= HAR_CLASS_WALKING) && (cls <= HAR_CLASS_STANDING) && (confidence >= FEEDBACK_MIN_CONFIDENCE))
        {
            if (cls == candidate)
            {
                if (confirm_count < 255U)
                {
                    confirm_count++;
                }
            }
            else
            {
                candidate = cls;
                confirm_count = 1U;
            }
        }
        else
        {
            candidate = HAR_CLASS_UNKNOWN;
            confirm_count = 0U;
        }

        trigger = (confirm_count >= FEEDBACK_CONFIRM_STEPS);

        FeedbackBusyCompute(candidate, confidence);

        if (trigger)
        {
            GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);
        }
        else
        {
            GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0U);
        }

        t1 = DWT_SNAPSHOT();
        FeedbackTimingRecord(t1 - t0);
    }
}

uint32_t FeedbackTaskInit(void)
{
    FeedbackLedInit();

    if (xTaskCreate(FeedbackTask, "FBACK", FEEDBACK_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + PRIORITY_FEEDBACK_TASK, NULL) != pdTRUE)
    {
        return 1;
    }

    return 0;
}

void FeedbackTaskTimingGetStats(FeedbackTaskTimingStats_t *pStats)
{
    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    pStats->count = g_sFeedbackTiming.count;
    pStats->last_cycles = g_sFeedbackTiming.last;
    pStats->min_cycles = (g_sFeedbackTiming.count == 0U) ? 0U : g_sFeedbackTiming.min;
    pStats->max_cycles = g_sFeedbackTiming.max;
    pStats->mean_cycles = (g_sFeedbackTiming.count == 0U) ? 0U : (uint32_t)(g_sFeedbackTiming.sum / g_sFeedbackTiming.count);
    taskEXIT_CRITICAL();
}
