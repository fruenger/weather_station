/*
 * nRF24L01 Minimal Transmitter Test
 *
 * Sends 32-byte packets (16 x int16_t) matching receive.ino / weather_station.ino.
 * Use with receive.ino on the receiver Arduino.
 *
 * Serial Monitor: 9600 baud
 */

#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define RADIO_CE_PIN 9
#define RADIO_CSN_PIN 8
#define RADIO_CHANNEL 76
static const uint8_t RF_ADDRESS[5] = {'9', '9', '9', '9', '9'};

RF24 radio(RADIO_CE_PIN, RADIO_CSN_PIN);

int16_t payload[16];
uint16_t packetNumber = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  Serial.println(F("nRF24L01 Transmit Test"));
  Serial.println(F("======================"));

  if (!radio.begin()) {
    Serial.println(F("ERROR: RF24 begin() failed!"));
    while (true) {
      delay(1000);
    }
  }

  // Must match receive.ino exactly
  radio.setAddressWidth(5);
  radio.setDataRate(RF24_1MBPS);
  radio.setChannel(RADIO_CHANNEL);
  radio.setPayloadSize(32);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setPALevel(RF24_PA_HIGH);
  radio.setRetries(5, 15);

  radio.openWritingPipe(RF_ADDRESS);
  radio.flush_tx();
  radio.stopListening();

  if (radio.isChipConnected()) {
    Serial.println(F("RF24 chip detected, transmitting on channel 76"));
  } else {
    Serial.println(F("WARNING: RF24 chip not detected — check wiring and 3.3V power"));
  }

  Serial.println(F("Sending 32-byte packets every second..."));
  Serial.println();
}

void loop() {
  // Fill test payload (same layout as weather_station.ino)
  payload[0] = (int16_t)(millis() / 1000);  // pseudo timestamp
  payload[1] = 2345;   // 23.45 C scaled
  payload[2] = 10132;  // 1013.2 hPa scaled
  payload[3] = 5000;   // 50.00 % humidity scaled
  payload[7] = (int16_t)packetNumber;

  for (int i = 4; i < 16; i++) {
    if (i != 7) {
      payload[i] = 0;
    }
  }

  if (radio.write(&payload, sizeof(payload))) {
    Serial.print(F("Packet "));
    Serial.print(packetNumber);
    Serial.println(F(" transmitted successfully"));
  } else {
    Serial.print(F("Packet "));
    Serial.print(packetNumber);
    Serial.println(F(" FAILED (no ACK from receiver)"));
  }

  packetNumber++;
  delay(1000);
}
