/***************************************************************************
  This is a library for the BME280 humidity, temperature & pressure sensor

  Designed specifically to work with the Adafruit BME280 Breakout
  ----> http://www.adafruit.com/products/2650

  These sensors use I2C or SPI to communicate, 2 or 4 pins are required
  to interface. The device's I2C address is either 0x76 or 0x77.

  Adafruit invests time and resources providing this open source code,
  please support Adafruit andopen-source hardware by purchasing products
  from Adafruit!

  Written by Limor Fried & Kevin Townsend for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
  See the LICENSE file for details.
 ***************************************************************************/

// temperature and pressure sensor

#include <Wire.h>               //library to use I2C (this sensor ONLY uses I2C for communication)
#include <Adafruit_Sensor.h>   //interface with the BME280 sensor
#include <Adafruit_BME280.h>   //interface with the BME280 sensor

#define SEALEVELPRESSURE_HPA (1030.5)

Adafruit_BME280 bme; //create an Adafruit_BME280 object called bme

unsigned long delayTime;

// anemomenter variables
const int RecordTime = 1000; //Define Measuring Time (Milliseconds)
const int SensorPin = 3;  //Define Interrupt Pin (2 or 3 @ Arduino Uno)
int InterruptCounter;
float WindSpeed;

// SETUP Function
void setup() {
    Serial.begin(9600); //start serial communication
    while(!Serial);    // time to get serial running
    Serial.println(F("BME280 test"));

    unsigned status;
    
    // default settings, initialize sensor
    status = bme.begin(0x76);  
    // You can also pass in a Wire library object like &Wire2
    // status = bme.begin(0x76, &Wire2)
    if (!status) {
        Serial.println(F("Could not find a valid BME280 sensor, check wiring, address, sensor ID!"));
        Serial.print(F("SensorID was: 0x")); Serial.println(bme.sensorID(),16);
        Serial.print(F("        ID of 0xFF probably means a bad address, a BMP 180 or BMP 085\n"));
        Serial.print(F("   ID of 0x56-0x58 represents a BMP 280,\n"));
        Serial.print(F("        ID of 0x60 represents a BME 280.\n"));
        Serial.print(F("        ID of 0x61 represents a BME 680.\n"));
        while (1) delay(10);
    }
    
    Serial.println(F("-- Default Test --"));
    delayTime = 1000;

    Serial.println();
}


void loop() { 
    printValues();    //function reads values from BME280 and prints them in the Serial Monitor
    delay(delayTime);
}

//////////////////////
// UTLITY FUNCTIONS //
//////////////////////

// A utility function that helps to print the values of the Barometric and Temperature sensor!
void printValues() {
    Serial.print("Temperature = ");
    Serial.print(bme.readTemperature());
    Serial.println(" °C");

    Serial.print(F("Pressure = "));

    Serial.print(bme.readPressure() / 100.0F);   //reads pressure in hPa (hectoPascal = millibar)
    Serial.println(F(" hPa"));

    Serial.print(F("Approx. Altitude = "));
    Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
    Serial.println(" m");

    Serial.print(F("Humidity = "));
    Serial.print(bme.readHumidity()); //reads absolute humidity
    Serial.println(F(" %"));

    Serial.println();
}
