
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

char savedPassword[5]; 

void setup() {
  Serial.begin(9600);
  while (!Serial); // Waits for Serial Monitor to connect

  Serial.println("\nBOARD STARTED / RESET");
  
  // Read existing value
  unsigned char byte1 = EEPROM_read(1);
  savedPassword[0] = (byte1 / 10) + '0';
  savedPassword[1] = (byte1 % 10) + '0';

  unsigned char byte2 = EEPROM_read(2);
  savedPassword[2] = (byte2 / 10) + '0';
  savedPassword[3] = (byte2 % 10) + '0';
  savedPassword[4] = '\0';

  Serial.print("Value read BEFORE write: ");
  Serial.println(savedPassword);
  
  delay(2000); // 2 second delay so you can track the timing

  // Write new values
  EEPROM_write(1, 19);
  EEPROM_write(2, 84);
  Serial.println("New value written to EEPROM.");
}

void loop() {
  // Main code
}