#include <Arduino.h>
#include <SPI.h>

#include <ELECHOUSE_CC1101_SRC_DRV.h>

void setup() {
  Serial.setRx(PA10);
  Serial.setTx(PA9);
  Serial.begin(115200);
  delay(1000);
  ELECHOUSE_cc1101.setSpiPin(PA5, PA6, PA7, PA4);
  ELECHOUSE_cc1101.setGDO(PA0, PA1);
  if (ELECHOUSE_cc1101.getCC1101()) { // Check the CC1101 Spi connection.
    Serial.println("Connection OK");
  } else {
    Serial.println("Connection Error");
  }

  ELECHOUSE_cc1101.Init(); // must be set to initialize the cc1101!
  // ELECHOUSE_cc1101.setCCMode(1); // set config for internal transmission
  // mode.

  // Radio configuration per PROTOCOL.md
  ELECHOUSE_cc1101.setModulation(1);  // GFSK (transmitter uses MOD_FORMAT=001)
  ELECHOUSE_cc1101.setMHZ(868.0);     // 868.0 MHz
  ELECHOUSE_cc1101.setDeviation(5.2); // 5.2 kHz
  ELECHOUSE_cc1101.setChannel(0);
  ELECHOUSE_cc1101.setChsp(200);
  ELECHOUSE_cc1101.setRxBW(58);             // 58 kHz RX bandwidth
  ELECHOUSE_cc1101.setDRate(1.2);           // 1.2 kbps
  ELECHOUSE_cc1101.setPA(12);               // +10 dBm (PA_TABLE 0xC0)
  ELECHOUSE_cc1101.setSyncMode(3);          // 30/32 sync word detect
  ELECHOUSE_cc1101.setSyncWord(0xD3, 0x91); // 0xD391D391
  ELECHOUSE_cc1101.setAdrChk(0);            // No address check
  ELECHOUSE_cc1101.setAddr(0);
  ELECHOUSE_cc1101.setWhiteData(0);      // Data whitening disabled
  ELECHOUSE_cc1101.setPktFormat(0);      // Normal mode
  ELECHOUSE_cc1101.setLengthConfig(1);   // Variable packet length
  ELECHOUSE_cc1101.setPacketLength(255); // Max packet length
  ELECHOUSE_cc1101.setCrc(1);            // CRC enabled
  ELECHOUSE_cc1101.setCRC_AF(0);         // No auto flush on bad CRC
  ELECHOUSE_cc1101.setDcFilterOff(0);    // DC filter enabled
  ELECHOUSE_cc1101.setManchester(0);
  ELECHOUSE_cc1101.setFEC(0);
  ELECHOUSE_cc1101.setPRE(2); // 4-byte preamble
  ELECHOUSE_cc1101.setPQT(0);
  ELECHOUSE_cc1101.setAppendStatus(1); // Append RSSI+LQI+CRC_OK

  // Direct register writes per PROTOCOL.md
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FIFOTHR, 0x47); // RX FIFO threshold
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCTRL1, 0x06); // IF frequency
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FOCCFG, 0x16); // Freq offset compensation
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_BSCFG, 0x6C);  // Bit sync config
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL2, 0x03);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL1, 0x40);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_AGCCTRL0, 0x91);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FREND1, 0xB6); // Front-end RX
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FREND0, 0x10); // Front-end TX
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCAL3, 0xE9); // Freq synth cal
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCAL2, 0x2A);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCAL1, 0x00);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_FSCAL0, 0x1F);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_TEST2, 0x81);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_TEST1, 0x35);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_TEST0, 0x09);
  ELECHOUSE_cc1101.SpiWriteReg(CC1101_IOCFG0,
                               0x06); // GDO0: sync word (Init sets 0x0D)
  ELECHOUSE_cc1101.SpiWriteReg(
      CC1101_DEVIATN, 0x15); // Exact match transmitter (library gives 0x16)

  Serial.println("Rx Mode");
  ELECHOUSE_cc1101.SpiWriteReg(
      CC1101_FSCTRL0,
      0x0); // reset frequency offset to 0 (Init sets 0x08)

#if 0
  Serial.printf("Partnum: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM));
  Serial.printf("Version: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION));
  Serial.printf("FREQEST: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_FREQEST));
  Serial.printf("LQI: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_LQI));
  Serial.printf("RSSI: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_RSSI));
  Serial.printf("MARCSTATE: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE));
  Serial.printf("WORTIME1: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_WORTIME1));
  Serial.printf("WORTIME0: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_WORTIME0));
  Serial.printf("PKTSTATUS: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_PKTSTATUS));
  Serial.printf("CC1101_VCO_VC_DAC: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_VCO_VC_DAC));
  Serial.printf("TXBYTES: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_TXBYTES));
  Serial.printf("RXBYTES: %x\r\n",
                ELECHOUSE_cc1101.SpiReadStatus(CC1101_RXBYTES));
#endif

#if 1
  // Read all registers from 0x00 to 0x3F
  // for (int i = 0; i < 0x3F; i++) {
  //  Serial.printf("REG: %02X: %02X\r\n", i, ELECHOUSE_cc1101.SpiReadReg(i));
  //}
#endif
  Serial.println("Listening for packets...");
}
byte buffer[1024] = {0};

bool buffer_is_empty(int len) {
  for (int i = 0; i < len; i++) {
    if (buffer[i] != 0xff) {
      return false;
    }
  }
  return true;
}

void loop() {

  // Checks whether something has been received.
  // When something is received we give some time to receive the message in
  // full.(time in millis)
  if (ELECHOUSE_cc1101.CheckRxFifo(100)) {

    // CRC Check. If "setCrc(false)" crc returns always OK!
    if (ELECHOUSE_cc1101.CheckCRC()) {

      // Get received Data and calculate length
      int len = ELECHOUSE_cc1101.ReceiveData(buffer);

      // If any of the bytes returned by len is not 0xff
      Serial.printf("\r\nLENGTH: %d RSSI: %d LQT: %d\r\n", len,
                    ELECHOUSE_cc1101.getRssi(), ELECHOUSE_cc1101.getLqi());

      // Print received in bytes format.
      for (int i = 0; i < len; i++) {
        // if (i > 0) {
        //   Serial.print(", ");
        // }
        Serial.printf("%02X ", buffer[i]);
      }
      Serial.println();
    }
  }
}