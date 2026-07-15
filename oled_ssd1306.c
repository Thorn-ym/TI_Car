/*
 * SSD1306 128x64 I2C OLED driver.
 */

#include "oled_ssd1306.h"
#include "board_i2c.h"
#include "ti_msp_dl_config.h"

#include <stdbool.h>
#include <stdint.h>

#define OLED_ADDR_LOW       0x3CU
#define OLED_ADDR_HIGH      0x3DU
#define OLED_BUFFER_SIZE    ((OLED_WIDTH * OLED_HEIGHT) / 8U)

#define OLED_GLYPH_XUAN     0U
#define OLED_GLYPH_ZE       1U
#define OLED_GLYPH_DI       2U
#define OLED_GLYPH_TI       3U
#define OLED_GLYPH_YI       4U
#define OLED_GLYPH_JING     5U

volatile OLED_t g_oled =
{
  .address = OLED_ADDR_LOW,
  .present = 0U,
  .initialized = 0U,
  .error_count = 0U,
};

static uint8_t s_buffer[OLED_BUFFER_SIZE];

static const uint8_t s_font5x7[11][5] =
{
  {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}, /* 0 */
  {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}, /* 1 */
  {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}, /* 2 */
  {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}, /* 3 */
  {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}, /* 4 */
  {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}, /* 5 */
  {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}, /* 6 */
  {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}, /* 7 */
  {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}, /* 8 */
  {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}, /* 9 */
  {0x00U, 0x00U, 0x00U, 0x00U, 0x00U}, /* space */
};

static const uint8_t s_chinese16[][32] =
{
  /* 选 */
  {
    0x40U, 0x42U, 0xCCU, 0x00U, 0x08U, 0x48U, 0x48U, 0x7FU,
    0x48U, 0x48U, 0x7FU, 0x48U, 0x48U, 0x08U, 0x00U, 0x00U,
    0x40U, 0x20U, 0x1FU, 0x20U, 0x44U, 0x4AU, 0x49U, 0x48U,
    0x48U, 0x48U, 0x48U, 0x49U, 0x4AU, 0x44U, 0x40U, 0x00U
  },
  /* 择 */
  {
    0x10U, 0x10U, 0x10U, 0xFFU, 0x10U, 0x90U, 0x08U, 0x88U,
    0x69U, 0x59U, 0x4BU, 0x4DU, 0x69U, 0x88U, 0x08U, 0x00U,
    0x04U, 0x44U, 0x82U, 0x7FU, 0x01U, 0x80U, 0x88U, 0x88U,
    0x88U, 0x88U, 0xFFU, 0x88U, 0x88U, 0x88U, 0x80U, 0x00U
  },
  /* 第 */
  {
    0x08U, 0x08U, 0xFDU, 0x0AU, 0x08U, 0x00U, 0x08U, 0x08U,
    0xFDU, 0x0AU, 0x08U, 0x00U, 0xFEU, 0x22U, 0x22U, 0x00U,
    0x10U, 0x10U, 0x1FU, 0x08U, 0x88U, 0x68U, 0x18U, 0x0EU,
    0x09U, 0x08U, 0x08U, 0x08U, 0xFFU, 0x08U, 0x08U, 0x00U
  },
  /* 题 */
  {
    0x00U, 0xFEU, 0x92U, 0x92U, 0x92U, 0xFEU, 0x00U, 0x02U,
    0x02U, 0xFAU, 0x0AU, 0x0AU, 0x0AU, 0xFEU, 0x02U, 0x00U,
    0x80U, 0x8FU, 0x44U, 0x44U, 0x24U, 0x1FU, 0x00U, 0x80U,
    0x83U, 0x6CU, 0x10U, 0x10U, 0x6CU, 0x83U, 0x80U, 0x00U
  },
  /* 已 */
  {
    0x00U, 0x02U, 0x02U, 0x02U, 0x02U, 0x02U, 0x02U, 0xFEU,
    0x02U, 0x02U, 0x02U, 0x02U, 0x02U, 0x02U, 0x00U, 0x00U,
    0x00U, 0x00U, 0x7FU, 0x40U, 0x40U, 0x40U, 0x40U, 0x43U,
    0x44U, 0x48U, 0x50U, 0x60U, 0x40U, 0x00U, 0x00U, 0x00U
  },
  /* 经 */
  {
    0x20U, 0x30U, 0xACU, 0x63U, 0x10U, 0x00U, 0x22U, 0x22U,
    0x22U, 0xE2U, 0x12U, 0x0AU, 0x86U, 0x62U, 0x00U, 0x00U,
    0x22U, 0x67U, 0x22U, 0x12U, 0x12U, 0x40U, 0x44U, 0x44U,
    0x44U, 0x7FU, 0x44U, 0x44U, 0x44U, 0x40U, 0x00U, 0x00U
  },
};

static const uint8_t s_oled_init_cmds[] =
{
  0xAEU, 0x20U, 0x00U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
  0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
  0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
  0x40U, 0x8DU, 0x14U, 0xAFU
};

static bool OLED_WriteCommand(uint8_t cmd);
static bool OLED_WriteCommands(const uint8_t *cmds, uint16_t len);
static bool OLED_WriteData(const uint8_t *data, uint16_t len);
static bool OLED_SelectAddress(uint8_t address);
static void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on);
static void OLED_DrawDigit(uint8_t x, uint8_t page, uint8_t digit);

void OLED_Init(void)
{
  g_oled.present = 0U;
  g_oled.initialized = 0U;

  if (!OLED_SelectAddress(OLED_ADDR_LOW) &&
      !OLED_SelectAddress(OLED_ADDR_HIGH))
  {
    g_oled.error_count++;
    return;
  }

  if (!OLED_WriteCommands(s_oled_init_cmds, (uint16_t)sizeof(s_oled_init_cmds)))
  {
    g_oled.error_count++;
    g_oled.present = 0U;
    return;
  }

  OLED_Clear();
  OLED_Update();
  g_oled.initialized = 1U;
}

void OLED_Clear(void)
{
  uint16_t index = 0U;

  for (index = 0U; index < OLED_BUFFER_SIZE; index++)
  {
    s_buffer[index] = 0U;
  }
}

void OLED_Update(void)
{
  uint8_t page = 0U;

  if (g_oled.present == 0U)
  {
    return;
  }

  for (page = 0U; page < 8U; page++)
  {
    uint8_t cmd[3];

    cmd[0] = (uint8_t)(0xB0U + page);
    cmd[1] = 0x00U;
    cmd[2] = 0x10U;

    if (!OLED_WriteCommands(cmd, (uint16_t)sizeof(cmd)) ||
        !OLED_WriteData(&s_buffer[(uint16_t)page * OLED_WIDTH], OLED_WIDTH))
    {
      g_oled.error_count++;
      return;
    }
  }
}

void OLED_DrawAscii(uint8_t x, uint8_t page, const char *text)
{
  uint8_t cursor = x;

  if ((text == (const char *)0) || (page >= 8U))
  {
    return;
  }

  while ((*text != '\0') && (cursor < (OLED_WIDTH - 5U)))
  {
    uint8_t glyph = 10U;
    uint8_t col = 0U;

    if ((*text >= '0') && (*text <= '9'))
    {
      glyph = (uint8_t)(*text - '0');
    }
    else if (*text == ' ')
    {
      glyph = 10U;
    }

    for (col = 0U; col < 5U; col++)
    {
      s_buffer[((uint16_t)page * OLED_WIDTH) + cursor + col] = s_font5x7[glyph][col];
    }

    if ((cursor + 5U) < OLED_WIDTH)
    {
      s_buffer[((uint16_t)page * OLED_WIDTH) + cursor + 5U] = 0U;
    }

    cursor = (uint8_t)(cursor + 6U);
    text++;
  }
}

void OLED_DrawChinese(uint8_t x, uint8_t page, uint8_t glyph_id)
{
  uint8_t col = 0U;

  if ((glyph_id >= (uint8_t)(sizeof(s_chinese16) / sizeof(s_chinese16[0]))) ||
      (page > 6U) ||
      (x > (OLED_WIDTH - 16U)))
  {
    return;
  }

  for (col = 0U; col < 16U; col++)
  {
    s_buffer[((uint16_t)page * OLED_WIDTH) + x + col] =
        s_chinese16[glyph_id][col];
    s_buffer[((uint16_t)(page + 1U) * OLED_WIDTH) + x + col] =
        s_chinese16[glyph_id][16U + col];
  }
}

void OLED_DrawProblemSelect(uint8_t problem)
{
  OLED_Clear();
  OLED_DrawChinese(22U, 2U, OLED_GLYPH_XUAN);
  OLED_DrawChinese(40U, 2U, OLED_GLYPH_ZE);
  OLED_DrawChinese(58U, 2U, OLED_GLYPH_DI);
  OLED_DrawDigit(80U, 3U, problem);
  OLED_DrawChinese(92U, 2U, OLED_GLYPH_TI);
  OLED_Update();
}

void OLED_DrawProblemSelected(uint8_t problem)
{
  OLED_Clear();
  OLED_DrawChinese(12U, 2U, OLED_GLYPH_YI);
  OLED_DrawChinese(30U, 2U, OLED_GLYPH_JING);
  OLED_DrawChinese(48U, 2U, OLED_GLYPH_XUAN);
  OLED_DrawChinese(66U, 2U, OLED_GLYPH_ZE);
  OLED_DrawChinese(22U, 5U, OLED_GLYPH_DI);
  OLED_DrawDigit(44U, 6U, problem);
  OLED_DrawChinese(56U, 5U, OLED_GLYPH_TI);
  OLED_Update();
}

static bool OLED_WriteCommand(uint8_t cmd)
{
  uint8_t data[2];

  data[0] = 0x00U;
  data[1] = cmd;

  return Board_I2C_Write(g_oled.address, data, (uint16_t)sizeof(data));
}

static bool OLED_WriteCommands(const uint8_t *cmds, uint16_t len)
{
  uint16_t index = 0U;

  if ((cmds == (const uint8_t *)0) || (len == 0U))
  {
    return false;
  }

  for (index = 0U; index < len; index++)
  {
    if (!OLED_WriteCommand(cmds[index]))
    {
      return false;
    }
  }

  return true;
}

static bool OLED_WriteData(const uint8_t *data, uint16_t len)
{
  uint8_t packet[8];
  uint16_t offset = 0U;

  if ((data == (const uint8_t *)0) || (len == 0U))
  {
    return false;
  }

  packet[0] = 0x40U;

  while (offset < len)
  {
    uint8_t chunk = (uint8_t)((len - offset) > 7U ? 7U : (len - offset));
    uint8_t index = 0U;

    for (index = 0U; index < chunk; index++)
    {
      packet[1U + index] = data[offset + index];
    }

    if (!Board_I2C_Write(g_oled.address, packet, (uint16_t)(chunk + 1U)))
    {
      return false;
    }

    offset = (uint16_t)(offset + chunk);
  }

  return true;
}

static bool OLED_SelectAddress(uint8_t address)
{
  g_oled.address = address;
  g_oled.present = 1U;

  if (OLED_WriteCommand(0xAEU))
  {
    return true;
  }

  g_oled.present = 0U;
  return false;
}

static void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
  uint16_t index = 0U;
  uint8_t mask = 0U;

  if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
  {
    return;
  }

  index = ((uint16_t)(y / 8U) * OLED_WIDTH) + x;
  mask = (uint8_t)(1U << (y & 0x07U));

  if (on != 0U)
  {
    s_buffer[index] |= mask;
  }
  else
  {
    s_buffer[index] &= (uint8_t)~mask;
  }
}

static void OLED_DrawDigit(uint8_t x, uint8_t page, uint8_t digit)
{
  uint8_t col = 0U;

  if ((digit > 9U) || (page >= 8U) || (x > (OLED_WIDTH - 10U)))
  {
    return;
  }

  for (col = 0U; col < 5U; col++)
  {
    uint8_t bits = s_font5x7[digit][col];
    uint8_t row = 0U;

    for (row = 0U; row < 7U; row++)
    {
      uint8_t on = ((bits & (1U << row)) != 0U) ? 1U : 0U;
      OLED_SetPixel((uint8_t)(x + (col * 2U)), (uint8_t)((page * 8U) + (row * 2U)), on);
      OLED_SetPixel((uint8_t)(x + (col * 2U) + 1U), (uint8_t)((page * 8U) + (row * 2U)), on);
      OLED_SetPixel((uint8_t)(x + (col * 2U)), (uint8_t)((page * 8U) + (row * 2U) + 1U), on);
      OLED_SetPixel((uint8_t)(x + (col * 2U) + 1U), (uint8_t)((page * 8U) + (row * 2U) + 1U), on);
    }
  }
}
