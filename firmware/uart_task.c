//*****************************************************************************
// uart_task.c - UART comms / replay task.
//
// Two responsibilities:
//
// 1. TX heartbeat: prints a status line every 5 seconds so you know the
//    system is alive even if sensor/inference are blocked.
//
// 2. RX replay: listens for incoming bytes on UART0 from your PC.
//    A Python script on the PC sends pre-recorded IMU windows in the
//    replay frame format below. The task repackages them into SensorWindow_t
//    and injects them directly into g_xSensorQueue, bypassing the sensor
//    task. This lets you test inference end-to-end before the IMU arrives.
//
// Replay frame format (little-endian):
//   [0]      0xAA           - start byte
//   [1]      0x55           - start byte
//   [2..3]   uint16_t       - window index (for sequencing checks)
//   [4..603] int16_t[50][6] - raw sample data (WINDOW_SIZE * SENSOR_AXES * 2)
//   [604]    uint8_t        - XOR checksum of bytes [2..603]
//   [605]    0xBB           - end byte
//   Total: 606 bytes per frame
//*****************************************************************************

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "driverlib/uart.h"
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "sensor_task.h"
#include "uart_task.h"
#include "priorities.h"

#define UART_TASK_STACK     256
#define UART_REPLAY_ENABLE  0   // 0: production/RM runs, 1: host replay injection mode
#define UART_HEARTBEAT_ENABLE 0 // 0: clean RM timing, 1: periodic liveness print every 5 s

#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
#define DWT_SNAPSHOT()  (DWT_CYCCNT)

typedef struct
{
    uint32_t count;
    uint32_t last;
    uint32_t min;
    uint32_t max;
    uint64_t sum;
} UARTTimingState_t;

static UARTTimingState_t g_sUartTiming = {0U, 0U, UINT32_MAX, 0U, 0ULL};

static void UARTTimingRecord(uint32_t cycles)
{
    taskENTER_CRITICAL();
    g_sUartTiming.count++;
    g_sUartTiming.last = cycles;
    g_sUartTiming.sum += cycles;

    if (cycles < g_sUartTiming.min)
    {
        g_sUartTiming.min = cycles;
    }
    if (cycles > g_sUartTiming.max)
    {
        g_sUartTiming.max = cycles;
    }
    taskEXIT_CRITICAL();
}

#if UART_REPLAY_ENABLE
#define REPLAY_START0   0xAA
#define REPLAY_START1   0x55
#define REPLAY_END      0xBB
#define REPLAY_HEADER   4                                   // 2 start + 2 idx
#define REPLAY_PAYLOAD  (WINDOW_SIZE * SENSOR_AXES * 2)    // 600 bytes
#define REPLAY_FRAME    (REPLAY_HEADER + REPLAY_PAYLOAD + 1 + 1) // 606 bytes
#endif

extern SemaphoreHandle_t g_pUARTSemaphore;

//*****************************************************************************
// read_byte - blocking single-byte read from UART0 raw registers.
// UARTStdioConfig takes over UART0 for printf but the underlying RX FIFO
// is still readable directly. Blocks in 1 ms slices to stay RTOS-friendly.
//*****************************************************************************
#if UART_REPLAY_ENABLE
static uint8_t read_byte(void)
{
    while (UARTCharsAvail(UART0_BASE) == false)
    {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return (uint8_t)UARTCharGetNonBlocking(UART0_BASE);
}

//*****************************************************************************
// try_parse_replay_frame - called when REPLAY_START0 is seen.
// Returns true and fills *pWindow if a valid frame is received.
//*****************************************************************************
static bool try_parse_replay_frame(SensorWindow_t *pWindow)
{
    uint8_t  b;
    uint8_t  chk = 0;
    uint16_t window_idx;
    uint8_t  raw[REPLAY_PAYLOAD];

    // Second start byte
    b = read_byte();
    if (b != REPLAY_START1) return false;

    // Window index (2 bytes, little-endian) - included in checksum
    uint8_t idx_lo = read_byte();
    uint8_t idx_hi = read_byte();
    chk ^= idx_lo;
    chk ^= idx_hi;
    window_idx = (uint16_t)(idx_lo | (idx_hi << 8));
    (void)window_idx;   // use for sequence checking later if needed

    // Payload
    for (int i = 0; i < REPLAY_PAYLOAD; i++)
    {
        raw[i] = read_byte();
        chk ^= raw[i];
    }

    // Checksum byte (covers bytes [2..603])
    uint8_t rx_chk = read_byte();
    if (rx_chk != chk) return false;

    // End byte
    b = read_byte();
    if (b != REPLAY_END) return false;

    // Unpack into SensorWindow_t
    int byte_idx = 0;
    for (int s = 0; s < WINDOW_SIZE; s++)
    {
        for (int a = 0; a < SENSOR_AXES; a++)
        {
            uint8_t lo = raw[byte_idx++];
            uint8_t hi = raw[byte_idx++];
            pWindow->data[s][a] = (int16_t)(lo | (hi << 8));
        }
    }
    pWindow->timestamp_ms = xTaskGetTickCount();

    return true;
}
#endif

//*****************************************************************************
// UARTTask
//*****************************************************************************
static void UARTTask(void *pvParameters)
{
#if UART_REPLAY_ENABLE || UART_HEARTBEAT_ENABLE
    uint32_t        replay_count = 0;
#endif
#if UART_HEARTBEAT_ENABLE
    uint32_t        heartbeat_count = 0;
    TickType_t      xLastHeartbeat = xTaskGetTickCount();
#endif
#if UART_REPLAY_ENABLE
    SensorWindow_t  replay_window;
#endif
    uint32_t        t0;
    uint32_t        t1;

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));  // yield; 10 ms RX poll granularity
        t0 = DWT_SNAPSHOT();

        // --- Optional heartbeat every 5 seconds ---
    #if UART_HEARTBEAT_ENABLE
        if ((xTaskGetTickCount() - xLastHeartbeat) >= pdMS_TO_TICKS(5000))
        {
            xLastHeartbeat = xTaskGetTickCount();
            xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
            UARTprintf("[UART]  heartbeat #%u  replay_rx=%u\n",
                       heartbeat_count++, replay_count);
            xSemaphoreGive(g_pUARTSemaphore);
        }
    #endif

        // --- Optional replay path (disabled for production WCET/RM campaigns) ---
    #if UART_REPLAY_ENABLE
        if (UARTCharsAvail(UART0_BASE))
        {
            uint8_t b = (uint8_t)UARTCharGetNonBlocking(UART0_BASE);
            if (b == REPLAY_START0)
            {
                if (try_parse_replay_frame(&replay_window))
                {
                    // Inject into sensor queue as if it came from real IMU
                    if (xQueueSend(g_xSensorQueue, &replay_window, 0) == pdPASS)
                    {
                        replay_count++;
                        xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
                        UARTprintf("[UART]  replay frame %u injected\n",
                                   replay_count);
                        xSemaphoreGive(g_pUARTSemaphore);
                    }
                    else
                    {
                        xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
                        UARTprintf("[UART]  replay frame dropped (queue full)\n");
                        xSemaphoreGive(g_pUARTSemaphore);
                    }
                }
                else
                {
                    xSemaphoreTake(g_pUARTSemaphore, portMAX_DELAY);
                    UARTprintf("[UART]  replay frame BAD checksum/framing\n");
                    xSemaphoreGive(g_pUARTSemaphore);
                }
            }
        }
#endif

        t1 = DWT_SNAPSHOT();
        UARTTimingRecord(t1 - t0);
    }
}

//*****************************************************************************
// UARTTaskInit
//*****************************************************************************
uint32_t UARTTaskInit(void)
{
    if (xTaskCreate(UARTTask, "UART", UART_TASK_STACK, NULL,
                    tskIDLE_PRIORITY + PRIORITY_UART_TASK, NULL) != pdTRUE)
    {
        return 1;
    }
    return 0;
}

void UARTTaskTimingGetStats(UARTTaskTimingStats_t *pStats)
{
    if (pStats == NULL)
    {
        return;
    }

    taskENTER_CRITICAL();
    pStats->count = g_sUartTiming.count;
    pStats->last_cycles = g_sUartTiming.last;
    pStats->min_cycles = (g_sUartTiming.count == 0U) ? 0U : g_sUartTiming.min;
    pStats->max_cycles = g_sUartTiming.max;
    pStats->mean_cycles = (g_sUartTiming.count == 0U) ? 0U : (uint32_t)(g_sUartTiming.sum / g_sUartTiming.count);
    taskEXIT_CRITICAL();
}