void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);

  int savedPassword = (EEPROM.read(1) << 8) | EEPROM.read(2);
  Serial.println(" ");
  Serial.print("Saved Password in EEPROM is: ");
  Serial.println(savedPassword);
}

int curr = 0;
int prev = 0;
const int sensorPin = A5;
const int size = 12;
int low[] = {501, 528, 552, 577, 603, 644, 695, 747, 796, 857, 935, 1013};
int high[] = {522, 548, 573, 597, 630, 665, 716, 767, 816, 878, 955, 1023};
char key[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'};
int count = 0, keyCount = 0, password = 0;

void loop() {
  
  
  // put your main code here, to run repeatedly:
  prev = curr;
  curr = analogRead(sensorPin);
  if((curr > 100)){
    for(int i = 0; i < size; i++){
      if((curr >= low[i]) & (curr <= high[i])){
        if(((prev >= low[i]) & (prev <= high[i]))){
          count ++;
          Serial.print("Value inputted is : ");
          Serial.println(curr);
            if(count == 3){
              Serial.print(key[i]);
              Serial.println(" pressed");
              if(key[i] != '*' && key[i] != '#'){
                password = password*10 + (key[i] - '0');
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
