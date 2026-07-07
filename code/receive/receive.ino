// Weather Station - Receiver Arduino
// Version: 2.2.1 (RF24 config aligned with sender)
// Features: Receives data from sender and forwards via serial to Python script
// Data Format: 16 int16_t values (32 bytes) with scaled integers

//Include Libraries
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

#define RADIO_CE_PIN 9
#define RADIO_CSN_PIN 8
#define RADIO_CHANNEL 76
static const uint8_t RF_ADDRESS[5] = {'9', '9', '9', '9', '9'};

RF24 radio(RADIO_CE_PIN, RADIO_CSN_PIN);

const byte length = 16;  // 16 int16_t values (32 bytes - nRF24L01 limit)

int16_t results[length];  // Changed from float to int16_t
uint16_t lastPacketNumber = 0;
uint16_t receivedPackets = 0;
uint16_t lostPackets = 0;

// Sync/validity management
static bool isSynced = false;
static unsigned long lastPacketMs = 0;

void setup()
{
  Serial.begin(9600);
  while (!Serial) {
    ;
  }

  if (!radio.begin()) {
    Serial.println(F("ERROR: RF24 radio begin() failed!"));
    while (true) {
      delay(1000);
    }
  }

  // Must match sender (weather_station.ino initializeRadio) exactly
  radio.setAddressWidth(5);
  radio.setDataRate(RF24_1MBPS);
  radio.setChannel(RADIO_CHANNEL);
  radio.setPayloadSize(32);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setPALevel(RF24_PA_HIGH);

  radio.openReadingPipe(0, RF_ADDRESS);
  radio.flush_rx();
  radio.flush_tx();
  radio.startListening();

  delay(500);

  Serial.println(F("Weather Station Receiver Starting..."));
  Serial.println(F("Version 2.2.1"));
  if (radio.isChipConnected()) {
    Serial.println(F("RF24 chip detected, listening on channel 76"));
  } else {
    Serial.println(F("WARNING: RF24 chip not detected — check CE/CSN wiring and 3.3V power"));
  }
}

// Helper: modulo-65536 increment check
static inline bool isNextModulo(uint16_t current, uint16_t previous) {
  return current == (uint16_t)(previous + 1);
}

void loop()
{
  //Read the data if available in buffer
  if (radio.available())
  {
    radio.read(&results, sizeof(results));
    
    // Extract packet number from results[7]
    uint16_t currentPacketNumber = (uint16_t)results[7];
    unsigned long nowMs = millis();

    // If long idle gap or not synced, accept the next packet as new sync point
    if (!isSynced || (nowMs - lastPacketMs) > 5000UL) {
      isSynced = true;
      lastPacketNumber = currentPacketNumber;
      lastPacketMs = nowMs;
      // fall through to printing this packet
    } else {
      // Validate expected sequence with wrap-around; count losses but do not drop output
      if (!isNextModulo(currentPacketNumber, lastPacketNumber)) {
        bool looksLikeReset = (currentPacketNumber <= 5);
        if (looksLikeReset) {
          // Treat as sender reset: restart sequence from this packet without counting loss
          lastPacketNumber = currentPacketNumber;
          lastPacketMs = nowMs;
        } else {
          uint16_t expected = (uint16_t)(lastPacketNumber + 1);
          uint16_t diff = (uint16_t)(currentPacketNumber - expected);
          lostPackets = (uint16_t)(lostPackets + diff);
          lastPacketNumber = currentPacketNumber;
          lastPacketMs = nowMs;
        }
      } else {
        // Normal in-order packet
        lastPacketNumber = currentPacketNumber;
        lastPacketMs = nowMs;
      }
    }

    // Sequence trackers updated above

    receivedPackets++;

    // Send data marker for Python script
    Serial.print('D');
    
    // Send all 16 int16_t values as binary data (32 bytes)
    byte *p = (byte*) results;
    for (int i = 0; i < sizeof(results); i++) {
      Serial.write(p[i]);
    }
    
    // Send end marker
    Serial.print('E');
    
    // Print debug information
    Serial.print(F("Packet "));
    Serial.print(currentPacketNumber);
    Serial.print(F(" received. Data: "));
    for (int i = 0; i < 16; i++) {  // Print all 16 values for debug
      Serial.print(results[i]);
      Serial.print(F(" "));
    }
    Serial.println();
    
    // Print particulate matter data if available
    if (results[12] > 0 || results[13] > 0 || results[14] > 0) {
      Serial.print(F("Particulate Matter - PM1.0: "));
      Serial.print(results[12]);
      Serial.print(F(" PM2.5: "));
      Serial.print(results[13]);
      Serial.print(F(" PM10: "));
      Serial.println(results[14]);
    }

    if (results[15] > 0) {
      Serial.print(F("UV Index: "));
      Serial.println(results[15]);
    }
  }
  
  // Print statistics every 100 packets
  if (receivedPackets % 100 == 0 && receivedPackets > 0) {
    Serial.print(F("Statistics - Received: "));
    Serial.print(receivedPackets);
    Serial.print(F(", Lost: "));
    Serial.print(lostPackets);
    Serial.print(F(", Loss rate: "));
    Serial.print((float)lostPackets / (receivedPackets + lostPackets) * 100);
    Serial.println(F("%"));
  }
}