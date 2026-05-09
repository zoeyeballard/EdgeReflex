/*
 * spi_master.c - SPI Master Implementation for TM4C123GH6PM
 * 
 * Configures SSI0 for 1 MHz SPI master communication with DE0-CV FPGA.
 * GPIO port A pin assignments:
 *   PA2 - SSI0 SCLK (output)
 *   PA4 - SSI0 RX/MISO (input)
 *   PA5 - SSI0 TX/MOSI (output)
 *   PB4 - GPIO input for FPGA IRQ (optional; can be any GPIO)
 *   PC4 - GPIO output for chip select (or use SSI0 CS on PA3)
 */

#include <stdbool.h>
#include <stdint.h>
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ssi.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/ssi.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "utils/uartstdio.h"
#include "FreeRTOS.h"
#include "semphr.h"

#include "spi_master.h"

/*
 * Global semaphore for synchronizing SPI completion.
 * Given by GPIO ISR when FPGA signals computation done.
 */
SemaphoreHandle_t g_xSpiDoneSem = NULL;

/*
 * Initialize SPI master (SSI0) on TM4C123GH6PM.
 * 
 * Configuration:
 *   - SSI0 on GPIO Port A
 *   - 1 MHz clock (80 MHz system clock / 80 = 1 MHz)
 *   - SPI Mode 0 (CPOL=0, CPHA=0)
 *   - Master mode
 *   - 8-bit data frames
 *   - MSB first
 */
int32_t SpiMasterInit(void)
{
    // Create semaphore for SPI completion
    g_xSpiDoneSem = xSemaphoreCreateBinary();
    if (g_xSpiDoneSem == NULL) {
        UARTprintf("[SPI] Failed to create semaphore\n");
        return -1;
    }
    
    // Enable peripheral clocks
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);  // For IRQ input
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);  // For chip select
    
    // Wait for peripherals to be ready
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_SSI0) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB) ||
           !SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {
        // Wait
    }
    
    // Configure GPIO Port A pins for SSI0
    // PA2 = SCLK, PA4 = RX, PA5 = TX (PA3 = CS, but we use GPIO for CS)
    GPIOPinConfigure(GPIO_PA2_SSI0CLK);
    GPIOPinConfigure(GPIO_PA4_SSI0RX);
    GPIOPinConfigure(GPIO_PA5_SSI0TX);
    
    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5);
    
    // Configure PA3 as GPIO for chip select (output, initially high)
    GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, GPIO_PIN_3);
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, GPIO_PIN_3);  // CS high (inactive)
    
    // Configure PB4 as GPIO input for FPGA IRQ (active-high pulse)
    GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, GPIO_PIN_4);
    
    // Configure PC4 as GPIO output for chip select (alternative location)
    // Uncomment if using PC4 instead of PA3
    // GPIOPinTypeGPIOOutput(GPIO_PORTC_BASE, GPIO_PIN_4);
    // GPIOPinWrite(GPIO_PORTC_BASE, GPIO_PIN_4, GPIO_PIN_4);  // CS high
    
    // Configure SSI0
    // Clock source: system clock (80 MHz)
    // Bit rate: 1 MHz (prescaler = 80)
    SSIConfigSetExpClk(SSI0_BASE,
                       SysCtlClockGet(),  // 80 MHz
                       SSI_FRF_MOTO_MODE_0,  // Mode 0: CPOL=0, CPHA=0
                       SSI_MODE_MASTER,   // Master mode
                       1000000,           // 1 MHz bit rate
                       8);                // 8-bit data
    
    // Enable SSI0
    SSIEnable(SSI0_BASE);
    
    // Flush any remaining data from RX FIFO
    while (SSIDataGetNonBlocking(SSI0_BASE, NULL)) {
        // Discard
    }
    
    // Configure GPIO interrupt on PB4 (IRQ from FPGA)
    GPIOIntTypeSet(GPIO_PORTB_BASE, GPIO_PIN_4, GPIO_RISING_EDGE);
    
    // Register interrupt handler
    GPIOIntRegister(GPIO_PORTB_BASE, SpiGpioIsr);
    
    // Enable GPIO interrupt
    IntEnable(INT_GPIOB);
    GPIOIntEnable(GPIO_PORTB_BASE, GPIO_PIN_4);
    
    UARTprintf("[SPI] SSI0 initialized: 1 MHz, Mode 0, Master\n");
    
    return 0;
}

/*
 * Transmit feature vector to FPGA over SPI.
 * 
 * Protocol:
 *   1. Assert CS (PA3 = low)
 *   2. Send 304 bytes over SSI0
 *   3. Deassert CS (PA3 = high)
 *   4. Wait for IRQ pulse from FPGA (indicates computation done)
 */
int32_t SpiSendFeatures(const int8_t *features, uint32_t len)
{
    uint32_t i;
    uint32_t dummy;
    
    if (features == NULL || len != 304) {
        UARTprintf("[SPI] Invalid feature vector (len=%d, expected 304)\n", len);
        return -1;
    }
    
    // Assert chip select
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 0);
    
    // Wait a few cycles for CS to settle
    for (i = 0; i < 100; i++) { asm("nop"); }
    
    // Send all 304 bytes
    for (i = 0; i < len; i++) {
        // Send byte
        SSIDataPut(SSI0_BASE, (uint32_t)features[i]);
        
        // Wait for byte to be transmitted
        while (SSIBusy(SSI0_BASE)) {
            // Busy
        }
        
        // Read back any received data (should be 0 during RX phase)
        while (SSIDataGetNonBlocking(SSI0_BASE, &dummy)) {
            // Discard
        }
    }
    
    // Deassert chip select
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, GPIO_PIN_3);
    
    UARTprintf("[SPI] Sent %d feature bytes\n", len);
    
    return 0;
}

/*
 * Receive Layer 1 result from FPGA over SPI.
 * 
 * Protocol:
 *   1. Assert CS (PA3 = low)
 *   2. Receive 64 bytes over SSI0 (send dummy 0xFF bytes to clock in data)
 *   3. Deassert CS (PA3 = high)
 */
int32_t SpiRecvLayer1(int8_t *out, uint32_t len)
{
    uint32_t i;
    uint32_t rx_data;
    
    if (out == NULL || len != 64) {
        UARTprintf("[SPI] Invalid output buffer (len=%d, expected 64)\n", len);
        return -1;
    }
    
    // Assert chip select
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, 0);
    
    // Wait a few cycles for CS to settle
    for (i = 0; i < 100; i++) { asm("nop"); }
    
    // Receive all 64 bytes
    for (i = 0; i < len; i++) {
        // Send dummy byte to clock in data
        SSIDataPut(SSI0_BASE, 0xFF);
        
        // Wait for byte to be received
        while (SSIBusy(SSI0_BASE)) {
            // Busy
        }
        
        // Read received byte
        if (SSIDataGetNonBlocking(SSI0_BASE, &rx_data)) {
            out[i] = (int8_t)(rx_data & 0xFF);
        } else {
            UARTprintf("[SPI] Failed to receive byte %d\n", i);
            return -1;
        }
    }
    
    // Deassert chip select
    GPIOPinWrite(GPIO_PORTA_BASE, GPIO_PIN_3, GPIO_PIN_3);
    
    UARTprintf("[SPI] Received %d result bytes\n", len);
    
    return 0;
}

/*
 * Wait for FPGA computation complete (IRQ pulse).
 * Blocks on semaphore until GPIO ISR fires.
 */
int32_t SpiWaitComputeDone(uint32_t timeout_ms)
{
    BaseType_t result;
    TickType_t timeout_ticks;
    
    if (timeout_ms == 0) {
        timeout_ticks = portMAX_DELAY;  // Block forever
    } else {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    }
    
    // Wait for semaphore
    result = xSemaphoreTake(g_xSpiDoneSem, timeout_ticks);
    
    if (result == pdTRUE) {
        return 0;  // Success
    } else {
        UARTprintf("[SPI] Timeout waiting for compute done\n");
        return -1;  // Timeout
    }
}

/*
 * GPIO ISR for FPGA IRQ line (PB4).
 * Signals g_xSpiDoneSem when IRQ pulse detected.
 */
void SpiGpioIsr(void)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // Clear GPIO interrupt
    GPIOIntClear(GPIO_PORTB_BASE, GPIO_PIN_4);
    
    // Give semaphore to wake waiting task
    if (g_xSpiDoneSem != NULL) {
        xSemaphoreGiveFromISR(g_xSpiDoneSem, &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
