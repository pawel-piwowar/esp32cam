#include "esp_camera.h"
#include "SPI.h"
#include "driver/rtc_io.h"
#include <FS.h>
#include <SPIFFS.h>
#include "ftpParams.h"

#define uS_TO_S_FACTOR 1000000    //Conversion factor for micro seconds to seconds
#define TIME_TO_SLEEP  600        //Time ESP32 will go to sleep (in seconds)

#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
  #define PWDN_GPIO_NUM     32
  #define RESET_GPIO_NUM    -1
  #define XCLK_GPIO_NUM      0
  #define SIOD_GPIO_NUM     26
  #define SIOC_GPIO_NUM     27
  
  #define Y9_GPIO_NUM       35
  #define Y8_GPIO_NUM       34
  #define Y7_GPIO_NUM       39
  #define Y6_GPIO_NUM       36
  #define Y5_GPIO_NUM       21
  #define Y4_GPIO_NUM       19
  #define Y3_GPIO_NUM       18
  #define Y2_GPIO_NUM        5
  #define VSYNC_GPIO_NUM    25
  #define HREF_GPIO_NUM     23
  #define PCLK_GPIO_NUM     22
#else
  #error "Camera model not selected"
#endif

// Photo File Name to save in SPIFFS
#define FILE_PHOTO "/photo.jpg"
int gsmPowerControlPin = 2;


  
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  pinMode(gsmPowerControlPin, OUTPUT);
  rtc_gpio_hold_dis(GPIO_NUM_2);
  Serial2.begin(115200,SERIAL_8N1,14,15);
  Serial.begin(115200);
  Serial.println();
  
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("SPIFFS mounted successfully");
  }
   
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +  " Seconds");
}

void loop() {
  delay(10000);
  if (capturePhotoSaveSpiffs()) {
    powerUpGsm();
    sendPhoto();
    powerDownGsm();
  }  
  Serial.print("Going to sleep for " + String(TIME_TO_SLEEP) + " seconds" );
  rtc_gpio_hold_en(GPIO_NUM_2);
  esp_deep_sleep_start();
}

void powerUpGsm(){
    if (checkGsmState() != 0) {
      Serial.println("Powering up GSM module ...");
      digitalWrite(gsmPowerControlPin, HIGH);
          delay(500);
      digitalWrite(gsmPowerControlPin, LOW);
      String result = readLineFromSerial("PB DONE", 60000);
      boolean ok = result.indexOf("PB DONE") > 0;
      if (!ok){
           Serial.println("Reseting GSM module ...");
           sendATcommand("AT+CRESET","PB DONE", 60000);   
        }
    }     
  }

int checkGsmState(){
    String result = sendATcommand("AT","OK",3000);
    boolean ok = result.indexOf("OK") > 0;
    if (ok) {
      Serial.println("GSM module started");
      return 0;
    } else {
      Serial.println("GSM module not responding");
      return -1;
    }
  }

void powerDownGsm(){
   Serial.println("Powering down GSM module ...");
   bool ok = false;
   String result = sendATcommand("AT+CPOF","NW PDN DEACT 1", 20000);
  }

// Check if photo capture was successful
bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  Serial.print("Photo file size : ");
  unsigned int pic_sz = f_pic.size();
  Serial.println(pic_sz);
  return ( pic_sz > 100 );
}

// Capture Photo and Save it to SPIFFS
boolean capturePhotoSaveSpiffs( void ) {
  camera_fb_t * fb = NULL; // pointer
  bool ok = 0; // Boolean indicating if the picture has been taken correctly

  int attemptCount=1;
  do {
    // Take a photo with the camera
    Serial.print("Taking a photo, attempt nr: ");
    Serial.println(attemptCount);

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return false;
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.print(FILE_PHOTO);
      Serial.print(" - Size: ");
      Serial.print(file.size());
      Serial.println(" bytes");
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);

    // check if file has been correctly saved in SPIFFS
    ok = checkPhoto(SPIFFS);
    attemptCount++;
  } while ( !ok && attemptCount <=10 );
  if (! ok) {
     Serial.print("Cannot take a picture");
    }
  return ok;  
}

boolean sendPhoto(){
  String imageFileName = getFileNameFromTimeResp();
  if (!sendFileToEFS(imageFileName)){
    Serial.println("Error while sending file to EFS. Is SD card ok ?"); 
    return false;
  };
  if (! initFtp()) {
    Serial.println("Error while conecting to FTP"); 
    return false;
  };
  int ftpResult = -1;
  int retries = 3;
  while (ftpResult != 0 && retries >= 0) {     
      ftpResult = sendFileToFtp(imageFileName);
      retries--;
      if(ftpResult != 0){
       Serial.print("Error sending file to FTP, retrying, number of retires left : ");
       Serial.println(retries); 
      }
    }
  stopFtp();
  if (ftpResult == 0){
    return true;
  } else {
    Serial.print("Cannot send file to FTP");
    return false;
  }
}


String getFileNameFromTimeResp(){
  String timeResp = sendATcommand("AT+CCLK?","+CCLK:",2000);
  Serial.println("Time response: [" + timeResp + "]");
  int dateStartPosition = timeResp.indexOf("CCLK:") + 7;
  int timeStartPosition = dateStartPosition + 9;
  String datePart = timeResp.substring(dateStartPosition, dateStartPosition + 8);
  datePart.replace("/", "-");
  String timePart = timeResp.substring(timeStartPosition, timeStartPosition + 8);
  timePart.replace(":", "-");
  String fileName = datePart + "_" + timePart + ".jpg";
  Serial.println("imageFileName: [" + fileName + "]");
  return fileName;
  }

boolean initFtp(void) {
  sendATcommand("AT+CSQ","+CSQ:", 2000);
  String result = sendATcommand("AT+CFTPSSTART","+CFTPSSTART: 0", 10000);
  if (result.indexOf("ERROR") > 0) {
     stopFtp();
     sendATcommand("AT+CFTPSSTART","OK", 5000);
  }
  String loginCommand = String("AT+CFTPSLOGIN=")
     + "\"" + ftpServerAddress + "\""
     + "," + String(ftpServerPort)
     + ","+ "\"" + ftpUser + "\""
     + ","+ "\"" + ftpPassword + "\""
     + ",0";
  String response = sendATcommand(loginCommand,"+CFTPSLOGIN:",20000);
    if (response.indexOf("CFTPSLOGIN: 0") > 0) {
      return true;
    } else {
      Serial.println("FTP init error, response: " + response);
      return false;
    }
}

void stopFtp(void){
  sendATcommand("AT+CFTPSLOGOUT","OK", 2000);
  sendATcommand("AT+CFTPSSTOP","OK", 2000); 
  }

int sendFileToFtp(String imageFileName){
  String putCommand = "AT+CFTPSPUTFILE=";
  putCommand = putCommand + "\"" + imageFileName + "\"," + "2";
  String response = sendATcommand(putCommand,"+CFTPSPUTFILE:",120000);
  Serial.println("Response string : [" + response + "]");
  int resultCodeStartPosition = response.indexOf("+CFTPSPUTFILE:") + 15;
  String resultCodeString = response.substring(resultCodeStartPosition,resultCodeStartPosition + 2);
  Serial.println("Result code string : [" + resultCodeString + "]");
  int resultCode = resultCodeString.toInt();
  if (resultCode != 0) {
      Serial.print("Error executing +CFTPSPUTFILE, error code : ");
      Serial.println(resultCode);
  } else {
      Serial.println("File sent to FTP, file name: " + imageFileName);
  }   
  return resultCode;
}

boolean sendFileToEFS(String imageFileName) {
    Serial.print("Start reading file from SPIFFS , file name: ");
    Serial.println(FILE_PHOTO);
    File dataFile = SPIFFS.open(FILE_PHOTO, FILE_READ);
    Serial.println("File opened");
    int len = dataFile.size();
    Serial.print("File length: ");
    Serial.println(len);
    String uploadCommand = "AT+CFTRANRX=";
    uploadCommand = uploadCommand + "\"d:/" + imageFileName + "\"," + len;
    Serial.println("upload command: " + uploadCommand);
    Serial2.println(uploadCommand);
    String resp = Serial2.readString();
    Serial.println(resp);

  int counter = 0;
  if (dataFile) {
    unsigned long startTime = millis();
    while (dataFile.available() && (millis() - startTime < 30000) ) {
      Serial2.write(dataFile.read());
      counter++;
    }
    dataFile.close();
  }
  else {
    Serial.println("error opening file");
  }
  Serial.print("Bytes sent: ");
  Serial.println(counter);
  String sendResp = readLineFromSerial("OK", 60000);
  if (sendResp.indexOf("OK") > 0 ) {
    return true;
    }
  else {
    return false;
    }  
}

String readLineFromSerial(String stringToRead, unsigned long timeoutMilis){
    String result;
    unsigned long startTime = millis();
    boolean ok = false;
    Serial.print("Received: ");
    while (!ok & millis() - startTime < timeoutMilis) {
     if (Serial2.available()) {
       String s = Serial2.readString();
       ok = s.indexOf(stringToRead) > 0;
       Serial.print(s);
       result += s;  // append to the result string
     }
   }
   return result;
  }

String sendATcommand(String toSend, String expectedResponse, unsigned long milliseconds) {
  Serial.print("Sending AT command: ");
  Serial.println(toSend);
  Serial2.println(toSend);
  String result = readLineFromSerial(expectedResponse, milliseconds);
return result;
}