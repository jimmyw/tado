#include <Arduino.h>

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



  ELECHOUSE_cc1101.Init();       // must be set to initialize the cc1101!
  //ELECHOUSE_cc1101.setCCMode(1); // set config for internal transmission mode.

  ELECHOUSE_cc1101.setModulation(0); // CHECK

  ELECHOUSE_cc1101.setMHZ(868.75); // CHECK

  ELECHOUSE_cc1101.setDeviation(25); // CHECK

  ELECHOUSE_cc1101.setChannel(0); // CHECK

  ELECHOUSE_cc1101.setChsp(200); // CHECK

  ELECHOUSE_cc1101.setRxBW(120); // CHECK

  ELECHOUSE_cc1101.setDRate(6); // CHECK

  ELECHOUSE_cc1101.setPA(8);

  ELECHOUSE_cc1101.setSyncMode(2); // CHECK

  ELECHOUSE_cc1101.setSyncWord(0xff, 0xfe); // CHECK

  ELECHOUSE_cc1101.setAdrChk(0); // CHECK

  ELECHOUSE_cc1101.setAddr(0); // CHECK

  ELECHOUSE_cc1101.setWhiteData(0); // CHECK

  ELECHOUSE_cc1101.setPktFormat(0); // CHECK

  ELECHOUSE_cc1101.setLengthConfig(2); // CHECK

  ELECHOUSE_cc1101.setPacketLength(0xd1); // CHECK

  ELECHOUSE_cc1101.setCrc(0); // CHECK

  ELECHOUSE_cc1101.setCRC_AF(0); // CHECK

  ELECHOUSE_cc1101.setDcFilterOff(0); // CHECK

  ELECHOUSE_cc1101.setManchester(0); // CHECK

  ELECHOUSE_cc1101.setFEC(0); // CHECK

  ELECHOUSE_cc1101.setPRE(2); // CHECK

  ELECHOUSE_cc1101.setPQT(0); // CHECK

  ELECHOUSE_cc1101.setAppendStatus(0); // CHECK

  Serial.println("Rx Mode");

  ELECHOUSE_cc1101.setModulation(1);     // CHECK
  ELECHOUSE_cc1101.setMHZ(868.44943725); // CHECK

  Serial.printf("Partnum: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_PARTNUM));
  Serial.printf("Version: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_VERSION));
  Serial.printf("FREQEST: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_FREQEST));
  Serial.printf("LQI: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_LQI));
  Serial.printf("RSSI: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_RSSI));
  Serial.printf("MARCSTATE: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_MARCSTATE));
  Serial.printf("WORTIME1: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_WORTIME1));
  Serial.printf("WORTIME0: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_WORTIME0));
  Serial.printf("PKTSTATUS: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_PKTSTATUS));
  Serial.printf("CC1101_VCO_VC_DAC: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_VCO_VC_DAC));
  Serial.printf("TXBYTES: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_TXBYTES));
  Serial.printf("RXBYTES: %x\r\n", ELECHOUSE_cc1101.SpiReadStatus(CC1101_RXBYTES));


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
      if (!buffer_is_empty(len)) {
        Serial.print("Received: ");
        Serial.print(len);
        Serial.print(" bytes: ");
        // Rssi Level in dBm
        Serial.print("Rssi: ");
        Serial.println(ELECHOUSE_cc1101.getRssi());

        // Link Quality Indicator
        Serial.print("LQI: ");
        Serial.println(ELECHOUSE_cc1101.getLqi());

        // Print received in bytes format.
        for (int i = 0; i < len; i++) {
          if (i > 0) {
            Serial.print(", ");
          }
          Serial.print("0x");
          Serial.print(buffer[i]);
        }
        Serial.println();
      }
    }
  }
}