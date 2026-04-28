//*****************************************************************************
// mpu6050.c - MPU-6050 I2C driver for TM4C123 / TivaWare.
//*****************************************************************************

#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_memmap.h"
#include "inc/hw_i2c.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/i2c.h"
#include "driverlib/pin_map.h"
#include "utils/uartstdio.h"

#include "mpu6050.h"

//*****************************************************************************
// Internal helper: write one byte to a register.
// Returns true on success.
//*****************************************************************************
static bool I2C_WriteReg(uint8_t reg, uint8_t value)
{
    // Set slave address, write mode
    I2CMasterSlaveAddrSet(MPU6050_I2C_BASE, MPU6050_I2C_ADDR, false);

    // Send register address
    I2CMasterDataPut(MPU6050_I2C_BASE, reg);
    I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(MPU6050_I2C_BASE));
    if (I2CMasterErr(MPU6050_I2C_BASE) != I2C_MASTER_ERR_NONE) return false;

    // Send data byte
    I2CMasterDataPut(MPU6050_I2C_BASE, value);
    I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
    while (I2CMasterBusy(MPU6050_I2C_BASE));
    if (I2CMasterErr(MPU6050_I2C_BASE) != I2C_MASTER_ERR_NONE) return false;

    return true;
}

//*****************************************************************************
// Internal helper: read N bytes starting from reg into buf.
// Uses repeated-start burst read (write reg addr, then read N bytes).
//*****************************************************************************
static bool I2C_ReadBurst(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    // Write register address (no stop)
    I2CMasterSlaveAddrSet(MPU6050_I2C_BASE, MPU6050_I2C_ADDR, false);
    I2CMasterDataPut(MPU6050_I2C_BASE, reg);
    I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_START);
    while (I2CMasterBusy(MPU6050_I2C_BASE));
    if (I2CMasterErr(MPU6050_I2C_BASE) != I2C_MASTER_ERR_NONE) return false;

    // Switch to read mode (repeated start)
    I2CMasterSlaveAddrSet(MPU6050_I2C_BASE, MPU6050_I2C_ADDR, true);

    for (i = 0; i < len; i++)
    {
        if (i == 0 && len == 1)
        {
            // Single byte read
            I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE);
        }
        else if (i == 0)
        {
            // First of multiple bytes
            I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_START);
        }
        else if (i == len - 1)
        {
            // Last byte — send NACK + STOP
            I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
        }
        else
        {
            // Middle bytes
            I2CMasterControl(MPU6050_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_CONT);
        }

        while (I2CMasterBusy(MPU6050_I2C_BASE));
        if (I2CMasterErr(MPU6050_I2C_BASE) != I2C_MASTER_ERR_NONE) return false;

        buf[i] = (uint8_t)I2CMasterDataGet(MPU6050_I2C_BASE);
    }

    return true;
}

//*****************************************************************************
// MPU6050_Init
//*****************************************************************************
bool MPU6050_Init(void)
{
    uint8_t whoami;

    // Enable I2C0 and GPIOB peripherals
    SysCtlPeripheralEnable(MPU6050_I2C_PERIPH);
    SysCtlPeripheralEnable(MPU6050_GPIO_PERIPH);
    while (!SysCtlPeripheralReady(MPU6050_I2C_PERIPH));
    while (!SysCtlPeripheralReady(MPU6050_GPIO_PERIPH));

    // Configure PB2 (SCL) and PB3 (SDA) as I2C pins
    GPIOPinConfigure(GPIO_PB2_I2C0SCL);
    GPIOPinConfigure(GPIO_PB3_I2C0SDA);
    GPIOPinTypeI2CSCL(MPU6050_GPIO_BASE, MPU6050_SCL_PIN);
    GPIOPinTypeI2C(MPU6050_GPIO_BASE, MPU6050_SDA_PIN);

    // Initialize I2C master at 400 kHz (fast mode)
    I2CMasterInitExpClk(MPU6050_I2C_BASE, SysCtlClockGet(), true);

    // Wake the MPU-6050 (clear sleep bit, select PLL clock)
    if (!I2C_WriteReg(MPU6050_REG_PWR_MGMT_1, MPU6050_CLK_PLL_XGYRO))
    {
        UARTprintf("[MPU6050] Init failed: PWR_MGMT_1 write error\n");
        return false;
    }

    // Small delay to let the device wake up
    SysCtlDelay(SysCtlClockGet() / 100);    // ~10 ms at 80 MHz

    // Verify WHO_AM_I register
    if (!I2C_ReadBurst(MPU6050_REG_WHO_AM_I, &whoami, 1) || whoami != 0x68)
    {
        UARTprintf("[MPU6050] Init failed: WHO_AM_I=0x%02X (expected 0x68)\n", whoami);
        return false;
    }

    // Configure sample rate divider: 1 kHz / (1 + 19) = 50 Hz
    // This matches SAMPLE_PERIOD_MS = 20 in sensor_task.c
    if (!I2C_WriteReg(MPU6050_REG_SMPLRT_DIV, 19)) return false;

    // DLPF: 44 Hz bandwidth — filters vibration above Nyquist for 50 Hz sampling
    if (!I2C_WriteReg(MPU6050_REG_CONFIG, MPU6050_DLPF_BW_44)) return false;

    // Gyro full scale: ±250 °/s
    if (!I2C_WriteReg(MPU6050_REG_GYRO_CONFIG, MPU6050_GYRO_FS_250)) return false;

    // Accel full scale: ±2 g
    if (!I2C_WriteReg(MPU6050_REG_ACCEL_CONFIG, MPU6050_ACCEL_FS_2G)) return false;

    UARTprintf("[MPU6050] Init OK — 50 Hz, ±2g, ±250dps, DLPF 44 Hz\n");
    return true;
}

//*****************************************************************************
// MPU6050_ReadSample
// Reads 14 bytes starting at ACCEL_XOUT_H in one burst:
//   [0-1]  ACCEL_X   [2-3]  ACCEL_Y   [4-5]  ACCEL_Z
//   [6-7]  TEMP (discarded)
//   [8-9]  GYRO_X    [10-11] GYRO_Y   [12-13] GYRO_Z
//*****************************************************************************
bool MPU6050_ReadSample(int16_t *ax, int16_t *ay, int16_t *az,
                        int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t raw[14];

    if (!I2C_ReadBurst(MPU6050_REG_ACCEL_XOUT_H, raw, 14))
    {
        return false;
    }

    // Each value is big-endian: high byte first
    *ax = (int16_t)((raw[0]  << 8) | raw[1]);
    *ay = (int16_t)((raw[2]  << 8) | raw[3]);
    *az = (int16_t)((raw[4]  << 8) | raw[5]);
    // raw[6..7] = temperature, skip
    *gx = (int16_t)((raw[8]  << 8) | raw[9]);
    *gy = (int16_t)((raw[10] << 8) | raw[11]);
    *gz = (int16_t)((raw[12] << 8) | raw[13]);

    return true;
}