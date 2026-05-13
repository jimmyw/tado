# Alarm Door Sensor — RF Protocol

## Radio Configuration

| Parameter        | Value              |
|------------------|--------------------|
| Frequency        | 868.0 MHz          |
| Modulation       | 2-FSK              |
| Data rate        | 1.2 kbps           |
| Deviation        | 5.2 kHz            |
| RX bandwidth     | 58 kHz             |
| TX power         | +10 dBm (0xC0)     |
| Crystal          | 26 MHz             |

## Packet Format (CC1101 built-in)

| Field            | Length   | Notes                                         |
|------------------|----------|-----------------------------------------------|
| Preamble         | 4 bytes  | 0xAA pattern, inserted by CC1101              |
| Sync word        | 4 bytes  | 0xD391D391 (CC1101 default, 30/32 bit detect) |
| Length byte      | 1 byte   | Payload length (= 9)                          |
| Payload          | 9 bytes  | Application data (see below)                  |
| CRC-16           | 2 bytes  | Computed and appended by CC1101               |

Data whitening is **disabled** (PKTCTRL0 bit 6 = 0).

## Payload Structure (9 bytes)

| Offset | Size   | Field        | Description                                          |
|--------|--------|--------------|------------------------------------------------------|
| 0–3    | 4 bytes| Device ID    | Unique device identifier (hardcoded per sensor)      |
| 4      | 1 byte | Tamper       | 0 = closed (OK), 1 = open (tampered)                 |
| 5      | 1 byte | Reed switch  | 0 = closed (window shut), 1 = open (window open)     |
| 6–7    | 2 bytes| Battery mV   | Supply voltage in millivolts, big-endian              |
| 8      | 1 byte | Checksum     | XOR of bytes 0–7                                     |

## Event Types

The sensor transmits a packet on:

- **State change** — reed or tamper switch changes state (polled every 500 ms)
- **Heartbeat** — periodic keep-alive every 5 minutes

All event types use the same packet structure. The receiver detects changes by comparing successive packets from the same Device ID.

## CC1101 Register Settings

```
IOCFG0   = 0x06   GDO0: sync word sent/received
FIFOTHR  = 0x47   RX FIFO threshold 33 bytes
PKTCTRL0 = 0x05   Variable length, CRC enabled, whitening disabled
PKTCTRL1 = 0x04   Append status (RSSI+LQI+CRC_OK), no address check
FSCTRL1  = 0x06   IF frequency 152.3 kHz
FREQ2    = 0x21   868.0 MHz
FREQ1    = 0x62
FREQ0    = 0x76
MDMCFG4  = 0xF5   RX BW 58 kHz, DRATE_E=5
MDMCFG3  = 0x83   DRATE_M=131 → 1.2 kbps
MDMCFG2  = 0x13   2-FSK, 30/32 sync word detect
MDMCFG1  = 0x22   4-byte preamble, FEC off
DEVIATN  = 0x15   5.2 kHz deviation
MCSM0    = 0x18   Auto-cal from IDLE→RX/TX
FOCCFG   = 0x16   Freq offset compensation
BSCFG    = 0x6C   Bit sync config
AGCCTRL2 = 0x03
AGCCTRL1 = 0x40
AGCCTRL0 = 0x91
FREND1   = 0xB6   Front-end RX config
FREND0   = 0x10   Front-end TX, PA index 0
FSCAL3   = 0xE9   Frequency synthesizer calibration
FSCAL2   = 0x2A
FSCAL1   = 0x00
FSCAL0   = 0x1F
TEST2    = 0x81
TEST1    = 0x35
TEST0    = 0x09
PA_TABLE = 0xC0   +10 dBm (868 MHz)
```

## Receiver Notes

1. Load the register settings above into a CC1101
2. Enter RX mode (`STX` strobe 0x34 → `SRX`)
3. Wait for GDO0 to assert (sync word detected), then deassert (end of packet)
4. Read length byte from RX FIFO, then that many payload bytes + 2 status bytes
5. Status byte 1 = RSSI, status byte 2 bits [6:0] = LQI, bit 7 = CRC_OK
6. Discard packets where CRC_OK = 0
7. Verify application checksum (XOR of payload bytes 0–5 == byte 6)
8. Key on Device ID (bytes 0–3) to track per-sensor state
