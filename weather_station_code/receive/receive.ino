//Include Libraries
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

//create an RF24 object
RF24 radio(9, 10);  // CE, CSN

//address through which two modules communicate.
const byte address[6] = "99999";

const byte length = 8;

float results[length];

void setup()
{
  
  while (!Serial);
    Serial.begin(9600);
  
  radio.begin();
  
  //set the address
  radio.openReadingPipe(0, address);
  
  //Set module as receiver
  radio.startListening();

  delay(500);
}

void loop()
{
  //Read the data if available in buffer
  if (radio.available())
  {
    radio.read(&results, sizeof(results));

    byte *p = (byte*) results;

    //for (int i=0; i<length; i++) {
      //Serial.print(results[i]);
      //Serial.print(F(" "));
      
    //  Serial.write((float) results[i]);
    //}
    //Serial.print("\n");

    for(byte i=0; i<sizeof(results); i++) {
      Serial.write(p[i]);
    }
    //Serial.print("\n");
  }
}