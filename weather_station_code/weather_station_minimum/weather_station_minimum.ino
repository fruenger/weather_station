// Version vom 15.12.2023 14:43:00 CET

// used for the I2C interface
#include <Wire.h>             // library to use I2C (this sensor ONLY uses I2C for communication)
#include <Adafruit_Sensor.h>  // interface with the BME280 sensor
#include <Adafruit_BME280.h>  // interface with the BME280 sensor
#include "DEV_Config.h"       // light sensor material
#include "TSL2591.h"          // light sensor material
#include <SPI.h>              // Antenna stuff
#include <nRF24L01.h>         // Antenna stuff
#include <RF24.h>             // Antenna stuff


#define SEALEVELPRESSURE_HPA (1030.5)

float time_stamp;
unsigned long delayTime;


//  BME280 sensor -> temperature, humidity, pressure
Adafruit_BME280 bme;  //  create an Adafruit_BME280 object called bme


//  Anemomenter variables
const int RecordTime = 1000;  //Define Measuring Time (Milliseconds)
const int SensorPin = 3;      //Define Interrupt Pin (2 or 3 @ Arduino Uno)
int InterruptCounter;
float WindSpeed;
const float conversion_factor = 1.0;

const byte ResetPin = 4;
const unsigned long ResetInterval = 3600000;


//  Light sensor variables
UWORD Lux = 0;


//  Antenna variables
RF24 radio(9, 8);  // CE, CSN
const byte address[6] = "99999";


//  Rain sensor variables
//  Digital input pin
int rain_sensor_input_pin = 2;

//  1.25 ml per dipper change
//  The diameter of raingauge is about 130mm, r = 65mm, surface = pi*r^2 = 132.73 cm^2
//  therefore per m^2 (=10000cm^2) we have a factor of about 75.34
//  => 1.25*75.34 = 0.094175mm/m^2
//  
// const char* mmPerSquareMeter = "0.094175";
// const float mmPerSquareMeter = 0.094175;
const float mmPerSquareMeter = 1.25;

//  Indicates that this is the first loop
bool firstLoop = true;
//  Specifies which mode was last time
bool lastState = false;
//  Specifies the state of the loop now
bool currentState = false;
//  Specifies when did we change the status the last time
unsigned long lastChanged = 0;
//  Number of tippings since its start
int totalCount   = 0;


//  Define the results array in which the results will be stored. 
//  The array can have a maximum size of 32 bytes! If sending the 
//  values as floating point numbers (each being 4bytes in size) 
//  we can send a total of 8 valies per transmission
float results[8];

// SETUP Function
void setup() {
  //  Set up the reset pin
  digitalWrite(ResetPin, HIGH);
  //pinMode(ResetPin, OUTPUT);

  Serial.begin(9600);
  while (!Serial)
    ;  // time to get serial running


  //  Initialize pin for rain sensor
  pinMode(rain_sensor_input_pin, INPUT);


  //  Set up BME with defaults
  unsigned status;
  status = bme.begin(0x76);
  // You can also pass in a Wire library object like &Wire2
  // status = bme.begin(0x76, &Wire2)

  //if (!status) {
  //    Serial.println(F("Could not find a valid BME280 sensor, check wiring, address, sensor ID!"));
  //    Serial.print(F("SensorID was: 0x")); Serial.println(bme.sensorID(),16);
  //    while (1) delay(10);
  //}

  delayTime = 1000;


  //  Light sensor stuff
  DEV_ModuleInit();
  TSL2591_Init();


  //  Antenna stuff
  radio.begin();
  radio.openWritingPipe(address);
  radio.stopListening();
}

void loop() {

  time_stamp++;
  results[0] = time_stamp;

  //  BME stuff
  results[1] = bme.readTemperature();
  results[2] = bme.readPressure();
  results[3] = bme.readHumidity();


  //  Light sensor stuff
  results[4] = (float)TSL2591_Read_Lux();


  //  Rain sensor stuff
  // int rainAnalogVal = analogRead(A1);  // analog to digital converter, reads value from 0 to 1023 (achieved via voltage divider),
  // // prop to resistance (lower resistance -> more rain)
  // int rainDigitalVal = digitalRead(2);  // reads 0, when rain sensor's resitance is smaller that a comparison value (dep. on potentiometer)
  // results[5] = (float)rainAnalogVal * (2 * rainDigitalVal - 1);

  //  If it is the first startup,  it should not recognize the current
  //  position of the tipper as a tip
  int currentState = digitalRead(rain_sensor_input_pin);
  if (firstLoop) {
    lastState = currentState;
    firstLoop = false;  // Sets that it has now run the startup
  };

  //  Checks the value of the tipper and translates it to a bool value
  if (currentState == 1) {
    currentState = true;
  } else {
    currentState = false;
  }

  //  Checks if the tipper has moved since the last loop run
  if (lastState != currentState) {
    lastState = currentState;

    //  Between two changes, there should be 0.25 s
    if ((millis() - lastChanged) > 250) {
      totalCount++;
      lastChanged = millis(); 

      results[5] = mmPerSquareMeter;
    } else {
      results[5] = 0.;
    }
  } else {
    results[5] = 0.;
  }


  //  Anemomenter stuff
  //WindSpeed = wind_measurement();
  results[6] = wind_measurement();


  //  Antenna stuff
  radio.write(&results, sizeof(results));

  for (int i = 0; i < 8; i++) {
    Serial.print(results[i]);
    Serial.print(F(" "));
  }

  Serial.print("\n");


  //  Check the run time for whether the Arduino should be resetted.
  if (millis() > ResetInterval) {
    digitalWrite(ResetPin, LOW);
  }

}

//////////////////////
// UTLITY FUNCTIONS //
//////////////////////

// A utility function that helps to print the values of the Barometric and Temperature sensor!
// void printValues() {
//   Serial.print("Temperature = ");
//   Serial.print(bme.readTemperature());
//   Serial.println(" °C");

//   Serial.print(F("Pressure = "));

//   Serial.print(bme.readPressure() / 100.0F);  //reads pressure in hPa (hectoPascal = millibar)
//   Serial.println(F(" hPa"));

//   Serial.print(F("Approx. Altitude = "));
//   Serial.print(bme.readAltitude(SEALEVELPRESSURE_HPA));
//   Serial.println(" m");

//   Serial.print(F("Humidity = "));
//   Serial.print(bme.readHumidity());  //reads absolute humidity
//   Serial.println(F(" %"));

//   Serial.println();
// }

// take a measurement with the wind sensor
float wind_measurement(void) {
  InterruptCounter = 0;
  attachInterrupt(digitalPinToInterrupt(SensorPin), countup, RISING);
  delay(RecordTime);
  detachInterrupt(digitalPinToInterrupt(SensorPin));
  WindSpeed = 1000.0 * (float)InterruptCounter / (float)RecordTime * conversion_factor;
  return WindSpeed;
}

// a utility function that eases to increment numbers
void countup() {
  InterruptCounter++;
}
