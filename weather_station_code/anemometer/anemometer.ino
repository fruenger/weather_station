// advance the circuit with another capacitor - 100nF

const int RecordTime = 1000; //Define Measuring Time (Milliseconds)
const int SensorPin = 3;  //Define Interrupt Pin (2 or 3 @ Arduino Uno)
int InterruptCounter;
float WindSpeed;

void setup() {
    Serial.begin(9600);
}

void loop() {
  WindSpeed = wind_measurement();
  Serial.print("Wind Speed: ");
  Serial.print(WindSpeed);       //Speed in km/h
  Serial.print(" km/h - ");
  Serial.print(WindSpeed / 3.6); //Speed in m/s
  Serial.println(" m/s");
}


float wind_measurement(void){
    InterruptCounter = 0;
    attachInterrupt(digitalPinToInterrupt(SensorPin), countup, RISING);
    delay(RecordTime);
    detachInterrupt(digitalPinToInterrupt(SensorPin));
    WindSpeed = 1000.0 * (float)InterruptCounter / (float)RecordTime * 2.4;
    Serial.print(F("Interrupts: "));
    Serial.print(InterruptCounter);
    Serial.print(F("\t"));
    return WindSpeed;
}

void countup() {
  InterruptCounter++;
}