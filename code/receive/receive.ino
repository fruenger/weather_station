// Weather Station - Receiver Arduino
// Version: 2.0 (Updated for I2C Multiplexer and CCS811 Air Quality Sensor)
// Features: Receives data from sender and forwards via serial to Python script
// Data Format: 16 int16_t values (32 bytes) with scaled integers
// I2C Multiplexer: TCA9548A for better sensor organization

//Include Libraries
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

//create an RF24 object
RF24 radio(9, 8);  // CE, CSN

//address through which two modules communicate.
const byte address[6] = "99999";

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
  while (!Serial);
  Serial.begin(9600);
  
  radio.begin();

  // Configure radio to match transmitter settings and reduce false positives
  radio.setChannel(76);            // must match sender
  radio.setPayloadSize(32);        // fixed 32 bytes payload
  radio.setCRCLength(RF24_CRC_16); // robust CRC
  radio.setAutoAck(true);          // use auto-ack
  radio.setRetries(3, 15);         // retries similar to sender
  radio.setPALevel(RF24_PA_HIGH);  // adequate power level

  // Set the address and start listening
  radio.openReadingPipe(0, address);
  radio.flush_rx();                // clear any stale data
  radio.flush_tx();
  radio.startListening();

  delay(500);
  
  Serial.println(F("Weather Station Receiver Starting..."));
  Serial.println(F("Version 2.0"));
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
    for (int i = 0; i < 15; i++) {  // Print first 15 values for debug
      Serial.print(results[i]);
      Serial.print(F(" "));
    }
    Serial.println();
    
    // Print air quality data if available
    if (results[12] > 0 || results[13] > 0) {
      Serial.print(F("Air Quality - CO2: "));
      Serial.print(results[12]);
      Serial.print(F(" ppm, TVOC: "));
      Serial.print(results[13]);
      Serial.print(F(" ppb, Baseline: "));
      Serial.print(results[14]);
      Serial.println();
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