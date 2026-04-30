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
#include "agent_state.h"
#include "inference_task.h"
#include "priorities.h"
#include "har_model.h"
#include "smoothing.h"

#define INFERENCE_USE_HAR_MODEL      1
#define INFERENCE_LOG_EACH_WINDOW    0
#define INFERENCE_PERIOD_MS         100U
#define WCET_WARMUP_SAMPLES          8U
#define WCET_RING_SIZE               256U
#define WCET_HIST_BINS               64U
#define WCET_HIST_BIN_SHIFT          15U
#define WCET_HIST_BIN_WIDTH          (1UL << WCET_HIST_BIN_SHIFT)

//*****************************************************************************
// DWT (Data Watchpoint and Trace) cycle counter registers.
// ARM Cortex-M4 specific - gives exact CPU cycle counts at no runtime cost.
//*****************************************************************************
#define DWT_CTRL    (*((volatile uint32_t *)0xE0001000))
#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DEM_CR      (*((volatile uint32_t *)0xE000EDFC))
#define DEM_CR_TRCENA   (1UL << 24)
#define DWT_CTRL_CYCCNTENA (1UL << 0)

typedef struct
{
    uint32_t ring[WCET_RING_SIZE];
    uint32_t ring_head;
    uint32_t hist[WCET_HIST_BINS];
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint32_t max_unpreempted;
    uint32_t overhead;
    uint32_t hist_overflow;
    uint32_t warmup_discarded;
    uint32_t preempted_count;
    uint32_t unpreempted_count;
    uint64_t sum;
} InferenceWcetState_t;

static InferenceWcetState_t g_sWcet = {
    {0}, 0U, {0}, 0U, 0U, UINT32_MAX, 0U, 0U, 0U, 0U, 0U, 0U, 0ULL
};

static void DWT_Init(void)
{
    DEM_CR    |= DEM_CR_TRCENA;      // enable trace subsystem
    DWT_CYCCNT = 0;                  // reset counter
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA; // start counting
}

#define DWT_SNAPSHOT()  (DWT_CYCCNT)

static uint32_t DWT_CalibrateOverhead(void)
{
    uint32_t i;
    uint32_t t0;
    uint32_t t1;
    uint32_t d;
    uint32_t min_d = UINT32_MAX;

    for (i = 0; i < 128U; i++)
    {
        t0 = DWT_SNAPSHOT();
        t1 = DWT_SNAPSHOT();
        d = t1 - t0;
        if (d < min_d)
        {
            min_d = d;
        }
    }
    return min_d;
}

static uint32_t WcetSubtractOverhead(uint32_t raw)
{
    if (raw > g_sWcet.overhead)
    {
        return raw - g_sWcet.overhead;
    }
    return 0U;
}

static void WcetRecordSample(uint32_t cycles, bool preempted)
{
    uint32_t bin;

    taskENTER_CRITICAL();

    g_sWcet.last = cycles;
    g_sWcet.count++;
    g_sWcet.sum += cycles;

    if (cycles < g_sWcet.min)
    {
        g_sWcet.min = cycles;
    }
    if (cycles > g_sWcet.max)
    {
        g_sWcet.max = cycles;
    }

    if (preempted)
    {
        g_sWcet.preempted_count++;
    }
    else
    {
        g_sWcet.unpreempted_count++;
        if (cycles > g_sWcet.max_unpreempted)
        {
            g_sWcet.max_unpreempted = cycles;
        }
    }

    g_sWcet.ring[g_sWcet.ring_head] = cycles;
    g_sWcet.ring_head = (g_sWcet.ring_head + 1U) % WCET_RING_SIZE;

    bin = (cycles >> WCET_HIST_BIN_SHIFT);
    if (bin < WCET_HIST_BINS)
    {
        g_sWcet.hist[bin]++;
    }
    else
    {
        g_sWcet.hist_overflow++;
    }

    taskEXIT_CRITICAL();
}

//*****************************************************************************
// Shared results (written here, read by logger / UART task)
//*****************************************************************************
volatile HarClass_t g_eLastClass  = HAR_CLASS_UNKNOWN;
volatile uint32_t   g_uiLastCycles = 0;
volatile uint8_t    g_uiLastConfidence = 0U;

SemaphoreHandle_t   g_xTimingSem;

#define INFERENCE_TASK_STACK  1024

extern SemaphoreHandle_t g_pUARTSemaphore;

static const char * const CLASS_NAMES[] = {
    "UNKNOWN", "WALKING", "RUNNING", "SITTING", "STANDING"
};

volatile AgentState_t g_sAgentState = {
    {0, 0, 0, 0},
    0U,
    0UL,
    0UL
};

/*
 * Keep model input in static storage so inference timing does not consume
 * an additional HAR_INPUT_DIM floats on task stack.
 */
static float g_fHarInput[HAR_INPUT_DIM];

static void BuildHarInput(const SensorWindow_t *pWindow)
{
    uint32_t i;
    uint32_t j;
    uint32_t k = 0U;

    for (i = 0U; i < WINDOW_SIZE && k < HAR_INPUT_DIM; i++)
    {
        for (j = 0U; j < SENSOR_AXES && k < HAR_INPUT_DIM; j++)
        {
            g_fHarInput[k++] = (float)pWindow->data[i][j];
        }
    }

    for (i = 0U; i < NUM_CLASSES && k < HAR_INPUT_DIM; i++)
    {
        g_fHarInput[k++] = (float)g_sAgentState.prev_probs[i];
    }

    while (k < HAR_INPUT_DIM)
    {
        g_fHarInput[k++] = 0.0f;
    }
}

static void AgentStateUpdate(HarClass_t cls, uint32_t timestamp_ms)
{
    uint32_t i;
    uint32_t class_idx = 0U;
    uint8_t confidence = 0U;

    for (i = 0U; i < NUM_CLASSES; i++)
    {
        g_sAgentState.prev_probs[i] = 0;
    }

    if ((cls >= HAR_CLASS_WALKING) && (cls <= HAR_CLASS_STANDING))
    {
        class_idx = (uint32_t)(cls - HAR_CLASS_WALKING);
        g_sAgentState.prev_probs[class_idx] = 100;
        confidence = 100U;
    }

    g_sAgentState.confidence = confidence;
    g_sAgentState.step++;
    g_sAgentState.last_update_ms = timestamp_ms;
    g_uiLastConfidence = confidence;
}

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

static HarClass_t RunHarInference(const SensorWindow_t *pWindow)
{
    int label;

    BuildHarInput(pWindow);

    label = har_infer(g_fHarInput);

    if (label == 0) return HAR_CLASS_WALKING;
    if (label == 1) return HAR_CLASS_RUNNING;
    if (label == 2) return HAR_CLASS_SITTING;
    if (label == 3) return HAR_CLASS_STANDING;
    return HAR_CLASS_UNKNOWN;
}

//*****************************************************************************
// InferenceTask
//*****************************************************************************
static void InferenceTask(void *pvParameters)
{
    SensorWindow_t window;
    TickType_t tick_before;
    TickType_t tick_after;
    uint32_t t0;
    uint32_t t1;
    uint32_t raw_cycles;
    uint32_t net_cycles;
    TickType_t  xLastWakeTime;
    bool preempted;

    (void)pvParameters;

    xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(INFERENCE_PERIOD_MS));

        // Take the most recent window if available, skip if none
        if (xQueueReceive(g_xSensorQueue, &window, 0) != pdTRUE)
        {
            continue;
        }

        // --- Timed inference block ---
        tick_before = xTaskGetTickCount();
        t0 = DWT_SNAPSHOT();
        #if INFERENCE_USE_HAR_MODEL
        g_eLastClass = RunHarInference(&window);
        #else
        g_eLastClass = run_inference_stub(&window);
        #endif
        AgentStateUpdate(g_eLastClass, window.timestamp_ms);
        ConfidenceSmoothingStep(g_eLastClass, window.timestamp_ms);
        t1 = DWT_SNAPSHOT();
        tick_after = xTaskGetTickCount();
        // --- End timed block ---

        raw_cycles = t1 - t0;
        net_cycles = WcetSubtractOverhead(raw_cycles);
        g_uiLastCycles = net_cycles;
        preempted = ((tick_after - tick_before) != 0U);

        if (g_sWcet.warmup_discarded < WCET_WARMUP_SAMPLES)
        {
            taskENTER_CRITICAL();
            g_sWcet.warmup_discarded++;
            taskEXIT_CRITICAL();
        }
        else
        {
            WcetRecordSample(net_cycles, preempted);
        }

        #if INFERENCE_LOG_EACH_WINDOW
        xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
        UARTprintf("[INFER] class=%s  cycles=%u  window_ts=%u ms\n",
               CLASS_NAMES[g_eLastClass],
               g_uiLastCycles,
               window.timestamp_ms);
        xSemaphoreGive(g_pUARTSemaphore);
        #endif

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
    g_sWcet.overhead = DWT_CalibrateOverhead();

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

void InferenceWcetGetStats(InferenceWcetStats_t *pStats)
{
    uint32_t i;
    uint32_t cumulative;
    uint32_t target;

    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();

    pStats->count = g_sWcet.count;
    pStats->warmup_discarded = g_sWcet.warmup_discarded;
    pStats->last_cycles = g_sWcet.last;
    pStats->min_cycles = (g_sWcet.count == 0U) ? 0U : g_sWcet.min;
    pStats->max_cycles = g_sWcet.max;
    pStats->max_unpreempted_cycles = g_sWcet.max_unpreempted;
    pStats->mean_cycles = (g_sWcet.count == 0U) ? 0U : (uint32_t)(g_sWcet.sum / g_sWcet.count);
    pStats->dwt_overhead_cycles = g_sWcet.overhead;
    pStats->hist_bin_width_cycles = WCET_HIST_BIN_WIDTH;
    pStats->hist_bins = WCET_HIST_BINS;
    pStats->hist_overflow = g_sWcet.hist_overflow;
    pStats->preempted_count = g_sWcet.preempted_count;
    pStats->unpreempted_count = g_sWcet.unpreempted_count;

    if (g_sWcet.count == 0U)
    {
        pStats->p99_cycles = 0U;
        pStats->approx_bound_cycles = 0U;
        taskEXIT_CRITICAL();
        return;
    }

    target = (g_sWcet.count * 99U + 99U) / 100U;
    cumulative = 0U;
    pStats->p99_cycles = pStats->max_cycles;

    for (i = 0U; i < WCET_HIST_BINS; i++)
    {
        cumulative += g_sWcet.hist[i];
        if (cumulative >= target)
        {
            pStats->p99_cycles = ((i + 1U) * WCET_HIST_BIN_WIDTH) - 1U;
            break;
        }
    }

    pStats->approx_bound_cycles = pStats->max_cycles + (pStats->max_cycles >> 3);

    taskEXIT_CRITICAL();
}

uint32_t InferenceWcetCopyHistogram(uint32_t *pBins, uint32_t maxBins)
{
    uint32_t n;
    uint32_t i;

    if (pBins == NULL || maxBins == 0U)
    {
        return 0U;
    }

    n = (maxBins < WCET_HIST_BINS) ? maxBins : WCET_HIST_BINS;

    taskENTER_CRITICAL();
    for (i = 0U; i < n; i++)
    {
        pBins[i] = g_sWcet.hist[i];
    }
    taskEXIT_CRITICAL();

    return n;
}