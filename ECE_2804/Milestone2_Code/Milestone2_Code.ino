void EEPROM_write(unsigned int uiAddress, unsigned char ucData)
{
/* Wait for completion of previous write */
while(EECR & (1<<EEPE))
;
/* Set up address and Data Registers */
EEAR = uiAddress;
EEDR = ucData;
/* Write logical one to EEMPE */
EECR |= (1<<EEMPE);
/* Start eeprom write by setting EEPE */
EECR |= (1<<EEPE);
}

unsigned char EEPROM_read(unsigned int uiAddress)
{
/* Wait for completion of previous write */
while(EECR & (1<<EEPE))
;
/* Set up address register */
EEAR = uiAddress;
/* Start eeprom read by writing EERE */
EECR |= (1<<EERE);
/* Return data from Data Register */
return EEDR;
}

int AHHHH = 0;
int prev = 0;
const int sensorPin = A5;
const int length = 12;
int Sm[] = {501, 528, 552, 577, 603, 644, 695, 747, 796, 857, 935, 1013};
int Lg[] = {522, 548, 573, 597, 630, 665, 716, 767, 816, 878, 955, 1023};
char symbol[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
int savedPassword = 0, i = 0;

int typePass(){
  Serial.println("Type your password: ");
  int password = 0, keyCount = 0, count = 0;
  while(keyCount < 4){
    prev = AHHHH;
    AHHHH = analogRead(sensorPin);
    if(AHHHH > 100){
      for(int i = 0; i < length; i++){
        if((AHHHH >= Sm[i]) & (AHHHH <= Lg[i])){
          if(((prev >= Sm[i]) & (prev <= Lg[i]))){
            count ++;
              if(count == 6){
                // Serial.print(symbol[i]);
                // Serial.println(" pressed");
                if(symbol[i] != '*' && symbol[i] != '#'){
                  password = password*10 + (symbol[i] - '0');
                  keyCount ++;
                  // Serial.print(symbol[i]);
                  // Serial.print(" added to pass, current is: ");
                  // Serial.println(password);
                }
              }
            break;
          }
          else{
            count = 0;
          }
        }
      }  
    }
  }
  while(AHHHH > 100){
    prev = AHHHH;
    AHHHH = analogRead(sensorPin);
  }
  if(keyCount == 4)
    return password;
  return -1;
}

void changePass(){
  Serial.println("Type a password to save to memory");
  int newPass = typePass();
  EEPROM_write(1, (unsigned char)(newPass/100));
  EEPROM_write(2, (unsigned char)(newPass%100));
  Serial.print(newPass);
  Serial.println(" was typed by user and saved to EEPROM");
  savedPassword = newPass;
}

void displayPass(){
  savedPassword = (EEPROM_read(1) *100) + EEPROM_read(2);
  Serial.print("Saved Password in EEPROM is: ");
  Serial.println(savedPassword);
}

/*THE ABOVE IS WHAT AIDAN DID
THE BELOW IS WHAT DIMY DID*/

String state = "LOCKED";//sets the default state of the locker to LOCKED
const int buzzerPin = 8;
void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  savedPassword = (EEPROM_read(1) *100) + EEPROM_read(2);

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
    Serial.println("");
          Serial.println("Hello User! Press ANY Key to get started");//provides the user with a welcome message

}

void loop() {
  // put your main code here, to run repeatedly

    int userInput; 
    String userMessage;
    //String passcode="Milestone2";//we are simply setting a temporary password for TESTING purposes only for this Milestone
    if (Serial.available() > 0){//if ANY key is pressed (everything should be in this if statement)
    Serial.readStringUntil('\n');  //this take in the first key cuz it was assuming that the initial key press was a password attempt issues earlier
  Serial.println("You're locker is currently "+ state);
      userInput = typePass();

        /*while (Serial.available()==0){
          //simply waits since we dont want the serial to keep going
        }

        userInput = Serial.readStringUntil('\n');//reads the user's input password
*/

    while (savedPassword!=userInput && state =="LOCKED"){//if the passcode is incorrect, we want to show an error and then have them reinsert the password
    
    Serial.println("ERROR, your password is incorrect");//provides the user with an error message
    Serial.println("You're locker is currently "+ state);
    //tone(buzzerPin, 1000);   //sounds a buzzer
    //tone(buzzerPin, 500);//different tone?
    unsigned long buzzerStartTime = millis();//starts a timer for the buzzer so it sounds for a tiny bit

        //Serial.println("Enter password:");//has the user insert a password
        //Serial.println("Made it here");
        tone(buzzerPin, 500);
//unsigned long buzzerStartTime = millis();

        while (millis() - buzzerStartTime < 500){
    // buzzer stays on during this time
          }

        noTone(buzzerPin);
              //Serial.println("Made it TOOO");

        userInput = typePass();//reads the user's input password
        state="LOCKED";//not sure what this does

    }
    Serial.println("Password is correct!!!"); 

      userMessage = " ";
        //Serial.println("You're locker is currently "+ state);
        
      while (state =="LOCKED" && (userMessage !="c" && userMessage !="u")){

      Serial.println("Would you like to UnLock your locker (type u) or Change you Password (type c)?");
      

      while (Serial.available()==0){
          //simply waits since we dont want the serial to keep going
      }

      userMessage = Serial.readStringUntil('\n');


      if (userMessage =="u"){//unlocks the locker
      OCR1A = 3000;//moves the servo to an unlocked angle
        Serial.println("Locker is now UNLOCKED");
        state = "UNLOCKED";//once the password is correct...
      }
      else if (userMessage = "c"){//changes the password
          changePass();//allows the user to change their password
          displayPass();//displays the new password for the user
          Serial.println("Youre Password has now been changed!");
          Serial.println("The Locker will now go back to the welcome screen.");
          Serial.println("Thank you!");
      }
    }//end of change/unlock while 

    //if the user 
      while (userMessage !="y" && userMessage !="Y" && state =="UNLOCKED"){
        Serial.println("Would you like to Lock your locker? Type y for yes");
        while (Serial.available()==0){
          //simply waits since we dont want the serial to keep going
        }
        userMessage = Serial.readStringUntil('\n');
      }
      state= "LOCKED";
        OCR1A = 4000;
        Serial.println("Locker is "+state);
        Serial.println("Hello User! Press ANY Key to get started");//provides the user with a welcome message
    }
  
  // Nothing is needed here right now because Timer1 hardware
  // continues generating the PWM signal automatically.
  
}