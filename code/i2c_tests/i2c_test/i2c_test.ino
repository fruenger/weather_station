// I2C Test ohne Multiplexer
// Testet nur die grundlegende I2C-Kommunikation

#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== I2C TEST OHNE MULTIPLEXER ==="));
  
  // I2C initialisieren
  Wire.begin();
  Serial.println(F("I2C initialized"));
  
  // Kurzer I2C-Scan (nur erste 10 Adressen)
  Serial.println(F("Quick I2C scan (addresses 1-10)..."));
  for (byte address = 1; address <= 10; address++) {
    Serial.print(F("Testing address 0x"));
    if (address < 16) Serial.print("0");
    Serial.print(address, HEX);
    Serial.print(F(": "));
    
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.println(F("OK"));
    } else if (error == 1) {
      Serial.println(F("Data too long"));
    } else if (error == 2) {
      Serial.println(F("NACK on address"));
    } else if (error == 3) {
      Serial.println(F("NACK on data"));
    } else if (error == 4) {
      Serial.println(F("Other error"));
    } else {
      Serial.println(F("Unknown error"));
    }
    
    delay(100); // Kurze Pause zwischen Tests
  }
  
  Serial.println(F("I2C scan complete"));
  Serial.println(F("================================"));
}

void loop() {
  Serial.print(F("Loop running - Time: "));
  Serial.print(millis());
  Serial.println(F(" ms"));
  delay(2000);
} 