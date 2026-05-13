/*
 * Home Assistant Mobile App integration client
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef HA_CLIENT_H
#define HA_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Register this device with Home Assistant and register all sensor
 * types. Must be called once after network is up.
 *
 * @return 0 on success, negative errno on failure
 */
int ha_init(void);

/**
 * Update a binary sensor state in Home Assistant.
 *
 * @param dev_id     4-byte device ID from the RF packet
 * @param suffix     "tamper" or "reed"
 * @param state      true = on, false = off
 * @param rssi       RSSI value
 * @param lqi        LQI value
 * @return 0 on success, negative errno on failure
 */
int ha_update_sensor(uint32_t dev_id, const char *suffix, bool state, int rssi,
                     uint8_t lqi);

/**
 * Update the battery voltage sensor in Home Assistant.
 *
 * @param dev_id      4-byte device ID from the RF packet
 * @param battery_mv  Battery voltage in millivolts
 * @param rssi        RSSI value
 * @param lqi         LQI value
 * @return 0 on success, negative errno on failure
 */
int ha_update_battery(uint32_t dev_id, uint16_t battery_mv, int rssi,
                      uint8_t lqi);

#endif /* HA_CLIENT_H */
