#include<stdlib.h>
#include <avr/sleep.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
//#include <SD.h>
#include <SdFat.h>
#include <PCD8544.h>
#include <EEPROM.h>
#include <TinyGPS++.h>
TinyGPSPlus gps;
#define SIZE_MEM  13


//#define NO_TONE

typedef union __attribute__((__packed__)){
    struct __attribute__((__packed__)){
        uint8_t index;
        float   alt_max;
        float   rate_max;
        uint32_t minutes;
    };
    byte raw[sizeof(uint8_t)+2*sizeof(float)+sizeof(uint32_t)];

}memory_t;

memory_t mem;

// Software SPI (slower updates, more flexible pin options):
// pin 7 - Serial clock out (SCLK)
// pin 6 - Serial data out (DIN)
// pin 5 - Data/Command select (D/C)
// pin 4 - LCD chip select (CS)
// pin 3 - LCD reset (RST)

static PCD8544 lcd;

static Adafruit_BMP085 bmp;

//File dataFile;
SdFat sd;
SdFile dataFile;
char filename[9] = "vari.igc";

// ADD of mem struct in EEPROM
#define STAT_GLOBAL 0x00

void read_EEPROM(byte offset) {
    for (uint8_t i = offset; i < SIZE_MEM; i++){
        mem.raw[i] = EEPROM.read(i);
        Serial.print(mem.raw[i]);
    }
}

void write_EEPROM(byte offset) {
    for (uint8_t i = offset; i < SIZE_MEM;i++){
        EEPROM.write(i,mem.raw[i]);
    }
}

typedef union __attribute__((__packed__)){
    struct __attribute__((__packed__)){
        char b ;
        char time[6];
        char lat[8];
        char lng[9];
        char a;
        char pAlt[5];
        char gAlt[5];
    };
    char raw[35];
}igc_t;

igc_t cur_igc;

char is_gps_valid = 'E';
//char test[] ="$GPRMC,220516,A,5133.82,N,00042.24\
//,W,173.8,231.8,130694,004.2,W*70";
//const char test[] ="$GPGGA,064036.289,4836.5375,N,00740.9373,E,1,04,3.2,200.2,M,,,,0000*0E";
//const char test[] ="$GPGGA,064036.289,,,,,1,04,3.2,200.2,M,,,,0000*0E";

void setup() {
    memset(&cur_igc,'0',sizeof(cur_igc));
    cur_igc.a = 'A';
    cur_igc.b = 'B';

    // Fetch last file index
    mem.index = 0;
    mem.alt_max = 0.0f;
    mem.rate_max = 0.0f;
    mem.minutes = 0;
    // Fetch stats in memory
    Serial.begin(9600);

    read_EEPROM(STAT_GLOBAL);
    Serial.println(mem.alt_max);
    // Prepare filename with current index
    filename[1] = (mem.index / 100) % 10 + '0';
    filename[2] = (mem.index / 10) % 10 + '0';
    filename[3] = (mem.index) % 10 + '0';
    mem.index ++;

    lcd.begin(84, 48);
    lcd.clear();

    pinMode(A0, OUTPUT);// Backlight
    pinMode(2, OUTPUT);// Tone
    pinMode(A1, OUTPUT);// Voltage
    analogWrite(A0,0x00);


    // see if the card is present and can be initialized:
    /*if (!SD.begin(10)) {
      lcd.setCursor(0, 4);
      lcd.print("SD ERR");
      while (1);
      }*/
    if (!sd.begin(10, SPI_FULL_SPEED)) {
        sd.initErrorHalt();
    }
    if (!bmp.begin(3)) {
        while (1) {}
    }
    //dataFile = SD.open(filename, FILE_WRITE);
    if(!dataFile.open(filename, O_CREAT | O_WRITE | O_EXCL)){
        lcd.print(F("FILE ERR"));
        lcd.setCursor(0, 5);
        lcd.print(filename);
        while (1);
    }
    else {
        dataFile.close();
    }
}

uint8_t count;
bool tone_done = false;
static int start_beep;
static int duration, old_duration;
static int timer;
static bool inits = true;
static bool in_flight = true;
uint8_t i_t = 0;

void loop() {

    static unsigned long sec = 0;
    static uint8_t old_minu = 0;
    static uint8_t minu = 0;
    static uint8_t hour = 23;
    static unsigned long usec = 0;
    static unsigned long old_usec = 0;
    static float alt, old_alt, smooth;
    static float raw_deriv,derivative;
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
    smooth = 0.97 * smooth + 0.03* alt;
    // Tau = 100
    // Process Derivative
    if ((old_usec - usec) >= 100) {
        raw_deriv = ((smooth    - old_alt) * 1000) /(usec - old_usec);
        old_usec = usec;
        old_alt = smooth;
    }
    derivative = 0.9 * derivative + 0.1 * raw_deriv;

    //if(abs(derivative) > 1.0){in_flight = true;}
#ifdef PLOT
    //dataFile = SD.open(filename, FILE_WRITE);

    dataFile.open(filename, O_CREAT | O_WRITE | O_EXCL);
    dataFile.print(usec);
    dataFile.print(",");
    dataFile.print(smooth);
    dataFile.print(",");
    dataFile.print(derivative);
    dataFile.print(",");
    dataFile.print(raw_deriv);
    dataFile.print("\n");
    dataFile.close();
    if(in_flight){
        // Update stat
        if(mem.rate_max < derivative) mem.rate_max = derivative;
        // Update stat
        if(mem.alt_max < alt) mem.alt_max = alt;
    }
#endif

    // TONE
#define ZERO_LEVEL 0.23
    if (abs(derivative) >= ZERO_LEVEL) {
        if(derivative>0){duration = (int) 50 + 70 / abs(derivative);}
        else{duration = (int) 100 + 200 / abs(derivative);}
    }
    else {
        duration = 0;
    }
    duration = constrain(duration, 0, 400);
    freq = (int) (640 + 200 * derivative);
    if(derivative<0)freq-=250;
    freq = constrain(freq, 40, 4000);

    if (!tone_done) {
        if (duration != 0)
#ifndef NO_TONE
            tone(2, freq, duration);
#endif
        tone_done = true;
        start_beep = millis();
        old_duration = duration;
    }
    timer = start_beep + 2 * old_duration - millis();
    if (timer < 10) {
        tone_done = false;
    }
    switch (count) {
        case 1 : // Print data on screen
            count_lcd ++;
            if (count_lcd == 5) {


                lcd.setCursor(0, 0);

                if ((hour % 24) < 10) lcd.print("0");
                lcd.print(hour % 24);
                lcd.print(":");
                if ((minu % 60) < 10) lcd.print("0");
                lcd.print(minu % 60);
                lcd.print(":");
                if ((sec % 60) < 10) lcd.print("0");
                lcd.print(sec % 60);
                lcd.setCursor(55, 0);

                float battery = (float)analogRead(A1) * (5.0/1023.0) - 0.24;
                if(battery >= 4.2) lcd.print("~~~}");
                else if(battery >= 3.80) lcd.print(" ~~}");
                else if(battery >= 3.70) lcd.print("  ~}");
                else if(battery >= 3.60) lcd.print("   }");
                else {
                    // Got into sleep mode
                    lcd.print(battery);
                    dataFile.close();
                    lcd.clear();
                    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
                    cli();
                    sleep_enable();
                    sleep_bod_disable();
                    sei();
                    sleep_mode();
                }
                lcd.setCursor(79, 0);

                lcd.print((gps.location.isValid())?"{":"X");

                lcd.setCursor(0, 1);
                lcd.set_size(0x02);
                lcd.print(smooth);
                lcd.set_size(0x01);
                lcd.print("m");
                lcd.set_size(0x02);
                lcd.setCursor(0,4); 
                if(derivative>0.0){lcd.print("+");}
                lcd.print(derivative);
                lcd.set_size(0x01);
                lcd.print("m/s");
                count_lcd = 0;
            }
            break;
        case 2: // Read GPS & Write data to SD Card
            count_sd ++;
            if (in_flight && count_sd == 5 && gps.location.isValid()) {
                if((strlen(gps.c_lon)==8)&&
                        (strlen(gps.c_lat)==7)&&
                        (gps.dir_lat=='N'||gps.dir_lat=='S')&&
                        (gps.dir_lon=='W'||gps.dir_lon=='E')&&
                        !(gps.time.hour()==0&&gps.time.minute()==0&&gps.time.second()==0)){
                    //                        dataFile = SD.open(filename , FILE_WRITE);
                    dataFile.open(filename, O_CREAT | O_WRITE | O_EXCL);
                    //if (dataFile) {
                    if(1){
                        dataFile.print("B");
                        if (gps.time.hour() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.hour());

                        if (gps.time.minute() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.minute());
                        if (gps.time.second() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.second());

                        //if((gps.location.lat())<10.0) dataFile.print(F("0"));
                        //dataFile.print(gps.location.lat()*100000,0);
                        dataFile.print(gps.c_lat);
                        dataFile.print(gps.dir_lat);
                        //dataFile.print("N");
                        //if((gps.location.lng())<10.0) dataFile.print(F("00"));
                        //else if((gps.location.lng())<100.0) dataFile.print(F("0"));
                        //dataFile.print(gps.location.lng()*100000,0);
                        dataFile.print(gps.c_lon);
                        dataFile.print(gps.dir_lon);
                        dataFile.print("A");
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

                        dataFile.sync();
                        dataFile.getWriteError();
                        dataFile.close();
                    }
                }
                count_sd = 0;
                }
                break;
                case 3 :
                if(in_flight){	
                    if(minu >= old_minu+10){
                        mem.minutes+= (minu-old_minu);
                        old_minu = minu;
                    }
                    write_EEPROM(STAT_GLOBAL);
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
