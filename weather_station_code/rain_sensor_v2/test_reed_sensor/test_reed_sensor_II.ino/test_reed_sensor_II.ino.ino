//  Decleration and initialization of the input pin
int analog_input_pin = A0;
int digital_input_pin = 3;

//  1.25 ml per dipper change, 94.175 ml per m^2 (=1.25*75.34), = 0.094175mm/m^2
//  diameter of raingauge is about 130mm, r = 65mm, surface = pi*r^2 = 132.73 cm^2
//  therefore per m^2 (=10000cm^2) we have a factor of about 75.34
const char* mmPerSquareMeter = "0.094175";

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

void setup() {
  // pinMode(analog_input_pin, INPUT);
  pinMode(digital_input_pin, INPUT);

  //  Serial output with 9600 bps
  Serial.begin(9600);
}

void loop() {
  //  The program reads the current value of the input pins
  //  and outputs it via serial out
  // float analog;
  // int digital;

  //  If it is the first startup,  it should not send any data
  int currentState = digitalRead(digital_input_pin);
  if (firstLoop) {
    lastState = currentState;
    firstLoop = false;  // Sets that it has now run the startup.
  };

  //  Checks the value of the stick, translates it to a bool value.
  if (currentState == 1) {
    currentState = true;
  } else {
    currentState = false;
  }

  //  Checks whether the previous loop has the same loop as the current loop. Will not go into this first loop.
  if (lastState != currentState) {

    lastState = currentState;

    // between two changes, there should be 0.25 sec difference
    if ((millis() - lastChanged) > 250) {
      totalCount++;
      lastChanged = millis();

      // Serial.print("Number of times:");
      // Serial.println(totalCount);

      Serial.print("mm/m^2: ");
      Serial.print(totalCount); Serial.print(" - ");  Serial.println(mmPerSquareMeter);
    }
  }

  // //  Cuttent value will be read and the analog signal will
  // //  be converted to the voltage
  // analog = analogRead (analog_input_pin) * (5.0 / 1023.0);
  // digital = digitalRead (digital_input_pin);

  // //  Output results
  // Serial.print ("Analog voltage value:"); Serial.print (analog, 4); Serial.print ("V, ");
  // Serial.print ("Extreme value:");

  // if (digital==1) {
  //   Serial.println (" reached");
  // } else {
  //   Serial.println (" not reached yet");
  // }
  // Serial.println ("----------------------------------------------------");

  //  Small delay to avoid false readings.
  delay(150);
}
