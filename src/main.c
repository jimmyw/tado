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
#include "ha_client.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

/* ── Sensor state tracking ──────────────────────────────────────────── */

#define MAX_SENSORS 8
#define PAYLOAD_LEN_OLD 7
#define PAYLOAD_LEN_NEW 9

struct sensor_state {
  uint32_t device_id;
  uint8_t tamper;
  uint8_t reed;
  uint16_t battery_mv;
};

static struct sensor_state sensors[MAX_SENSORS];
static int sensor_count;

static struct sensor_state *find_or_add_sensor(uint32_t dev_id) {
  for (int i = 0; i < sensor_count; i++) {
    if (sensors[i].device_id == dev_id) {
      return &sensors[i];
    }
  }
  if (sensor_count < MAX_SENSORS) {
    sensors[sensor_count].device_id = dev_id;
    sensors[sensor_count].tamper = 0xFF; /* force first update */
    sensors[sensor_count].reed = 0xFF;
    return &sensors[sensor_count++];
  }
  return NULL;
}

static bool verify_checksum(const uint8_t *payload, uint8_t len) {
  uint8_t xor_val = 0;
  for (int i = 0; i < len - 1; i++) {
    xor_val ^= payload[i];
  }
  return xor_val == payload[len - 1];
}

static void post_sensor_state(uint32_t dev_id, const char *suffix, bool state,
                              int rssi, uint8_t lqi) {
  int ret = ha_update_sensor(dev_id, suffix, state, rssi, lqi);
  if (ret < 0) {
    LOG_WRN("Failed to update %08x_%s: %d", dev_id, suffix, ret);
  }
}

static void process_packet(const uint8_t *payload, uint8_t len, int rssi,
                           uint8_t lqi) {
  if (len != PAYLOAD_LEN_OLD && len != PAYLOAD_LEN_NEW) {
    LOG_WRN("Unexpected payload length: %d", len);
    return;
  }

  if (!verify_checksum(payload, len)) {
    LOG_WRN("Checksum mismatch");
    return;
  }

  uint32_t dev_id = ((uint32_t)payload[0] << 24) |
                    ((uint32_t)payload[1] << 16) | ((uint32_t)payload[2] << 8) |
                    payload[3];
  uint8_t tamper = payload[4];
  uint8_t reed = payload[5];

  uint16_t battery_mv = 0;
  if (len == PAYLOAD_LEN_NEW) {
    battery_mv = ((uint16_t)payload[6] << 8) | payload[7];
  }

  LOG_INF("Sensor %08x: tamper=%u reed=%u batt=%umV RSSI=%d LQI=%u", dev_id,
          tamper, reed, battery_mv, rssi, lqi);

  struct sensor_state *s = find_or_add_sensor(dev_id);
  if (!s) {
    LOG_WRN("Sensor table full, ignoring %08x", dev_id);
    return;
  }

  /* Post to HA on first packet or state change */
  if (s->tamper != tamper) {
    s->tamper = tamper;
    post_sensor_state(dev_id, "tamper", tamper != 0, rssi, lqi);
  }

  if (s->reed != reed) {
    s->reed = reed;
    post_sensor_state(dev_id, "reed", reed != 0, rssi, lqi);
  }

  if (len == PAYLOAD_LEN_NEW && s->battery_mv != battery_mv) {
    s->battery_mv = battery_mv;
    ha_update_battery(dev_id, battery_mv, rssi, lqi);
  }
}

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

  /* Register with Home Assistant */
  int ha_ret = ha_init();
  if (ha_ret < 0) {
    LOG_WRN("HA registration failed: %d (will retry on first sensor)", ha_ret);
  }

  /* Enter RX mode */
  cc1101_set_rx();
  LOG_INF("Listening for packets...");

  while (1) {
    if (cc1101_check_rx_fifo(100)) {
      if (cc1101_check_crc()) {
        uint8_t len = cc1101_receive_data(buffer);
        if (len > 0) {
          int rssi = cc1101_get_rssi();
          uint8_t lqi = cc1101_get_lqi();

          LOG_INF("LENGTH: %d RSSI: %d LQI: %d", len, rssi, lqi);
          LOG_HEXDUMP_INF(buffer, len, "RX data");

          process_packet(buffer, len, rssi, lqi);
        }
      }
    }
    k_yield();
  }
  return 0;
}
