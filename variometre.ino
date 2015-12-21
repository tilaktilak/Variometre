#include<stdlib.h>
#include <avr/sleep.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <SD.h>
#include <PCD8544.h>
#include <EEPROM.h>

#define SIZE_MEM  13

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

File dataFile;
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
bool new_gpsD = false;
//char test[] ="$GPRMC,220516,A,5133.82,N,00042.24\
//,W,173.8,231.8,130694,004.2,W*70";
//const char test[] ="$GPGGA,064036.289,4836.5375,N,00740.9373,E,1,04,3.2,200.2,M,,,,0000*0E";
//const char test[] ="$GPGGA,064036.289,,,,,1,04,3.2,200.2,M,,,,0000*0E";

void nmea_read(char c){
	static char buf[70]={0};
	static uint8_t field=0;
	static uint8_t index=0;
	static uint8_t i = 0;
	static uint8_t j = 0;
	static uint8_t start_alt = 0;
	buf[index] = c;
	if(c=='$'){
		index = 0;
		field = 0;
	}
	if(c==','){
		field++;
		uint8_t i;
		int8_t offset = 0;
		switch(field){
			case 1:
				if(buf[1]=='G'&&buf[2]=='P'&&buf[3]=='G'
						&&buf[4]=='G'&&buf[5]=='A'){
					// New packet to parse
				}
				else{
					index = 0;
					field = 0;
				}
				break;
			case 2:
				if(buf[index-1]!=','){
					for(i=0;i<6;i++)
						cur_igc.time[i] = buf[index-(10-i)];
				}
				break;
			case 3:// Fill LAT
				if(buf[index-1]!=','){
					offset = 0;
					for(i=0;i<7;i++){
						if(buf[index-(9-i)]=='.') offset = -1;
						cur_igc.lat[i] = buf[index-(9-i+offset)];
					}
				}
			case 4:
				if(buf[index-1]!=','){
					cur_igc.lat[7] = buf[index-1];
				}
				break;
			case 5:// Fill LON
				if(buf[index-1]!=','){
					offset = 0;
					for(i=0;i<8;i++){
						if(buf[index-(10-i)]=='.') offset = -1;
						cur_igc.lng[i] = buf[index-(10-i+offset)];
					}
				}
			case 6:
				if(buf[index-1]!=','){
					cur_igc.lng[8] = buf[index-1];
				}
				break;
			case 7:
				if(buf[index-1]!=','){
					is_gps_valid = buf[index-1];
				}
				break;
			case 9: // Save beginning of alt
				if(buf[index-1]!=','){
					start_alt = index+1;
				}
				break;
			case 10:
				if(buf[index-1]!=','){
					j = start_alt;
					while(buf[j]!='.')
						j++;
					for(i=0;i<(j-start_alt);i++){
						cur_igc.pAlt[(5-(j-start_alt))+i]=
							buf[j-((j-start_alt)-i)];
					}
				}
				break;

			case 11:
				new_gpsD = true;
				field = 0;
				break;
		}
	}
	index++;
}
void setup() {
	memset(&cur_igc,0,sizeof(cur_igc));
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
	if (!SD.begin(10)) {
		lcd.setCursor(0, 4);
		lcd.print("SD ERR");
		while (1);
	}
	if (!bmp.begin(3)) {
		while (1) {}
	}
	dataFile = SD.open(filename, FILE_WRITE);
	if (!dataFile) {
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
		raw_deriv = ((smooth	- old_alt) * 1000) /
			(usec - old_usec);
		old_usec = usec;
		old_alt = smooth;
	}
	derivative = 0.9 * derivative + 0.1 * raw_deriv;

	//if(abs(derivative) > 1.0){in_flight = true;}
#ifdef PLOT	
	dataFile = SD.open(filename, FILE_WRITE);
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
		duration = (int) 100 + 50 / abs(derivative);
	}
	else {
		duration = 0;
	}
	duration = constrain(duration, 0, 400);
	freq = (int) (640 + 110 * derivative);
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
	switch (count) {
		case 1 : // Print data on screen
			count_lcd ++;
			if (count_lcd == 5) {

				lcd.setCursor(0, 2);
				lcd.print(smooth);
				lcd.print(" m");

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

				float battery = (float)analogRead(A1) * (5.0/1023.0);
				if(battery >= 4.2) lcd.print("~~~}");
				else if(battery >= 3.85) lcd.print(" ~~}");
				else if(battery >= 3.80) lcd.print("  ~}");
				else if(battery >= 3.70) lcd.print("   }");
				else {set_sleep_mode(SLEEP_MODE_PWR_DOWN);}
				lcd.setCursor(79, 0);

				lcd.print((is_gps_valid==1)?"{":"X");
				//lcd.setCursor(49,1);
				//lcd.print((in_flight)?"Fling":"");
				lcd.setCursor(20,4); 
				if(derivative>0.0){lcd.print("+");}
				lcd.print(derivative);
				lcd.print(" m/s");

				lcd.setCursor(0,5);
				if(new_gpsD){
					//lcd.print(cur_igc.time);
					new_gpsD = false;
				}
				lcd.print(mem.alt_max,0);
				lcd.print("m|");

				lcd.print(mem.rate_max);
				lcd.print("m/s");
				count_lcd = 0;
			}
			break;
		case 2: // Read GPS & Write data to SD Card
			count_sd ++;
			if (in_flight && count_sd == 5) {
				while (Serial.available() > 0) {
					char ccc = Serial.read();
					nmea_read(ccc);
				}
				if(new_gpsD){
					if(cur_igc.time[0] != 0 &&
							cur_igc.lat[0] != 0 &&
							cur_igc.lng[0] != 0){
						dtostrf(smooth,5,0,cur_igc.pAlt);
						if(smooth<10000.0){cur_igc.pAlt[0]='0';}
						if(smooth<1000.0){cur_igc.pAlt[1]='0';}
						if(smooth<100.0){cur_igc.pAlt[2]='0';}
						if(smooth<10.0){cur_igc.pAlt[3]='0';}
#ifndef PLOT
						dataFile = SD.open(filename, FILE_WRITE);
						dataFile.println(cur_igc.raw);
						dataFile.close();
#endif
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
			count = 0;
			break;


		default:
			break;
	}
	count++;
}
