//*****************************************************************************
// mpu6050.h - MPU-6050 register map and driver API for TM4C / TivaWare.
//*****************************************************************************
#ifndef __MPU6050_H__
#define __MPU6050_H__

#include <stdint.h>
#include <stdbool.h>

//*****************************************************************************
// I2C bus and pin configuration.
// MPU-6050 AD0 pin LOW  → address 0x68
// MPU-6050 AD0 pin HIGH → address 0x69
// Wire: PB2 = I2C0SCL, PB3 = I2C0SDA
//*****************************************************************************
#define MPU6050_I2C_BASE        I2C0_BASE
#define MPU6050_I2C_PERIPH      SYSCTL_PERIPH_I2C0
#define MPU6050_GPIO_PERIPH     SYSCTL_PERIPH_GPIOB
#define MPU6050_GPIO_BASE       GPIO_PORTB_BASE
#define MPU6050_SCL_PIN         GPIO_PIN_2
#define MPU6050_SDA_PIN         GPIO_PIN_3
#define MPU6050_I2C_ADDR        0x68    // AD0 tied LOW

//*****************************************************************************
// Register addresses
//*****************************************************************************
#define MPU6050_REG_SMPLRT_DIV      0x19
#define MPU6050_REG_CONFIG          0x1A
#define MPU6050_REG_GYRO_CONFIG     0x1B
#define MPU6050_REG_ACCEL_CONFIG    0x1C
#define MPU6050_REG_ACCEL_XOUT_H    0x3B    // first of 14 burst bytes
#define MPU6050_REG_PWR_MGMT_1      0x6B
#define MPU6050_REG_WHO_AM_I        0x75    // should read 0x68

//*****************************************************************************
// Configuration values
// Gyro  FS = ±250 °/s  → 131 LSB/(°/s)
// Accel FS = ±2 g      → 16384 LSB/g
// DLPF bandwidth = 44 Hz (good for 50 Hz sampling)
//*****************************************************************************
#define MPU6050_GYRO_FS_250         0x00
#define MPU6050_ACCEL_FS_2G         0x00
#define MPU6050_DLPF_BW_44          0x03
#define MPU6050_CLK_PLL_XGYRO       0x01    // use gyro X as clock ref

//*****************************************************************************
// Driver API
//*****************************************************************************

// Call once from SensorTaskInit() before vTaskStartScheduler().
// Returns true on success, false if WHO_AM_I check fails.
bool MPU6050_Init(void);

// Read one sample. Fills ax,ay,az,gx,gy,gz as raw 16-bit signed counts.
// Returns true on success, false on I2C bus error.
bool MPU6050_ReadSample(int16_t *ax, int16_t *ay, int16_t *az,
                        int16_t *gx, int16_t *gy, int16_t *gz);

#endif // __MPU6050_H__