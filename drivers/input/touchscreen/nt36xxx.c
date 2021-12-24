// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Novatek NT36xxx series touchscreens
 *
 * Copyright (C) 2010 - 2017 Novatek, Inc.
 * Copyright (C) 2020 AngeloGioacchino Del Regno <kholk11@gmail.com>
 */

#include <linux/module.h>
#include <linux/spi/spi.h>

static int nt36xxx_spi_probe(struct spi_device *client)
{
  int ret = 0;
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
