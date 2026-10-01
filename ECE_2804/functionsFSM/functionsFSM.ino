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
int savedPassword = 0, i = 0, typed = -1;
char d, c;

char keyPress(){
  int count = 0, returnVal = -1;
  while(1){
    prev = AHHHH;
    AHHHH = analogRead(sensorPin);
    if(AHHHH > 100){
      for(int i = 0; i < length; i++){
        if((AHHHH >= Sm[i]) & (AHHHH <= Lg[i])){
          if(((prev >= Sm[i]) & (prev <= Lg[i]))){
            count ++;
              if(count == 3){
                returnVal = i;
              }
            break;
          }
          else{
            count = 0;
          }
        }
      }  
    }
    else if(returnVal != -1){
      return symbol[returnVal];
    }
  }
}

char decideMode(){
  Serial.println("Type * to open the locker and # to save a new password to memory");
  while(1){
    c = keyPress();
    if(c == '*' || c == '#')
      return c;
  }
}

int typePass(){
  Serial.println("Type your password: ");
  int password = 0, keyCount = 0;
  char s;
  while(keyCount < 4){
    s = keyPress();
    if(s != '#' && s != '*'){
      password = password*10 + s-'0';
      keyCount ++;
    }
    else if(s == '#'){
      keyCount = 0;
      password = 0;
    }
    else{
      if(keyCount > 0)
        keyCount --;
      password /= 10;
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

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  savedPassword = (EEPROM_read(1) *100) + EEPROM_read(2);
  Serial.println(" ");
}

void loop() {
  // put your main code here, to run repeatedly:
  if(i==0)
    displayPass();
  i++;
  typed = typePass();
  while(savedPassword != typed){
    Serial.print(typed);
    Serial.println(" is the wrong password, please retype");
    typed = typePass();
  }
  d = decideMode();
  if(d == '*')
    Serial.println("Opening locker");
  else if(d == '#'){
    Serial.println("Changing password");
    changePass();
  }
  else{
    Serial.println("exited decide mode too early");
  }
  
  
}
