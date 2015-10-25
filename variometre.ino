#include <TinyGPS++.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SD.h>
#include <PCD8544.h>
#include <EEPROM.h>

TinyGPSPlus gps;
// Software SPI (slower updates, more flexible pin options):
// pin 7 - Serial clock out (SCLK)
// pin 6 - Serial data out (DIN)
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)

static PCD8544 lcd;

static Adafruit_BMP085 bmp;

File dataFile;
  char filename[9] = "vari.igc";
void setup() {

  // Fetch last file index
  static uint8_t index = EEPROM.read(0x00);
  EEPROM.write(0x00, ++index);
  filename[1] = (index/100)%10 + '0';
  filename[2] = (index/10)%10 + '0';
  filename[3] = (index)%10 + '0';
  Serial.begin(9600);

  // PCD8544-compatible displays may have a different resolution...
  lcd.begin(84, 48);
  // Write a piece of text on the first line...
  lcd.setCursor(0, 0);
  lcd.print("Battery");


  // see if the card is present and can be initialized:
  if (!SD.begin(10)) {
    Serial.println(F("CARD ERR"));
    lcd.setCursor(0, 4);
    lcd.print("SD FAIL");
    // don't do anything more:
    while (1);
  }
  if (!bmp.begin(3)) {
    Serial.println(F("BMP ERR"));
    while (1) {}
  }
  dataFile = SD.open(filename, FILE_WRITE);
  if (!dataFile) {

    lcd.print(F("SD - Open Err"));
    lcd.setCursor(0, 5);
    lcd.print(filename);
    while (1);
  }
  else {
    //dataFile.println("HEADER FILE");
    dataFile.close();
  }


}
uint8_t count;
bool tone_done = false;
static int start_beep;
static int duration, old_duration;
static int timer;
static bool inits = true;
void loop() {

  static uint8_t sec = 0;
  static uint8_t minu = 0;
  static uint8_t hour = 23;
  static uint32_t usec = 0;
  static uint32_t old_usec = 0;
  static float alt, old_alt, smooth;
  static float derivative;
  static int16_t freq;
  static uint8_t count_lcd = 0;
  static uint8_t count_sd = 0;
  static uint8_t count_gps = 0;


  char heure[8];


  // Update Time
  usec = millis();
  sec = usec / 1000;
  minu = sec / 60;
  hour = minu / 60;

  alt = bmp.readAltitude();
  if (inits) {
    smooth = alt;
    old_alt = alt;
    old_usec = usec;
    inits = false;
  }

  // Process Pressure
  smooth = 0.99 * smooth + 0.01 * alt;

  if ((old_usec - usec) >= 250) {
    derivative = ((smooth - old_alt) * 1000) / (usec - old_usec);
    old_usec = usec;
    old_alt = smooth;
  }



  switch (count) {
    case 1 : // Print data on screen
      count_lcd ++;
      if (count_lcd == 5) {
        lcd.setCursor(0, 1);
        lcd.print("smooth :");
        lcd.print(smooth);
        lcd.setCursor(0, 2);
        //lcd.print("ALTI :");
        //lcd.print(bmp.readAltitude());
        lcd.setCursor(0, 3);
        lcd.print("time:");
        if ((hour % 24) < 10) lcd.print("0");
        lcd.print(hour % 24);
        lcd.print(":");
        if ((minu % 60) < 10) lcd.print("0");
        lcd.print(minu % 60);
        lcd.print(":");
        if ((sec % 60) < 10) lcd.print("0");
        lcd.print(sec % 60);
        lcd.setCursor(0, 4);
        lcd.print("rate: ");
        lcd.print(derivative);
        lcd.setCursor(0,5);
        lcd.print((gps.location.isValid()));
        count_lcd = 0;
      }
      break;
    case 2: // Write data to SD Card
      count_sd ++;
      if (count_sd == 8) {
        dataFile = SD.open(filename , FILE_WRITE);
        if (dataFile) {
          dataFile.print("B");
          if (gps.time.hour() < 10) dataFile.print(F("0"));
          dataFile.print(gps.time.hour());

          if (gps.time.minute() < 10) dataFile.print(F("0"));
          dataFile.print(gps.time.minute());

          if (gps.time.second() < 10) dataFile.print(F("0"));
          dataFile.print(gps.time.second());

          if((gps.location.lat())<10.0) dataFile.print(F("0"));
          dataFile.print(gps.location.lat()*100000,0);
          dataFile.print("N");
          if((gps.location.lng())<10.0) dataFile.print(F("00"));
          else if((gps.location.lng())<100.0) dataFile.print(F("0"));
          dataFile.print(gps.location.lng()*100000,0);
          dataFile.print("WA");
          if(smooth<10)dataFile.print("0000");
          else if(smooth<100) dataFile.print("000");
          else if(smooth<1000) dataFile.print("00");
          else if(smooth<10000) dataFile.print("0");
          dataFile.print(smooth,0);
          
          if(smooth<10)dataFile.print("0000");
          else if(smooth<100) dataFile.print("000");
          else if(smooth<1000) dataFile.print("00");
          else if(smooth<10000) dataFile.print("0");
          dataFile.println(gps.altitude.meters(),0);
          
          dataFile.close();
        } else {
          lcd.setCursor(0, 5);
          lcd.print("Write fail");
        }
        count_sd = 0;
      }
    case 3: // Make beep
#define ZERO_LEVEL 0.20
      if (abs(derivative) >= ZERO_LEVEL) {
        duration = (int) 100 + 50 / abs(derivative);
      }
      else {
        duration = 0;
      }
      duration = constrain(duration, 0, 400);
      //freq = (unsigned int)smooth_derivative / 500 ;
      //lcd.setCursor(0, 5);
      //lcd.print(duration);
      freq = (int) (640 + 100 * derivative);
      //if(derivative > 0.0) freq += 300;
      //else{freq += 300;}
      freq = constrain(freq, 40, 4000);

      if (!tone_done) {
        if (duration != 0)
          tone(2, freq, duration);
        tone_done = true;
        start_beep = millis();
        old_duration = duration;
      }
      timer = start_beep + 2 * old_duration - millis();
      if (timer < 10) {
        tone_done = false;
      }
      break;
    case 4 :
      while (Serial.available() > 0) {
        gps.encode(Serial.read());
      }
      count = 0;
      break;

    default:
      break;
  }
  count++;


}
