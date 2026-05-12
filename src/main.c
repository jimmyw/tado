/*
 * TADO CC1101 Receiver — Zephyr application
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>

#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_mgmt.h>

#include "cc1101.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static struct net_mgmt_event_callback mgmt_cb;

static void net_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
    char buf[NET_IPV4_ADDR_LEN];

    LOG_INF("DHCP bound: %s",
            net_addr_ntop(AF_INET, &iface->config.dhcpv4.requested_ip, buf,
                          sizeof(buf)));
  }
}

static void start_dhcp(void) {
  struct net_if *iface = net_if_get_default();

  if (!iface) {
    LOG_ERR("No network interface found");
    return;
  }

  net_mgmt_init_event_callback(&mgmt_cb, net_event_handler,
                               NET_EVENT_IPV4_DHCP_BOUND);
  net_mgmt_add_event_callback(&mgmt_cb);

  net_dhcpv4_start(iface);
  LOG_INF("DHCP client started");
}

static uint8_t buffer[64];

int main(void) {
  LOG_INF("TADO CC1101 Receiver");

  /* Start DHCP to obtain an IP address */
  start_dhcp();

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
