//  Decleration and initialization of the input pin
int analog_input_pin = A0;
int digital_input_pin = 3;

void setup() {
  pinMode (analog_input_pin, INPUT);
  pinMode (digital_input_pin, INPUT);

  //  Serial output with 9600 bps
  Serial.begin (9600);
}

void loop() {
  //  The program reads the current value of the input pins 
  //  and outputs it via serial out
  float analog;
  int digital;

  //  Cuttent value will be read and the analog signal will
  //  be converted to the voltage
  analog = analogRead (analog_input_pin) * (5.0 / 1023.0);
  digital = digitalRead (digital_input_pin);

  //  Output results
  Serial.print ("Analog voltage value:"); Serial.print (analog, 4); Serial.print ("V, ");
  Serial.print ("Extreme value:");

  if (digital==1) {
    Serial.println (" reached");
  } else {
    Serial.println (" not reached yet");
  }
  Serial.println ("----------------------------------------------------");
  delay (1000);
}
