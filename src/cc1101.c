/*
 * CC1101 Sub-1GHz RF Transceiver — Zephyr SPI driver
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "cc1101.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

/* ── SPI access constants ───────────────────────────────────────────── */
#define WRITE_BURST   0x40
#define READ_SINGLE   0x80
#define READ_BURST    0xC0
#define BYTES_IN_RXFIFO 0x7F

/* ── Devicetree SPI spec ────────────────────────────────────────────── */
static struct spi_dt_spec cc1101_spi = SPI_DT_SPEC_GET(
	DT_NODELABEL(cc1101),
	SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
	0);

/* ── Low-level SPI helpers ──────────────────────────────────────────── */

static void cc1101_strobe(uint8_t strobe)
{
	uint8_t tx = strobe;
	struct spi_buf tx_buf = { .buf = &tx, .len = 1 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	spi_write_dt(&cc1101_spi, &tx_set);
}

void cc1101_write_reg(uint8_t addr, uint8_t value)
{
	uint8_t tx[2] = { addr, value };
	struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	spi_write_dt(&cc1101_spi, &tx_set);
}

static void cc1101_write_burst(uint8_t addr, const uint8_t *data, uint8_t len)
{
	uint8_t hdr = addr | WRITE_BURST;
	struct spi_buf tx_bufs[2] = {
		{ .buf = &hdr, .len = 1 },
		{ .buf = (void *)data, .len = len },
	};
	struct spi_buf_set tx_set = { .buffers = tx_bufs, .count = 2 };

	spi_write_dt(&cc1101_spi, &tx_set);
}

uint8_t cc1101_read_reg(uint8_t addr)
{
	uint8_t tx[2] = { addr | READ_SINGLE, 0x00 };
	uint8_t rx[2] = { 0 };
	struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx, .len = 2 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	spi_transceive_dt(&cc1101_spi, &tx_set, &rx_set);
	return rx[1];
}

uint8_t cc1101_read_status_reg(uint8_t addr)
{
	uint8_t tx[2] = { (uint8_t)(addr | READ_BURST), 0x00 };
	uint8_t rx[2] = { 0 };
	struct spi_buf tx_buf = { .buf = tx, .len = 2 };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx, .len = 2 };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	spi_transceive_dt(&cc1101_spi, &tx_set, &rx_set);
	return rx[1];
}

static void cc1101_read_burst(uint8_t addr, uint8_t *buf, uint8_t len)
{
	uint8_t hdr = addr | READ_BURST;

	/* TX: header + len dummy bytes */
	uint8_t tx_dummy[65];

	tx_dummy[0] = hdr;
	memset(&tx_dummy[1], 0, len);

	uint8_t rx_all[65];

	struct spi_buf tx_buf = { .buf = tx_dummy, .len = (size_t)(1 + len) };
	struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf rx_buf = { .buf = rx_all, .len = (size_t)(1 + len) };
	struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

	spi_transceive_dt(&cc1101_spi, &tx_set, &rx_set);
	memcpy(buf, &rx_all[1], len);
}

/* ── CC1101 register configuration (matches transmitter exactly) ───── */

static const uint8_t cc1101_config[][2] = {
	{ CC1101_IOCFG0,   0x06 }, /* GDO0: asserts on sync, deasserts end-of-packet */
	{ CC1101_FIFOTHR,  0x47 }, /* RX FIFO threshold 33 bytes */
	{ CC1101_PKTCTRL0, 0x05 }, /* Variable length, CRC on, whitening off */
	{ CC1101_PKTCTRL1, 0x04 }, /* Append status (RSSI+LQI), no addr check */
	{ CC1101_FSCTRL1,  0x06 }, /* IF frequency 152.3 kHz */
	{ CC1101_FSCTRL0,  0x00 }, /* No frequency offset */
	{ CC1101_FREQ2,    0x21 }, /* 868.0 MHz */
	{ CC1101_FREQ1,    0x62 },
	{ CC1101_FREQ0,    0x76 },
	{ CC1101_MDMCFG4,  0xF5 }, /* RX BW 58 kHz, DRATE_E=5 */
	{ CC1101_MDMCFG3,  0x83 }, /* DRATE_M=131 -> 1.2 kbps */
	{ CC1101_MDMCFG2,  0x13 }, /* GFSK, 30/32 sync detect */
	{ CC1101_MDMCFG1,  0x22 }, /* 4-byte preamble, FEC off */
	{ CC1101_DEVIATN,  0x15 }, /* 5.2 kHz deviation */
	{ CC1101_MCSM0,    0x18 }, /* Auto-cal from IDLE->RX/TX */
	{ CC1101_FOCCFG,   0x16 }, /* Freq offset compensation */
	{ CC1101_BSCFG,    0x6C }, /* Bit sync config */
	{ CC1101_AGCCTRL2, 0x03 },
	{ CC1101_AGCCTRL1, 0x40 },
	{ CC1101_AGCCTRL0, 0x91 },
	{ CC1101_FREND1,   0xB6 }, /* Front-end RX */
	{ CC1101_FREND0,   0x10 }, /* Front-end TX, PA index 0 */
	{ CC1101_FSCAL3,   0xE9 }, /* Freq synth cal */
	{ CC1101_FSCAL2,   0x2A },
	{ CC1101_FSCAL1,   0x00 },
	{ CC1101_FSCAL0,   0x1F },
	{ CC1101_FSTEST,   0x59 },
	{ CC1101_TEST2,    0x81 },
	{ CC1101_TEST1,    0x35 },
	{ CC1101_TEST0,    0x09 },
	{ CC1101_SYNC1,    0xD3 }, /* Sync word 0xD391 */
	{ CC1101_SYNC0,    0x91 },
	{ CC1101_ADDR,     0x00 },
	{ CC1101_PKTLEN,   0xFF }, /* Max packet length */
};

/* PA table: +10 dBm at 868 MHz */
static const uint8_t pa_table[8] = { 0xC0, 0x00, 0x00, 0x00,
				      0x00, 0x00, 0x00, 0x00 };

/* ── Public API ─────────────────────────────────────────────────────── */

int cc1101_init(void)
{
	if (!spi_is_ready_dt(&cc1101_spi)) {
		printk("SPI bus not ready\n");
		return -ENODEV;
	}

	/* Reset */
	cc1101_strobe(CC1101_SRES);
	k_msleep(10);

	/* Check chip presence (VERSION register should be non-zero) */
	uint8_t ver = cc1101_read_status_reg(CC1101_VERSION);
	if (ver == 0x00 || ver == 0xFF) {
		printk("CC1101 not detected (VERSION=0x%02X)\n", ver);
		return -ENODEV;
	}
	printk("CC1101 VERSION=0x%02X\n", ver);

	/* Load register configuration */
	for (size_t i = 0; i < ARRAY_SIZE(cc1101_config); i++) {
		cc1101_write_reg(cc1101_config[i][0], cc1101_config[i][1]);
	}

	/* Load PA table */
	cc1101_write_burst(CC1101_PATABLE, pa_table, sizeof(pa_table));

	return 0;
}

void cc1101_set_rx(void)
{
	cc1101_strobe(CC1101_SIDLE);
	cc1101_strobe(CC1101_SFRX);
	cc1101_strobe(CC1101_SRX);
}

bool cc1101_check_rx_fifo(int timeout_ms)
{
	uint8_t rxbytes = cc1101_read_status_reg(CC1101_RXBYTES);
	if (rxbytes & BYTES_IN_RXFIFO) {
		k_msleep(timeout_ms);
		return true;
	}
	return false;
}

bool cc1101_check_crc(void)
{
	uint8_t lqi = cc1101_read_status_reg(CC1101_LQI);
	bool crc_ok = (lqi >> 7) & 0x01;
	if (crc_ok) {
		return true;
	}
	cc1101_strobe(CC1101_SFRX);
	cc1101_strobe(CC1101_SRX);
	return false;
}

uint8_t cc1101_receive_data(uint8_t *buffer)
{
	uint8_t rxbytes = cc1101_read_status_reg(CC1101_RXBYTES);
	if (!(rxbytes & BYTES_IN_RXFIFO)) {
		cc1101_strobe(CC1101_SFRX);
		cc1101_strobe(CC1101_SRX);
		return 0;
	}

	uint8_t len = cc1101_read_reg(CC1101_RXFIFO);
	if (len > 64) {
		/* Overflow / garbage — flush */
		cc1101_strobe(CC1101_SFRX);
		cc1101_strobe(CC1101_SRX);
		return 0;
	}

	cc1101_read_burst(CC1101_RXFIFO, buffer, len);

	/* Read & discard the 2 appended status bytes */
	uint8_t status[2];
	cc1101_read_burst(CC1101_RXFIFO, status, 2);

	cc1101_strobe(CC1101_SFRX);
	cc1101_strobe(CC1101_SRX);
	return len;
}

int cc1101_get_rssi(void)
{
	int rssi = cc1101_read_status_reg(CC1101_RSSI);
	if (rssi >= 128) {
		rssi = (rssi - 256) / 2 - 74;
	} else {
		rssi = rssi / 2 - 74;
	}
	return rssi;
}

uint8_t cc1101_get_lqi(void)
{
	return cc1101_read_status_reg(CC1101_LQI);
}
