//#include <TinyGPS++.h>

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SD.h>
#include <PCD8544.h>
#include <EEPROM.h>
//#include "charset.cpp"

#define SIZE_MEM  13

typedef union __attribute__((__packed__)){
    struct __attribute__((__packed__)){
	uint8_t index;
	float   alt_max;
	float   rate_max;
	uint32_t minutes;
    };
    char raw[SIZE_MEM];

}memory_t;

memory_t mem;

//TinyGPSPlus gps;
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

void read_EEPROM() {
    for (int i = 0; i < SIZE_MEM; i++){
	mem.raw[i] = EEPROM.read(i);
    }
}

void write_EEPROM() {
    for (int i = 0; i < SIZE_MEM;i++){
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
bool new_gpsD = false;
char test[] ="$GPRMC,220516,A,5133.82,N,00042.24,W,173.8,231.8,130694,004.2,W*70";

void nmea_read(char c){
    //		    1	  2    3    4    5   6      7   8    9     
    static char buf[70]={0};
    static uint8_t field=0;
    static uint8_t index=0;
    static uint8_t i = 0;
    buf[index] = c;
    if(c=='$'){
	index = 0;
	field = 0;
    }
    if(c==','){
	field++;
	uint8_t i;
	switch(field){
	    case 1:
		if(buf[1]=='G'&&buf[2]=='P'&&buf[3]=='R'&&buf[4]=='M'&&buf[5]=='C'){
		    // New packet to parse
		}
		else{
		    index = 0;
		    field = 0;
		}
		break;
		// Check field 2 : time
	    case 2:
		for(i=0;i<6;i++)
		    cur_igc.time[i] = buf[index-(6-i)];
		break;
	    case 3:
		is_gps_valid = buf[index-1];
		break;
		// Check field 
	    case 4:
		if(buf[index-1]!=','){
		    for(i=0;i<7;i++)
			cur_igc.lat[i] = buf[index-(7-i)];
		    /*cur_igc.lat[0] = buf[index-7];
		    cur_igc.lat[1] = buf[index-6];
		    cur_igc.lat[2] = buf[index-5];
		    cur_igc.lat[3] = buf[index-4];
		    cur_igc.lat[4] = buf[index-2];
		    cur_igc.lat[5] = buf[index-1];*/
		    cur_igc.lat[6] = '0';
		}
	    case 5:
		if(buf[index-1]!=','){
		    cur_igc.lat[7] = buf[index-1];
		}
		break;
	    case 6:
		if(buf[index-1]!=','){
		    for(i=0;i<8;i++)
			cur_igc.lng[i] = buf[index-(8-i)];
		    /*cur_igc.lng[0] = buf[index-8];
		    cur_igc.lng[1] = buf[index-7];
		    cur_igc.lng[2] = buf[index-6];
		    cur_igc.lng[3] = buf[index-5];
		    cur_igc.lng[4] = buf[index-4];
		    cur_igc.lng[5] = buf[index-2];
		    cur_igc.lng[6] = buf[index-1];*/
		    cur_igc.lng[7] = '0';
		}
	    case 7:
		if(buf[index-1]!=','){
		    cur_igc.lng[8] = buf[index-1];
		}
		break;
	    case 8:
		new_gpsD = true;
		break;
	}
    }
    index++;
}
void setup() {

    cur_igc.a = 'A';
    cur_igc.b = 'B';
    // Fetch last file index
    /*static uint8_t index = EEPROM.read(0x00);
      EEPROM.write(0x00, ++index);*/
    read_EEPROM();
    filename[1] = (mem.index / 100) % 10 + '0';
    filename[2] = (mem.index / 10) % 10 + '0';
    filename[3] = (mem.index) % 10 + '0';
    mem.index ++;
    Serial.begin(9600);

    // PCD8544-compatible displays may have a different resolution...
    lcd.begin(84, 48);
    // Write a piece of text on the first line...
    Serial.println("Variometre");

    // see if the card is present and can be initialized:
    if (!SD.begin(10)) {
	//Serial.println(F("CARD ERR"));
	lcd.setCursor(0, 4);
	lcd.print("S");
	// don't do anything more:
	while (1);
    }
    if (!bmp.begin(3)) {
	//Serial.println(F("BMP ERR"));
	while (1) {}
    }
    dataFile = SD.open(filename, FILE_WRITE);
    if (!dataFile) {

	lcd.print(F("E"));
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
uint8_t i = 0;
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

    //if(
    alt = bmp.readAltitude();
    // Update stat
    if(mem.alt_max < alt) mem.alt_max = alt;

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
    // Update stat
    if(mem.rate_max < derivative) mem.rate_max = derivative;


    switch (count) {
	case 1 : // Print data on screen
	    count_lcd ++;
	    if (count_lcd == 5) {

		lcd.setCursor(0, 2);
		//lcd.print(" :");
		lcd.print(smooth);
		lcd.print(" m");

		//lcd.setCursor(0, );
		//lcd.print("ALTI :");
		//lcd.print(bmp.readAltitude());
		lcd.setCursor(0, 0);
		//lcd.print("time:");
		if ((hour % 24) < 10) lcd.print("0");
		lcd.print(hour % 24);
		lcd.print(":");
		if ((minu % 60) < 10) lcd.print("0");
		lcd.print(minu % 60);
		lcd.print(":");
		if ((sec % 60) < 10) lcd.print("0");
		lcd.print(sec % 60);
		lcd.setCursor(55, 0);
		//lcd.drawBitmap(gimp_image.pixel_data,gimp_image.width,gimp_image.height);
		lcd.print("~~~}");
		lcd.setCursor(79, 0);

		lcd.print((is_gps_valid=='A')?"{":"X");
		lcd.setCursor(20,4); 
		if(derivative>0.0){

		    lcd.print("+");
		}
		lcd.print(derivative);


		lcd.print(" m/s");

		lcd.setCursor(0,5);
		if(new_gpsD){
		    lcd.print(cur_igc.time);
		    new_gpsD = false;
		}
		//lcd.print(mem.alt_max,0);
		//lcd.print("m|");

		//lcd.print(mem.rate_max);
		//lcd.print("m/s");
		//lcd.drawColumn(1, map(derivative, 0, 45, 0, 8));  // ...clipped to the 0-45C range.
		count_lcd = 0;
	    }
	    break;
	case 20: // Write data to SD Card
	    count_sd ++;
	    if (count_sd == 8) {
		dataFile = SD.open(filename , FILE_WRITE);
		if (dataFile) {
		    // TODO : Error decimal degree -> degree minute !
		    // http://www.rapidtables.com/convert/number/degrees-to-degrees-minutes-seconds.htm

		    /*dataFile.print("B");
		    if (gps.time.hour() < 10) dataFile.print(F("0"));
		    dataFile.print(gps.time.hour());

		    if (gps.time.minute() < 10) dataFile.print(F("0"));
		    dataFile.print(gps.time.minute());

		    if (gps.time.second() < 10) dataFile.print(F("0"));
		    dataFile.print(gps.time.second());

		    if ((gps.location.lat()) < 10.0) dataFile.print(F("0"));
		    //dataFile.print(gps.location.lat() * 100000, 0); //DDMMmmmN/S
		    //dataFile.print("N");
		    uint8_t degree = uint8_t(gps.location.lat());
		    uint8_t dmin = uint8_t((degree-gps.location.lat())*60);
		    uint16_t dset = (uint16_t) (degree-gps.location.lat()-dmin/60)*36000;


		    if ((gps.location.lng()) < 10.0) dataFile.print(F("00"));
		    else if ((gps.location.lng()) < 100.0) dataFile.print(F("0"));
		    dataFile.print(gps.location.lng() * 100000, 0);
		    dataFile.print("WA");*/
		    if (smooth < 10)dataFile.print("0000");
		    else if (smooth < 100) dataFile.print("000");
		    else if (smooth < 1000) dataFile.print("00");
		    else if (smooth < 10000) dataFile.print("0");
		    dataFile.print(smooth, 0);

		    if (smooth < 10)dataFile.print("0000");
		    else if (smooth < 100) dataFile.print("000");
		    else if (smooth < 1000) dataFile.print("00");
		    else if (smooth < 10000) dataFile.print("0");
		    //dataFile.println(gps.altitude.meters(), 0);

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
	    //if(i<sizeof(test)){
		//Serial.println(test[i]);
		//nmea_read(test[i]);
		i++;
	    //}
	    //else i = 0;
	    while (Serial.available() > 0) {
		char ccc = Serial.read();
		nmea_read(ccc);

		dataFile = SD.open(filename , FILE_WRITE);
		if (dataFile) {	
		dataFile.print(ccc);
		dataFile.close();
		}
	    }
	    //if(new_gpsD){
		//Serial.println("");
		//for(int j = 0;j<sizeof(cur_igc.raw);j++)
		//    Serial.print(cur_igc.raw[j]);
	       
		//new_gpsD = false;
	    //}
	    break;
	case 5 : 
	    write_EEPROM();
	    count = 0;
	    break;


	default:
	    break;
    }
    count++;
}

