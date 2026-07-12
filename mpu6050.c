/*
 * MPU6050 gyroscope input for line-following damping.
 */

#include "mpu6050.h"
#include "car_control.h"
#include "ti_msp_dl_config.h"

#include <stdbool.h>
#include <stdint.h>

#define MPU6050_ADDR_LOW             0x68U
#define MPU6050_ADDR_HIGH            0x69U

#define MPU6050_REG_SMPLRT_DIV       0x19U
#define MPU6050_REG_CONFIG           0x1AU
#define MPU6050_REG_GYRO_CONFIG      0x1BU
#define MPU6050_REG_GYRO_ZOUT_H      0x47U
#define MPU6050_REG_PWR_MGMT_1       0x6BU
#define MPU6050_REG_WHO_AM_I         0x75U

#define MPU6050_WHO_AM_I_VALUE       0x68U
#define MPU6050_GYRO_LSB_PER_DPS     131.0f
#define MPU6050_FILTER_ALPHA         0.30f
#define MPU6050_CALIBRATION_SAMPLES  64U
#define MPU6050_I2C_TIMEOUT_LOOPS    12000U

volatile Mpu6050_t g_mpu6050 =
{
  .address = MPU6050_ADDR_LOW,
  .present = 0U,
  .valid = 0U,
  .gyro_z_raw = 0,
  .gyro_z_dps = 0.0f,
  .gyro_z_filtered_dps = 0.0f,
  .gyro_z_bias_dps = 0.0f,
  .sample_tick = 0U,
  .error_count = 0U,
};

static uint32_t s_last_sample_tick = 0U;

static void MPU6050_ClearGyroOutput(void);
static void MPU6050_MarkError(void);
static bool MPU6050_WaitForStatus(uint32_t mask, bool set);
static bool MPU6050_WaitUntilIdle(void);
static bool MPU6050_WaitWhileBusy(void);
static bool MPU6050_HasError(void);
static void MPU6050_RecoverBus(void);
static bool MPU6050_WriteReg(uint8_t reg, uint8_t value);
static bool MPU6050_ReadRegs(uint8_t reg, uint8_t *data, uint8_t len);
static bool MPU6050_ReadGyroZRaw(int16_t *raw);
static bool MPU6050_ProbeAddress(uint8_t address);
static bool MPU6050_Configure(void);
static void MPU6050_UpdateGyro(int16_t raw);

void MPU6050_Init(void)
{
  uint8_t found = 0U;

  MPU6050_ClearGyroOutput();
  g_mpu6050.address = MPU6050_ADDR_LOW;
  g_mpu6050.present = 0U;
  g_mpu6050.valid = 0U;
  g_mpu6050.gyro_z_bias_dps = 0.0f;
  g_mpu6050.sample_tick = 0U;
  g_mpu6050.error_count = 0U;
  s_last_sample_tick = g_car.control_tick;

  if (MPU6050_ProbeAddress(MPU6050_ADDR_LOW))
  {
    g_mpu6050.address = MPU6050_ADDR_LOW;
    found = 1U;
  }
  else if (MPU6050_ProbeAddress(MPU6050_ADDR_HIGH))
  {
    g_mpu6050.address = MPU6050_ADDR_HIGH;
    found = 1U;
  }

  if (found == 0U)
  {
    MPU6050_MarkError();
    return;
  }

  g_mpu6050.present = 1U;

  if (MPU6050_Configure())
  {
    MPU6050_Calibrate();
  }
  else
  {
    MPU6050_MarkError();
  }
}

void MPU6050_Task(void)
{
  int16_t raw = 0;
  uint32_t tick = g_car.control_tick;

  if ((uint32_t)(tick - s_last_sample_tick) < 1U)
  {
    return;
  }
  s_last_sample_tick = tick;

  if (g_mpu6050.present == 0U)
  {
    MPU6050_ClearGyroOutput();
    return;
  }

  if (MPU6050_ReadGyroZRaw(&raw))
  {
    MPU6050_UpdateGyro(raw);
  }
  else
  {
    MPU6050_MarkError();
  }
}

void MPU6050_Calibrate(void)
{
  uint32_t index = 0U;
  uint32_t valid_count = 0U;
  float sum = 0.0f;

  if (g_mpu6050.present == 0U)
  {
    MPU6050_ClearGyroOutput();
    return;
  }

  g_mpu6050.valid = 0U;
  g_mpu6050.gyro_z_bias_dps = 0.0f;

  for (index = 0U; index < MPU6050_CALIBRATION_SAMPLES; index++)
  {
    int16_t raw = 0;

    if (MPU6050_ReadGyroZRaw(&raw))
    {
      sum += (float)raw / MPU6050_GYRO_LSB_PER_DPS;
      valid_count++;
    }
    else
    {
      MPU6050_MarkError();
      break;
    }

    delay_cycles(32000U);
  }

  if (valid_count > 0U)
  {
    g_mpu6050.gyro_z_bias_dps = sum / (float)valid_count;
    g_mpu6050.gyro_z_filtered_dps = 0.0f;
    g_mpu6050.gyro_z_dps = 0.0f;
    g_car.line.gyro_z = 0.0f;
    g_mpu6050.valid = 1U;
  }
}

static void MPU6050_ClearGyroOutput(void)
{
  g_mpu6050.valid = 0U;
  g_mpu6050.gyro_z_raw = 0;
  g_mpu6050.gyro_z_dps = 0.0f;
  g_mpu6050.gyro_z_filtered_dps = 0.0f;
  g_car.line.gyro_z = 0.0f;
}

static void MPU6050_MarkError(void)
{
  g_mpu6050.error_count++;
  g_mpu6050.present = 0U;
  MPU6050_ClearGyroOutput();
}

static bool MPU6050_WaitForStatus(uint32_t mask, bool set)
{
  uint32_t timeout = MPU6050_I2C_TIMEOUT_LOOPS;

  while (timeout > 0U)
  {
    uint32_t status = DL_I2C_getControllerStatus(I2C_MPU_INST);
    bool matched = ((status & mask) != 0U) ? true : false;

    if (matched == set)
    {
      return true;
    }

    if ((status & DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
    {
      return false;
    }

    timeout--;
  }

  return false;
}

static bool MPU6050_WaitUntilIdle(void)
{
  return MPU6050_WaitForStatus(DL_I2C_CONTROLLER_STATUS_IDLE, true);
}

static bool MPU6050_WaitWhileBusy(void)
{
  return MPU6050_WaitForStatus(DL_I2C_CONTROLLER_STATUS_BUSY, false);
}

static bool MPU6050_HasError(void)
{
  return ((DL_I2C_getControllerStatus(I2C_MPU_INST) &
           DL_I2C_CONTROLLER_STATUS_ERROR) != 0U) ? true : false;
}

static void MPU6050_RecoverBus(void)
{
  DL_I2C_resetControllerTransfer(I2C_MPU_INST);
  DL_I2C_flushControllerTXFIFO(I2C_MPU_INST);
  DL_I2C_flushControllerRXFIFO(I2C_MPU_INST);
  DL_I2C_clearInterruptStatus(I2C_MPU_INST,
                              DL_I2C_INTERRUPT_CONTROLLER_RX_DONE |
                              DL_I2C_INTERRUPT_CONTROLLER_TX_DONE |
                              DL_I2C_INTERRUPT_CONTROLLER_NACK |
                              DL_I2C_INTERRUPT_CONTROLLER_ARBITRATION_LOST |
                              DL_I2C_INTERRUPT_CONTROLLER_RXFIFO_TRIGGER |
                              DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_TRIGGER |
                              DL_I2C_INTERRUPT_CONTROLLER_RXFIFO_FULL |
                              DL_I2C_INTERRUPT_CONTROLLER_TXFIFO_EMPTY |
                              DL_I2C_INTERRUPT_CONTROLLER_START |
                              DL_I2C_INTERRUPT_CONTROLLER_STOP);
}

static bool MPU6050_WriteReg(uint8_t reg, uint8_t value)
{
  uint8_t data[2];

  data[0] = reg;
  data[1] = value;

  MPU6050_RecoverBus();

  if (!MPU6050_WaitUntilIdle())
  {
    MPU6050_RecoverBus();
    return false;
  }

  if (DL_I2C_fillControllerTXFIFO(I2C_MPU_INST, data, (uint16_t)sizeof(data)) !=
      (uint16_t)sizeof(data))
  {
    MPU6050_RecoverBus();
    return false;
  }

  DL_I2C_startControllerTransfer(I2C_MPU_INST,
                                 g_mpu6050.address,
                                 DL_I2C_CONTROLLER_DIRECTION_TX,
                                 (uint16_t)sizeof(data));

  if (!MPU6050_WaitWhileBusy())
  {
    MPU6050_RecoverBus();
    return false;
  }

  if (MPU6050_HasError())
  {
    MPU6050_RecoverBus();
    return false;
  }

  return true;
}

static bool MPU6050_ReadRegs(uint8_t reg, uint8_t *data, uint8_t len)
{
  uint8_t index = 0U;

  if ((data == (uint8_t *)0) || (len == 0U))
  {
    return false;
  }

  MPU6050_RecoverBus();

  if (!MPU6050_WaitUntilIdle())
  {
    MPU6050_RecoverBus();
    return false;
  }

  if (DL_I2C_fillControllerTXFIFO(I2C_MPU_INST, &reg, 1U) != 1U)
  {
    MPU6050_RecoverBus();
    return false;
  }

  DL_I2C_startControllerTransferAdvanced(I2C_MPU_INST,
                                         g_mpu6050.address,
                                         DL_I2C_CONTROLLER_DIRECTION_TX,
                                         1U,
                                         DL_I2C_CONTROLLER_START_ENABLE,
                                         DL_I2C_CONTROLLER_STOP_DISABLE,
                                         DL_I2C_CONTROLLER_ACK_DISABLE);

  if (!MPU6050_WaitWhileBusy())
  {
    MPU6050_RecoverBus();
    return false;
  }

  if (MPU6050_HasError())
  {
    MPU6050_RecoverBus();
    return false;
  }

  DL_I2C_startControllerTransferAdvanced(I2C_MPU_INST,
                                         g_mpu6050.address,
                                         DL_I2C_CONTROLLER_DIRECTION_RX,
                                         len,
                                         DL_I2C_CONTROLLER_START_ENABLE,
                                         DL_I2C_CONTROLLER_STOP_ENABLE,
                                         DL_I2C_CONTROLLER_ACK_DISABLE);

  for (index = 0U; index < len; index++)
  {
    uint32_t timeout = MPU6050_I2C_TIMEOUT_LOOPS;

    while (DL_I2C_isControllerRXFIFOEmpty(I2C_MPU_INST) != false)
    {
      if ((DL_I2C_getControllerStatus(I2C_MPU_INST) &
           DL_I2C_CONTROLLER_STATUS_ERROR) != 0U)
      {
        return false;
      }

      if (timeout == 0U)
      {
        MPU6050_RecoverBus();
        return false;
      }
      timeout--;
    }

    data[index] = DL_I2C_receiveControllerData(I2C_MPU_INST);
  }

  if (!MPU6050_WaitWhileBusy())
  {
    MPU6050_RecoverBus();
    return false;
  }

  if (MPU6050_HasError())
  {
    MPU6050_RecoverBus();
    return false;
  }

  return true;
}

static bool MPU6050_ReadGyroZRaw(int16_t *raw)
{
  uint8_t data[2];

  if (MPU6050_ReadRegs(MPU6050_REG_GYRO_ZOUT_H, data, 2U) == false)
  {
    return false;
  }

  *raw = (int16_t)(((uint16_t)data[0] << 8) | data[1]);
  return true;
}

static bool MPU6050_ProbeAddress(uint8_t address)
{
  uint8_t who_am_i = 0U;
  uint8_t previous_address = g_mpu6050.address;
  bool ok = false;

  g_mpu6050.address = address;
  ok = MPU6050_ReadRegs(MPU6050_REG_WHO_AM_I, &who_am_i, 1U);
  g_mpu6050.address = previous_address;

  return ((ok != false) && (who_am_i == MPU6050_WHO_AM_I_VALUE)) ? true : false;
}

static bool MPU6050_Configure(void)
{
  bool ok = true;

  ok = ok && MPU6050_WriteReg(MPU6050_REG_PWR_MGMT_1, 0x00U);
  delay_cycles(320000U);
  ok = ok && MPU6050_WriteReg(MPU6050_REG_SMPLRT_DIV, 0x09U);
  ok = ok && MPU6050_WriteReg(MPU6050_REG_CONFIG, 0x03U);
  ok = ok && MPU6050_WriteReg(MPU6050_REG_GYRO_CONFIG, 0x00U);

  return ok;
}

static void MPU6050_UpdateGyro(int16_t raw)
{
  float gyro = ((float)raw / MPU6050_GYRO_LSB_PER_DPS) -
               g_mpu6050.gyro_z_bias_dps;

  g_mpu6050.gyro_z_raw = raw;
  g_mpu6050.gyro_z_dps = gyro;
  g_mpu6050.gyro_z_filtered_dps +=
      MPU6050_FILTER_ALPHA * (gyro - g_mpu6050.gyro_z_filtered_dps);
  g_mpu6050.valid = 1U;
  g_mpu6050.present = 1U;
  g_mpu6050.sample_tick++;
  g_car.line.gyro_z = g_mpu6050.gyro_z_filtered_dps;
}
