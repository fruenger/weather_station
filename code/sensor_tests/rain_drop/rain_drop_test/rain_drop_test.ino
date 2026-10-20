void setup() {
Serial.begin(9600);
pinMode(2,INPUT);


}

void loop() {
int rainAnalogVal = analogRead(A1);   // analog to digital converter, reads value from 0 to 1023 (achieved via voltage divider), 
                                      // prop to resistance (lower resistance -> more rain)  
int rainDigitalVal = digitalRead(2);  // reads 0, when rain sensor's resitance is smaller that a comparison value (dep. on potentiometer)
String weather;


    if(rainDigitalVal==0){            // rainDigitalVal is 0, when resistance is lower than threshold -> assign 'raining'
      weather=F("raining");
    }
    else{                             // rainDigitalVal is 1, when resistance is higher than threshold -> assign 'no rain'
      weather=F("no rain");
    }
Serial.print(rainAnalogVal);         // print to serial monitor
Serial.print("\t");
Serial.println(weather);
delay(1000);                         // wait x ms before repeating the loop

}
