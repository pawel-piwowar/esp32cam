# Esp32 Cam Sim7600 GSM module integration
Sample code for sending pictures from Esp32 Cam to SIM7600 GSM module and FTP Server.  
New picture is taken every 10 minutes.  

SIM7600 GSM module is powered up only when the FTP communication is made by setting HIGH level
on its "Power" pin using code : 
```
      digitalWrite(gsmPowerControlPin, HIGH);
          delay(500);
      digitalWrite(gsmPowerControlPin, LOW);
```

After sending the file to FTP, GSM module is switched off using AT command : "AT+CPOF" and Esp32 Cam is going to sleep with commands:

```
rtc_gpio_hold_en(GPIO_NUM_2);
esp_deep_sleep_start();
```
rtc_gpio_hold_en() function is used to prevent state change of pin used to control power of GSM module. 
Without this setting GSM can still be active after sending "AT+CPOF" command. 
We need to activate this pin when Esp32Cam wakes up using command : "rtc_gpio_hold_dis(GPIO_NUM_2);"

Esp32 Cam pins 14 and 15 are connected to SIM7600 RXD and TXD PINS allowing serial communication between camara and GSM module.
Serial communication is started with code:

```
Serial2.begin(115200,SERIAL_8N1,14,15);
```

Pictures taken with Esp32 are sent using serial interface to SD card installed in Sim7600 using AT command : "AT+CFTRANRX"  
Then image is send to FTP server with AT command : "AT+CFTPSPUTFILE"  

FTP connection parameters are read from file : "ftpParams.h"
Such file needs to be created and placed in the same directory as main program.
Example content of ftpParams.h: 

```
const String ftpServerAddress = "ftp.myserver";  
const int ftpServerPort = 21;  
const String ftpUser = "myUserName";  
const String ftpPassword = "myPass1234";  
```

## Known issues:
- Data transfer to SIM 7600 is made by sending bytes directly without any error checking.
  It sometimes results in transmission errors and corrupted jpeg files. There seems not to be a good remedy to this problem. 
  One solution could be transferring the file back to Esp32 Cam and checking its hash. 
  Another one, resigning from build in SIM 7600 FTP AT commands in favor of lower level socket communication,
  using for example this library : https://github.com/vshymanskyy/TinyGSM together with any FTP library.      