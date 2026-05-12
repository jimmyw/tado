/*
 * TADO CC1101 Receiver — Zephyr application
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cc1101.h"

static uint8_t buffer[64];

int main(void)
{
	printk("TADO CC1101 Receiver\n");

	int ret = cc1101_init();
	if (ret < 0) {
		printk("CC1101 init failed: %d\n", ret);
		return ret;
	}
	printk("Connection OK\n");

	/* Enter RX mode */
	cc1101_set_rx();
	printk("Listening for packets...\n");

	while (1) {
		if (cc1101_check_rx_fifo(100)) {
			if (cc1101_check_crc()) {
				uint8_t len = cc1101_receive_data(buffer);
				if (len > 0) {
					printk("\r\nLENGTH: %d RSSI: %d LQI: %d\r\n",
					       len, cc1101_get_rssi(),
					       cc1101_get_lqi());

					for (int i = 0; i < len; i++) {
						printk("%02X ", buffer[i]);
					}
					printk("\n");
				}
			}
		}
		k_yield();
	}
	return 0;
}
