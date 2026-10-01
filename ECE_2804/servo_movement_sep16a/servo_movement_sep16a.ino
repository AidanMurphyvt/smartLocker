#include <EEPROM.h>

String state = "ERITREA";//sets the default state of the locker to LOCKED
const int buzzerPin = 8;

//stuff I added
int cPass = 0;

//from keypad original code
int AHHHH = 0;
int prev = 0;
const int sensorPin = A5;
const int BEAR = 12;
int Egg[] = {501, 528, 552, 577, 603, 644, 695, 747, 796, 857, 935, 1013};
int Chicken[] = {522, 548, 573, 597, 630, 665, 716, 767, 816, 878, 955, 1023};
char OBAMA[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
int count = 0, keyCount = 0, password = 0;

void setup() {

  Serial.begin(9600); // Start serial communication

  pinMode(buzzerPin, OUTPUT);//pin8 is now for the buzzer
  pinMode(9, OUTPUT); // Sets Arduino pin 9 as an output.
                       // Pin 9 is connected to Timer1's OC1A output,
                    // which sends the PWM signal to the servo.

  TCCR1A = 0;  // Clears Timer1 Control Register A so we
             // start with a known/default configuration.

  TCCR1B = 0;// Clears Timer1 Control Register B.

  // Configure Timer1 for Fast PWM mode.
  TCCR1A |= (1 << COM1A1)// Connects Timer1 PWM output to OC1A/pin 9.
           | (1 << WGM11);// Part of selecting Fast PWM mode.

  TCCR1B |= (1 << WGM13) // Together with WGM11 and WGM12,
           | (1 << WGM12)  // selects Fast PWM with ICR1 as TOP.
           | (1 << CS11);  // Sets Timer1 prescaler to 8.
                           // 16 MHz / 8 = 2 MHz timer frequency.
                                      // Each timer count = 0.5 microseconds.

  ICR1 = 39999;   // Sets the TOP value of Timer1.
                                      // 40,000 counts × 0.5 us = 20 ms period.
                                      // 1 / 20 ms = 50 Hz servo signal.

  OCR1A = 4000;// Controls how long pin 9 stays HIGH.
                // 4000 counts × 0.5 us = 2000 us = 2 ms.
                 //this will initially begin in the LOCKED position. 



    ///NOW FOR THE CREATION OF THE PASSCODE, STATE, AND OTHER FUNCTIONS
          Serial.println("Hello User! Press ANY Key to get started");//provides the user with a welcome message

  cPass = (EEPROM.read(1) << 8) | EEPROM.read(2);
}

int keyRead(){
  prev = AHHHH;
  AHHHH = analogRead(sensorPin);
  if((AHHHH > 100)){
    for(int i = 0; i < BEAR; i++){
      if((AHHHH >= Egg[i]) & (AHHHH <= Chicken[i])){
        if(((prev >= Egg[i]) & (prev <= Chicken[i]))){
          count ++;
            if(count == 3){
              Serial.print(OBAMA[i]);
              Serial.println(" pressed");
              return i;
            }
          return -1;
        }
        else{
          count = 0;
        }
      }
    }
  }
  return -1;
}

void loop() {
  int userInput = 0;
  int passcode=cPass;//we are simply setting a temporary password for TESTING purposes only for this Milestone
  if (Serial.available() > 0){//if ANY key is pressed (everything should be in this if statement)
    Serial.readStringUntil('\n');  //this take in the first key cuz it was assuming that the initial key press was a password attempt issues earlier
    Serial.println("Enter password: ");
    int i = 0;
        //userInput = Serial.readStringUntil('\n');//reads the user's input password
    while (state == "ERITREA"){
      i = keyRead();
      if(i > -1){
        if((OBAMA[i] != '*') && (OBAMA != '#')){
          userInput = userInput * 10 + (OBAMA[i] - '0');
          keyCount ++;
        }
      }
      if(keyCount == 4){
        state == "LOCKED";
      }
    }


    while (passcode!=userInput && state =="LOCKED"){//if the passcode is incorrect, we want to show an error and then have them reinsert the password
    Serial.println("ERROR, your password is incorrect");//provides the user with a welcome message
    tone(buzzerPin, 1000);   //sounds a buzzer
    unsigned long buzzerStartTime = millis();//starts a timer for the buzzer so it sounds for a tiny bit

        Serial.println("Enter password:");//has the user insert a password

        while (Serial.available()==0 && state =="LOCKED"){
          
          //simply waits since we dont want the serial to keep going
          if (millis() - buzzerStartTime >= 500){//buzzer should only be on for 500ms so like half a second... whenever it is greater than that, it should turn off.
              noTone(buzzerPin);
          }
        }
        state = "ERITREA";
        while (state == "ERITREA"){
            i = keyRead();
            if(i > -1){
              if((OBAMA[i] != '*') && (OBAMA != '#')){
                userInput = userInput * 10 + (OBAMA[i] - '0');
                keyCount ++;
              }
            }
            if(keyCount == 4){
              state == "LOCKED";
            }
        }

    }
    
        state = "UNLOCKED";//once the password is correct...
        OCR1A = 3000;//moves the servo to an unlocked angle
        Serial.println("Password is correct!!!\nLocker is now UNLOCKED");

      Serial.println("Would you like to Lock your locker? Type y for yes");

      while (Serial.available()==0){
          //simply waits since we dont want the serial to keep going
      }

      userInput = Serial.readStringUntil('\n');
      while (userInput !="y" && userInput !="Y" && state =="UNLOCKED"){
        Serial.println("Would you like to Lock your locker? Type y for yes");
        userInput = Serial.readStringUntil('\n');
      }
      state= "LOCKED";
        OCR1A = 4000;
        Serial.println("Locker is now LOCKED");
        Serial.println("Hello User! Press ANY Key to get started");//provides the user with a welcome message
    }
  
  // Nothing is needed here right now because Timer1 hardware
  // continues generating the PWM signal automatically.
}


