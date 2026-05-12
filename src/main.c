/*
 * TADO CC1101 Receiver — Zephyr application
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "cc1101.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static uint8_t buffer[64];

int main(void) {
  LOG_INF("TADO CC1101 Receiver");

  int ret = cc1101_init();
  if (ret < 0) {
    LOG_ERR("CC1101 init failed: %d", ret);
    return ret;
  }
  LOG_INF("Connection OK");

  /* Enter RX mode */
  cc1101_set_rx();
  LOG_INF("Listening for packets...");

  while (1) {
    if (cc1101_check_rx_fifo(100)) {
      if (cc1101_check_crc()) {
        uint8_t len = cc1101_receive_data(buffer);
        if (len > 0) {
          LOG_INF("LENGTH: %d RSSI: %d LQI: %d", len, cc1101_get_rssi(),
                  cc1101_get_lqi());

          LOG_HEXDUMP_INF(buffer, len, "RX data");
        }
      }
    }
    k_yield();
  }
  return 0;
}
