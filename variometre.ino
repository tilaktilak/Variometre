#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SD.h>
#include <PCD8544.h>
#include <EEPROM.h>

// Software SPI (slower updates, more flexible pin options):
// pin 7 - Serial clock out (SCLK)
// pin 6 - Serial data out (DIN)
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)

static PCD8544 lcd;
static char filename[10];
static Adafruit_BMP085 bmp;

File dataFile;

void setup() {

  // Fetch last file index
  static char index = EEPROM.read(0x00);
  EEPROM.write(0x00,++index);
  
  Serial.begin(9600);  

  // PCD8544-compatible displays may have a different resolution...
  lcd.begin(84, 48);
  // Write a piece of text on the first line...
  lcd.setCursor(0, 0);
  lcd.print("Battery");
  

    // see if the card is present and can be initialized:
    if (!SD.begin(10)) {
        Serial.println(F("CARD ERR"));
        lcd.setCursor(0,4);
        lcd.print("SD FAIL");
        // don't do anything more:
       while(1);
    }
    if (!bmp.begin(3)) {
        Serial.println(F("BMP ERR"));
        while (1) {}
    }
    
    filename[5] = index + '0';
    dataFile = SD.open("data.txt",FILE_WRITE);
    if(!dataFile){

      lcd.print(F("SD - Open Err"));
      lcd.setCursor(0,5);
      lcd.print(filename);
      while(1);
    }
    else{
      dataFile.println("HEADER FILE");
      dataFile.close();
    }

    
}
  uint8_t count;
    bool tone_done = false;
      static int start_beep;
  static int duration,old_duration;
  static int timer;
    static bool inits = true;
void loop() {

  static uint8_t sec = 0;
  static uint8_t minu = 0;
  static uint8_t hour = 23;
  static uint32_t usec = 0;
  static uint32_t old_usec = 0;
  static float alt,old_alt,smooth;
  static float derivative;
  static int16_t freq;


  char heure[8];


  // Update Time
  usec = millis();
  sec = usec / 1000;
  minu = sec / 60;
  hour = minu / 60;

  alt = bmp.readAltitude();
  if(inits){
  smooth = alt;
  old_alt = alt;
  old_usec = usec;
  inits = false;
  }



  // Process Pressure
smooth = 0.98*smooth + 0.02*alt;

if((old_usec - usec)>=250){
  derivative = ((smooth-old_alt)*1000)/(usec-old_usec);
  old_usec = usec;
  old_alt = smooth;
}

  

  switch(count){
    case 1 : // Print data on screen
      lcd.setCursor(0,1);
      lcd.print("smooth :");
      lcd.print(smooth);
      lcd.setCursor(0,2);
      //lcd.print("ALTI :");
      //lcd.print(bmp.readAltitude());
      lcd.setCursor(0,3);
      lcd.print("time:");
      if((hour%24)<10) lcd.print("0");
      lcd.print(hour%24);
      lcd.print(":");
      if((minu%60)<10) lcd.print("0");
      lcd.print(minu%60);
      lcd.print(":");
      if((sec%60)<10) lcd.print("0");
      lcd.print(sec%60);
      lcd.setCursor(0,4);
      lcd.print("rate: ");
      lcd.print(derivative);
      break;
     case 2: // Write data to SD Card
       dataFile = SD.open("data.txt",FILE_WRITE);
       if(dataFile){
          dataFile.println(smooth);
          dataFile.close();
       }else{
          lcd.setCursor(0,5);
          lcd.print("Write fail");
       }
       case 3: // Make beep
       #define ZERO_LEVEL 0.18
        if(abs(derivative) >= ZERO_LEVEL){
          duration = (int) 100 + 50/abs(derivative);}
          else{
          duration = 0;
          }
          duration = constrain(duration,0,400);
          //freq = (unsigned int)smooth_derivative / 500 ;
  lcd.setCursor(0,5);
  lcd.print(duration);
          freq =(int) (640 + 100*derivative);
          //if(derivative > 0.0) freq += 300;
          //else{freq += 300;}
          freq = constrain(freq,40,4000);
          
          if(!tone_done){
            if(duration != 0)
            tone(2,freq,duration);
            tone_done = true;
            start_beep = millis();
            old_duration = duration;
          }
            timer = start_beep+2*old_duration - millis();
          if(timer<10){
            tone_done = false;
          }
          count = 0;
      break;
      
    default:
    break;
  }
  count++;
  

}
