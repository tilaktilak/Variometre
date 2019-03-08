# variometre


Here the first Paragliding Computer I've designed : 

[![DSC00003-1.jpg](https://i.postimg.cc/CKhkJtn1/DSC00003-1.jpg)](https://postimg.cc/18d83vHh)

This is a variometer / gps / sd logger Atmega328p based project : 

Components :
* a SD card reader connected through SPI to the arduino
* a Nokia LCD screen connected to SPI
* a BMP180 (pressure sensor) connected through I2C
* a GPS Neo-6M receiver in UART.

The PCB is powerd by a 1s battery + a USB battery charger and a boost module to generate 5V.
