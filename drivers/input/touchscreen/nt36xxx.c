// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Novatek NT36xxx series touchscreens
 *
 * Copyright (C) 2010 - 2017 Novatek, Inc.
 * Copyright (C) 2020 AngeloGioacchino Del Regno <kholk11@gmail.com>
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regulator/consumer.h>
#include <linux/input/mt.h>
#include <linux/regmap.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
//#include <linux/input/touchscreen.h>

/* Number of bytes for chip identification */
#define NT36XXX_ID_LEN_MAX 6

/* Misc */
#define NT36XXX_NUM_SUPPLIES 2
#define NT36XXX_MAX_RETRIES 5

/* Global pages */
#define NT36XXX_PAGE_CHIP_INFO 0x3F004

uint32_t SWRST_N8_ADDR = 0x03F0FE;
uint32_t ENG_RST_ADDR = 0x7FFF80;

#define TOUCH_DEFAULT_MAX_WIDTH 1600
#define TOUCH_DEFAULT_MAX_HEIGHT 2560

struct nt36xxx_mem_map {
  u32 evtbuf_addr;
	u32 pipe0_addr;
	u32 pipe1_addr;
	u32 flash_csum_addr;
	u32 flash_data_addr;
};

struct nt36xxx_spi {
	struct spi_device *client;
	struct regmap *regmap;
	struct input_dev *input;
	//struct regulator_bulk_data *supplies;
	//struct gpio_desc *reset_gpio;
  int32_t irq_gpio;
  uint32_t irq_flags;

	struct mutex lock;

	//struct touchscreen_properties prop;
	//struct nt36xxx_fw_info fw_info;
	//struct nt36xxx_abs_object abs_obj;

	const struct nt36xxx_mem_map *mmap;

  uint16_t abs_x_max;
	uint16_t abs_y_max;
};

enum nt36xxx_chips {
  NT36523_IC = 0,
};

struct nt36xxx_trim_table {
  u8 id[NT36XXX_ID_LEN_MAX];
  u8 mask[NT36XXX_ID_LEN_MAX];
  enum nt36xxx_chips mapid;
};

static const struct nt36xxx_mem_map nt36xxx_memory_maps[] = {
  [NT36523_IC]  = { 0x2fe00, 0x30fa0, 0x30fa0, 0x24000, 0x24002 },
};

static const struct nt36xxx_trim_table trim_id_table[] = {
  {
    .id = {0x20, 0xFF, 0xFF, 0x23, 0x65, 0x03},
    .mask = { 1, 0, 0, 1, 1, 1 },
    .mapid = NT36523_IC,
  },
  {
    .id = {0x0C, 0xFF, 0xFF, 0x23, 0x65, 0x03},
    .mask = {1, 0, 0, 1, 1, 1},
    .mapid = NT36523_IC,
  },
  {
    .id = {0x0B, 0xFF, 0xFF, 0x23, 0x65, 0x03},
    .mask = {1, 0, 0, 1, 1, 1},
    .mapid = NT36523_IC,
  },
  {
    .id = {0x0A, 0xFF, 0xFF, 0x23, 0x65, 0x03},
    .mask = {1, 0, 0, 1, 1, 1},
    .mapid = NT36523_IC,
  },
  {
    .id = {0xFF, 0xFF, 0xFF, 0x23, 0x65, 0x03},
    .mask = {0, 0, 0, 1, 1, 1},
    .mapid = NT36523_IC,
  },
};

enum nt36xxx_cmd {
  NT36XXX_CMD_BOOTLOADER_RESET = 0x69,
  NT36XXX_CMD_ENG_RESET = 0x59,
  NT36XXX_CMD_SW_RESET = 0xaa,
  NT36XXX_CMD_SET_PAGE = 0xff,
};

static const struct regmap_config nt36xxx_spi_regmap_config = {
	.name = "nt36xxx_spi",
	.reg_bits = 8,
	.val_bits = 8,
	.cache_type = REGCACHE_NONE,
};

/**
 * nt36xxx_bootloader_reset - Reset MCU to bootloader
 * @ts: Main driver structure
 *
 * Return: Always zero for success, negative number for error
 */
static int nt36xxx_bootloader_reset(struct nt36xxx_spi *ts)
{
  int ret;

  ret = regmap_write(ts->regmap, SWRST_N8_ADDR,
          NT36XXX_CMD_BOOTLOADER_RESET);

  if (ret)
    return ret;

  mdelay(5);
  return ret;
}

static int nt36xxx_eng_reset(struct nt36xxx_spi *ts)
{
  int ret;

  ret = regmap_write(ts->regmap, ENG_RST_ADDR,
      NT36XXX_CMD_ENG_RESET);

  if (ret)
    return ret;
  
  mdelay(1);
  return ret;
}

#if 0
static int nt36xxx_sw_reset_idle(struct nt36xxx_spi *ts)
{
  int ret;

  ret = regmap_write(ts->regmap, SWRST_N8_ADDR,
      NT36XXX_CMD_SW_RESET);
  if (ret)
    return ret;

  /* Wait until the MCU resets the fw state */
	usleep_range(15000, 16000);
	return ret;
}
#endif

/**
 * nt36xxx_set_page - Set page number for read/write
 * @ts: Main driver structure
 *
 * Return: Always zero for success, negative number for error
 */
static int nt36xxx_set_page(struct nt36xxx_spi *ts, u32 pageaddr)
{
  u32 data = cpu_to_be32(pageaddr) >> 7;
  int ret;

  ret = regmap_noinc_write(ts->regmap, NT36XXX_CMD_SET_PAGE,
      &data, sizeof(data));
  if (ret)
    return ret;

  usleep_range(100, 200);
  return ret;
}

/**
 * nt36xxx_i2c_chip_version_init - Detect Novatek NT36xxx family IC
 * @ts: Main driver structure
 *
 * This function reads the ChipID from the IC and sets the right
 * memory map for the detected chip.
 *
 * Return: Always zero for success, negative number for error
 */
static int nt36xxx_spi_chip_version_init(struct nt36xxx_spi *ts)
{
  u8 buf[7] = { 0 };
  int retry = NT36XXX_MAX_RETRIES;
  int sz = sizeof(trim_id_table) / sizeof(struct nt36xxx_trim_table);
  int i, list, mapid, ret;

  do {
    ret = nt36xxx_bootloader_reset(ts);
    if (ret) {
      dev_err(&ts->client->dev, "Can't reset the nvt IC\n");
      return ret;
    }

    ret = nt36xxx_set_page(ts, NT36XXX_PAGE_CHIP_INFO);
    if (ret)
      continue;

    memset(buf, 0, ARRAY_SIZE(buf));
    ret = regmap_noinc_read(ts->regmap, NT36XXX_PAGE_CHIP_INFO,
        buf, sizeof(buf));
    if (ret)
      continue;

    /* Compare read chip id with trim list */
		for (list = 0; list < sz; list++) {
			/* Compare each not masked byte */
			for (i = 0; i < NT36XXX_ID_LEN_MAX; i++) {
				if (trim_id_table[list].mask[i] &&
				    buf[i] != trim_id_table[list].id[i])
					break;
			}

			if (i == NT36XXX_ID_LEN_MAX) {
				mapid = trim_id_table[list].mapid;
				ts->mmap = &nt36xxx_memory_maps[mapid];
				return 0;
			}

			ts->mmap = NULL;
			ret = -ENOENT;
		}

    usleep_range(10000, 11000);
  }while(--retry);

  return ret;
}

//static void nt36xxx_disable_regulators(void *data)
//{
//	struct nt36xxx_spi *ts = data;
//
//	regulator_bulk_disable(NT36XXX_NUM_SUPPLIES, ts->supplies);
//}

static int nt36xxx_spi_probe(struct spi_device *client)
{
  struct nt36xxx_spi *ts;
  struct input_dev *input;
  int ret;

  if (!client->irq) {
    dev_err(&client->dev, "No irq specified\n");
    return -EINVAL;
  }

  ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
  if (!ts)
    return -ENOMEM;

  //ts->supplies = devm_kcalloc(&client->dev,
  //          NT36XXX_NUM_SUPPLIES,
  //          sizeof(*ts->supplies),
  //          GFP_KERNEL);
  //if (!ts->supplies)
  //  return -ENOMEM;

  input = devm_input_allocate_device(&client->dev);

  ts->client = client;
  ts->input = input;
  spi_set_drvdata(ts->client, ts);
  ts->client->bits_per_word = 8;
  ts->client->mode = SPI_MODE_0;

  ret = spi_setup(ts->client);
  if (ret) {
    dev_err(&ts->client->dev, "Failed to setup spi, %d\n", ret);
    return ret;
  }


  ts->irq_gpio = of_get_named_gpio_flags(ts->client->dev.of_node, "novatek,irq-gpio", 0, &ts->irq_flags);

  /* request INT-pin (Input) */
	if (gpio_is_valid(ts->irq_gpio)) {
		ret = gpio_request_one(ts->irq_gpio, GPIOF_IN, "NVT-int");
		if (ret) {
			dev_err(&ts->client->dev, "Failed to request NVT-int GPIO\n");
      return ret;
		}
	}

  //ts->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
  //            GPIOD_OUT_HIGH);
  //if (IS_ERR(ts->reset_gpio))
  //  return PTR_ERR(ts->reset_gpio);
  //gpiod_set_consumer_name(ts->reset_gpio, "nt36xxx reset");

  /* These supplies are optional */
  //ts->supplies[0].supply = "vdd";
  //ts->supplies[1].supply = "vio";
  //ret = devm_regulator_bulk_get(&client->dev,
  //            NT36XXX_NUM_SUPPLIES,
  //            ts->supplies);
  //if (ret)
  //  return dev_err_probe(&ts->client->dev, ret,
  //            "Cannot get supplies: %d\n", ret);

  ts->regmap = devm_regmap_init_spi(ts->client,
            &nt36xxx_spi_regmap_config);
  if (IS_ERR(ts->regmap)) {
    dev_err(&ts->client->dev, "regmap init failed\n");
    return PTR_ERR(ts->regmap);
  }

  //ret = regulator_bulk_enable(NT36XXX_NUM_SUPPLIES, ts->supplies);
  //if (ret)
  //  return ret;

  usleep_range(10000, 11000);

  //ret = devm_add_action_or_reset(&client->dev,
   //             nt36xxx_disable_regulators, ts);

  //if (ret)
  //  return ret;

  mutex_init(&ts->lock);

  nt36xxx_eng_reset(ts);

  msleep(10);

  /* Set memory maps for the specific chip version */
  ret = nt36xxx_spi_chip_version_init(ts);
  if (ret) {
    dev_err(&ts->client->dev, "Failed to check chip version\n");
    return ret;
  }

  ts->abs_x_max = TOUCH_DEFAULT_MAX_WIDTH;
  ts->abs_y_max = TOUCH_DEFAULT_MAX_HEIGHT;

  return ret;
}

static const struct of_device_id nt36xxx_of_match[] = {
  { .compatible = "novatek,nt36523" },
  { }
};
MODULE_DEVICE_TABLE(of, nt36xxx_of_match);

static const struct spi_device_id nt36xxx_spi_ts_id[] = {
  { "NVT-ts", 0 },
  { }
};

MODULE_DEVICE_TABLE(spi, nt36xxx_spi_ts_id);

static struct spi_driver nt36xxx_spi_ts_driver = {
  .driver = {
    .name = "nt36xxx_ts",
    .of_match_table = nt36xxx_of_match,
  },
  .id_table = nt36xxx_spi_ts_id,
  .probe    = nt36xxx_spi_probe,
};

module_spi_driver(nt36xxx_spi_ts_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Novatek NT36XXX Touchscreen Driver");
MODULE_AUTHOR("AngeloGioacchino Del Regno <kholk11@gmail.com>");
