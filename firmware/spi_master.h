/*
 * spi_master.h - SPI Master Interface for TM4C123GH6PM
 * 
 * Provides blocking SPI communication to FPGA Layer 1 accelerator.
 * Supports SSI0 on GPIO Port A (standard pinout).
 * 
 * SPI Configuration:
 *   - Mode 0 (CPOL=0, CPHA=0)
 *   - 1 MHz clock (configurable)
 *   - MSB-first transmission
 *   - 8-bit data frames
 */

#ifndef SPI_MASTER_H
#define SPI_MASTER_H

#include <stdint.h>

/*
 * Initialize SSI0 as SPI master (1 MHz clock, mode 0).
 * Also configures GPIO interrupt for SPI-done signal.
 * 
 * Returns 0 on success, -1 on failure.
 */
int32_t SpiMasterInit(void);

/*
 * Send feature vector to FPGA over SPI.
 * Blocks until all bytes transmitted.
 * 
 * Args:
 *   features - pointer to 304-byte INT8 array
 *   len      - should be 304
 * 
 * Returns 0 on success, -1 on error.
 */
int32_t SpiSendFeatures(const int8_t *features, uint32_t len);

/*
 * Receive Layer 1 result from FPGA over SPI.
 * Blocks until all bytes received.
 * 
 * Args:
 *   out - pointer to output buffer (at least 64 bytes)
 *   len - should be 64
 * 
 * Returns 0 on success, -1 on error.
 */
int32_t SpiRecvLayer1(int8_t *out, uint32_t len);

/*
 * Wait for FPGA computation complete (GPIO IRQ from FPGA).
 * Blocks on binary semaphore until irq_done ISR fires.
 * 
 * This is called after SpiSendFeatures and before SpiRecvLayer1.
 * Timeout is configurable (in milliseconds, or 0 for blocking).
 * 
 * Returns 0 on success, -1 on timeout.
 */
int32_t SpiWaitComputeDone(uint32_t timeout_ms);

/*
 * GPIO ISR for FPGA IRQ line.
 * Signals g_xSpiDoneSem when FPGA computation complete.
 * (Called by port_spi_irq_isr in interrupt context)
 */
void SpiGpioIsr(void);

#endif // SPI_MASTER_H
