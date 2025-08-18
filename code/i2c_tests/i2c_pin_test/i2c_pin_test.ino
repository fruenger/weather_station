// I2C Pin Test - Explizite Pin-Konfiguration
// Testet I2C-Pins Schritt für Schritt

#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Serial.println(F("=== I2C PIN TEST ==="));
  
  // I2C-Pins explizit als INPUT konfigurieren
  Serial.println(F("Configuring I2C pins..."));
  pinMode(A4, INPUT);  // SDA
  pinMode(A5, INPUT);  // SCL
  Serial.println(F("I2C pins configured as INPUT"));
  
  // Kurze Pause
  delay(1000);
  
  // I2C initialisieren
  Serial.println(F("Initializing I2C..."));
  Wire.begin();
  Serial.println(F("I2C initialized"));
  
  // Teste nur eine Adresse
  Serial.println(F("Testing single address 0x01..."));
  Wire.beginTransmission(0x01);
  Serial.println(F("Transmission started"));
  
  byte error = Wire.endTransmission();
  Serial.print(F("Transmission ended with error code: "));
  Serial.println(error);
  
  if (error == 0) {
    Serial.println(F("SUCCESS: Device found at 0x01"));
  } else if (error == 1) {
    Serial.println(F("ERROR: Data too long"));
  } else if (error == 2) {
    Serial.println(F("NORMAL: NACK on address (no device)"));
  } else if (error == 3) {
    Serial.println(F("ERROR: NACK on data"));
  } else if (error == 4) {
    Serial.println(F("ERROR: Other error"));
  } else {
    Serial.println(F("ERROR: Unknown error"));
  }
  
  Serial.println(F("Test complete"));
  Serial.println(F("=================="));
}

void loop() {
  Serial.print(F("Loop running - Time: "));
  Serial.print(millis());
  Serial.println(F(" ms"));
  delay(2000);
} 