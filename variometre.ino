#include<stdlib.h>
#include <avr/sleep.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SdFat.h>
#include <PCD8544.h>
#include <EEPROM.h>
#include <TinyGPS++.h>
TinyGPSPlus gps;
#define SIZE_MEM  13

#define     CHANGE_LEVEL    1
#define     ENTER           2
#define     EXIT            3

#define PLOT
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
#if 0
typedef struct{
    const char text[10];
    char value;
    void (* funct)();
}t_item;

typedef struct{
    const char title[15];
    t_item items[2];
    char index;
}t_page;

float level = 0.0;
void set_level(char value){
    level = (float)value;
}

void screen_stat(void){
    lcd.print("stats");
}
t_page page[2] = {"MENU PRINCIPAL",{"[-Level-] ",0x02,set_level},{"[-Stats-] ",0x00,screen_stat},0x00};

#define l_page 0
#define l_item 1
#define l_value 2
void do_menu(){
    t_page cur_page = page[0]; // Page to screen
    char button = 0x00;
    uint8_t i = 0;
    char level; // 0, 1 or 2
    bool exit_menu = false;
    while(!exit_menu){
        //display(page,level); // page with highlight level
        switch(0){//Read_button()){
            case CHANGE_LEVEL :
                if(level < 0X02) level ++;
                else level = 0x00;
                break;
            case ENTER :
                if(level == l_page){cur_page=page[++i];}
                if(level == l_item){cur_page = index++;}
                if(level == l_value){cur_page.item[cur_page.index].funct();}
                break;
            case EXIT :
                exit_menu = true;
                break;
            case Default:
                break;
        }
        }
    }
#endif
    static float batt;
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
        digitalWrite(A0,0);


        if (!sd.begin(10, SPI_FULL_SPEED)) {
            sd.initErrorHalt();
        }
        if (!bmp.begin(3)) {
            while (1) {}
        }
        if(!dataFile.open(filename,O_RDWR | O_CREAT | O_TRUNC)){
            lcd.print(F("FILE ERR"));
            lcd.setCursor(0, 5);
            lcd.print(filename);
            while (1);
        }
        /*while (Serial.available() > 0) {
          dataFile.print(Serial.read());
          dataFile.sync();
          dataFile.getWriteError();
          }*/
        // Initialize battery voltage measure
        batt += (float)analogRead(A1) * (5.0/1023.0) - 0.24;
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
        static float avg_batt;
        static uint8_t avg_batt_count = 0;
        static float ground_speed = 0.0f;

        char heure[8];
#if 0
        if(digitalRead(ENTER)==HIGH){
            delay(1);
            while(digitalRead(ENTER)==HIGH);
            do_menu();
        }
#endif
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

        // Process Derivative
        if ((old_usec - usec) >= 100) {
            raw_deriv = ((smooth    - old_alt) * 1000) /(usec - old_usec);
            old_usec = usec;
            old_alt = smooth;
        }
        derivative = 0.9 * derivative + 0.1 * raw_deriv;

        //if(abs(derivative) > 1.0){in_flight = true;}
#ifdef PLOT
        dataFile.print(usec);
        dataFile.print(",");
        dataFile.print(smooth);
        dataFile.print(",");
        dataFile.print(derivative);
        dataFile.print(",");
        dataFile.print(raw_deriv);
        dataFile.print("\n");
        if(in_flight){
            // Update stat
            if(mem.rate_max < derivative) mem.rate_max = derivative;
            // Update stat
            if(mem.alt_max < alt) mem.alt_max = alt;
        }
        // Flush data
        dataFile.sync();
        dataFile.getWriteError();
#endif

        // TONE
#define RISING_LEVEL 0.23
#define FALLING_LEVEL 0.4
#define PERIOD_MAX 400
        if (derivative >= RISING_LEVEL) {
            //duration = (int) 50 + 70 / abs(derivative);
            duration = (int) ((-derivative)*70 + 400);
        }
        else if(derivative <= -FALLING_LEVEL){
            //duration = (int) 100 + 200 / abs(derivative);
            duration = (int) ((derivative)*35 +  400);
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

                    avg_batt += (float)analogRead(A1) * (5.0/1023.0) - 0.24;
                    avg_batt_count ++;
                    if(avg_batt_count >= 30){
                        avg_batt /= avg_batt_count;

                        batt = avg_batt;

                        avg_batt = 0;
                        avg_batt_count = 0;

                    }
                    if(batt >= 4.00) lcd.print("~~~}");
                    else if(batt >= 3.80) lcd.print(" ~~}");
                    else if(batt >= 3.70) lcd.print("  ~}");
                    else if(batt >= 3.40) lcd.print("   }");
                    else if(batt >= 1.00){
                        // Got into sleep mode
                        lcd.print(batt);
                        lcd.setCursor(0,3);
                        lcd.print(" BATT LOW !");
                        lcd.setCursor(0,5);
                        lcd.print(" SHUT DOWN !");
                        while(1){
                            tone(2, 500, 1000);
                            delay(3000);
                        }
                        //dataFile.close();
                        lcd.clear();
                        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
                        cli();
                        sleep_enable();
                        sleep_bod_disable();
                        sei();
                        sleep_mode();
                    }
                    else{
                        lcd.setCursor(0,2);
                        lcd.print("Device");
                        lcd.setCursor(0,3);
                        lcd.print("Connected");
                        while(1);
                    }
                    lcd.setCursor(79, 0);

                    lcd.print((gps.location.isValid())?"{":"X");

                    lcd.setCursor(0, 1);
                    lcd.set_size(0x02);
                    lcd.print(smooth);
                    lcd.set_size(0x01);
                    lcd.print("m");
                    // Print Vx
                    if(ground_speed >= 0.0f){
                        lcd.setCursor(0,3);
                        lcd.print("Vx :");
                        lcd.print(ground_speed);
                        lcd.print(" km/h");
                    }
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
                //count_sd ++;
                if (in_flight  && gps.location.isValid()) {
                    if((strlen(gps.c_lon)==8)&&
                            (strlen(gps.c_lat)==7)&&
                            (gps.dir_lat=='N'||gps.dir_lat=='S')&&
                            (gps.dir_lon=='W'||gps.dir_lon=='E')&&
                            !(gps.time.hour()==0&&gps.time.minute()==0&&gps.time.second()==0)){
                        ground_speed = gps.speed.kmph();

                        dataFile.print("B");
                        if (gps.time.hour() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.hour());

                        if (gps.time.minute() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.minute());
                        if (gps.time.second() < 10) dataFile.print(F("0"));
                        dataFile.print(gps.time.second());

                        dataFile.print(gps.c_lat);
                        dataFile.print(gps.dir_lat);
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
                        // Flush data
                        dataFile.sync();
                        dataFile.getWriteError();
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
                count = 0;
                break;
            /*case 4 :
                count = 0;
                break;
           */ 
           default:
                break;
        }
        count++;
    }
    
void serialEvent(){
    while (Serial.available() > 0) {
        gps.encode(Serial.read());
    }
}
