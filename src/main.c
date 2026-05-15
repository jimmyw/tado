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

#define MAX_SENSORS 16
#define PAYLOAD_LEN_OLD 7
#define PAYLOAD_LEN_NEW 9

typedef struct sensor_state {
  uint32_t device_id;

  // Values from last packet (0 or 1, or 0xFF if unknown)
  uint8_t tamper;
  uint8_t reed;
  uint16_t battery_mv;

  // Values last sent to HA, for change detection
  uint8_t tamper_sent;
  uint8_t reed_sent;
  uint16_t battery_mv_sent;

  // Additional for metrics
  int rssi;
  uint8_t lqi;

} sensor_state_t;

static sensor_state_t sensors[MAX_SENSORS];
static int sensor_count;

static sensor_state_t *find_or_add_sensor(uint32_t dev_id) {
  for (int i = 0; i < sensor_count; i++) {
    if (sensors[i].device_id == dev_id) {
      return &sensors[i];
    }
  }
  if (sensor_count < MAX_SENSORS) {
    sensor_state_t *s = &sensors[sensor_count];
    s->device_id = dev_id;
    s->tamper = 0xFF; /* force first update */
    s->reed = 0xFF;
    s->battery_mv = 0xFFFF;
    s->tamper_sent = 0xFF;
    s->reed_sent = 0xFF;
    s->battery_mv_sent = 0xFFFF;

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

  sensor_state_t *s = find_or_add_sensor(dev_id);
  if (!s) {
    LOG_WRN("Sensor table full, ignoring %08x", dev_id);
    return;
  }

  s->tamper = payload[4];
  s->reed = payload[5];

  if (len == PAYLOAD_LEN_NEW) {
    s->battery_mv = ((uint16_t)payload[6] << 8) | payload[7];
  }
  s->rssi = rssi;
  s->lqi = lqi;

  LOG_INF("Sensor %08x: tamper=%u reed=%u batt=%umV RSSI=%d LQI=%u", dev_id,
          s->tamper, s->reed, s->battery_mv, s->rssi, s->lqi);
}

static void sync_sensors_to_ha(void) {
  for (int i = 0; i < sensor_count; i++) {
    sensor_state_t *s = &sensors[i];

    if (s->tamper != s->tamper_sent) {
      int ret = ha_update_sensor(s->device_id, "tamper", s->tamper != 0,
                                 s->rssi, s->lqi);
      if (ret < 0) {
        LOG_WRN("Failed to update %08x_%s: %d", s->device_id, "tamper", ret);
      } else {
        s->tamper_sent = s->tamper;
      }
    }

    if (s->reed != s->reed_sent) {
      int ret =
          ha_update_sensor(s->device_id, "reed", s->reed != 0, s->rssi, s->lqi);
      if (ret < 0) {
        LOG_WRN("Failed to update %08x_%s: %d", s->device_id, "reed", ret);
      } else {
        s->reed_sent = s->reed;
      }
    }

    if (s->battery_mv != s->battery_mv_sent) {
      int ret = ha_update_battery(s->device_id, s->battery_mv, s->rssi, s->lqi);
      if (ret < 0) {
        LOG_WRN("Failed to update %08x_%s: %d", s->device_id, "battery_mv",
                ret);
      } else {
        s->battery_mv_sent = s->battery_mv;
      }
    }
  }
}

static struct net_mgmt_event_callback mgmt_cb;
static bool got_ip = false;

static void net_event_handler(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface) {
  if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND) {
    char buf[NET_IPV4_ADDR_LEN];

    LOG_INF("DHCP bound: %s",
            net_addr_ntop(AF_INET, &iface->config.dhcpv4.requested_ip, buf,
                          sizeof(buf)));
    got_ip = true;
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
  LOG_INF("C1101 OK");

  /* Enter RX mode */
  cc1101_set_rx();
  LOG_INF("Listening for packets...");

  while (1) {

    // Read data from radio, block max one second so we do reattempts on sending
    // sensor data in-between
    if (cc1101_wait_rx_packet(K_SECONDS(1)) == 0) {
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
    } else {
      LOG_DBG("No packet received");
    }

    if (got_ip) {

      if (!ha_is_registered()) {
        /* Register with Home Assistant */
        LOG_INF("Registering with Home Assistant...");
        int ha_ret = ha_init();
        if (ha_ret < 0) {
          LOG_WRN("HA registration failed: %d (will retry)", ha_ret);
        } else {
          LOG_INF("HA registered, waiting for sensor data...");
        }
      }

      if (ha_is_registered()) {
        sync_sensors_to_ha();
      }
    }

    k_msleep(1);
  }
  return 0;
}
