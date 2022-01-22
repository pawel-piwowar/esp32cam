#include "esp_camera.h"
#include "SPI.h"
#include "driver/rtc_io.h"
#include <FS.h>
#include "ftpParams.h"

#define uS_TO_M_FACTOR 60000000ULL    //Conversion factor for micro seconds to minutes
#define TIME_TO_SLEEP_MINUTES  10     //Time ESP32 will go to sleep (in minutes)

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

int gsmPowerControlPin = 2;
int GSM_RESET_PIN = 13;


  
void setup() {
  pinMode(33, OUTPUT); //RED led
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  pinMode(gsmPowerControlPin, OUTPUT);
  rtc_gpio_hold_dis(GPIO_NUM_2);
  pinMode(GSM_RESET_PIN, OUTPUT);

  Serial2.begin(115200,SERIAL_8N1,14,15);
  Serial.begin(115200);
  Serial.println(); 
   
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
  
   //config.frame_size = FRAMESIZE_UXGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
   //config.frame_size = FRAMESIZE_VGA;
   config.frame_size = FRAMESIZE_XGA;
   config.jpeg_quality = 12; //10-63 lower number means higher quality
   config.fb_count = 1;

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  Serial.println("Setting up ESP32 to sleep every " + String(TIME_TO_SLEEP_MINUTES) +  " minutes");
  esp_sleep_enable_timer_wakeup(uS_TO_M_FACTOR * TIME_TO_SLEEP_MINUTES);

}

void loop() {
  Serial.println("Starting ...");
  delay(10000);
  Serial.println("Taking a photo");
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
      Serial.println("Camera capture failed");
      blinkRed(3, 200, 200);
      goToSleep();
  }

  boolean gsmPowerUpOk = false;
  for (int i=0; i<3; i++) {
    gsmPowerUpOk = powerUpGsm();
    if (gsmPowerUpOk) {
      break;
    } 
    Serial.println("Cannot power up GSM module, hard reseting...");
    resetGsm(GSM_RESET_PIN);
    delay(10000);
  }  

  if (! gsmPowerUpOk) {
       Serial.println("Cannot start GSM module");
     blinkRed(4, 200, 200);
     esp_camera_fb_return(fb);
     goToSleep();
  }   

  if (!waitForGsmNetwork()) {
    resetGsm(GSM_RESET_PIN);
    if (!waitForGsmNetwork()) {
      Serial.println("Cannot register to GSM network" );
      esp_camera_fb_return(fb);
      goToSleep();
    }
  }

  boolean sendPhotoOk = false; 
  for (int i=0; i<3; i++) {
    sendPhotoOk = sendPhoto(fb);
    if (sendPhotoOk) {
      break;
    }
  }
  
  if (! sendPhotoOk) {
     Serial.println("Cannot send file to FTP");
     blinkRed(5, 200, 200);
     goToSleep();
  }
  blinkRed(1, 2000, 100);
  powerDownGsm();
  goToSleep();
}

void goToSleep(){
  Serial.println("Going to sleep for " + String(TIME_TO_SLEEP_MINUTES) + " minutes" );
  rtc_gpio_hold_en(GPIO_NUM_2);
  Serial.println("Sleep mode activated");
  esp_deep_sleep_start();
}

void resetGsm(int resetPinNum){
      Serial.println("Resetting GSM module ...");
      digitalWrite(resetPinNum, LOW);
          delay(200);
      digitalWrite(resetPinNum, HIGH);  
} 

boolean powerUpGsm(){
    for (int i=0; i<3; i++) {
      if (checkGsmState()) {
         return true;
      }  
    }
    Serial.println("Cannot get answer, powering up GSM module ...");
    digitalWrite(gsmPowerControlPin, HIGH);
    delay(500);
    digitalWrite(gsmPowerControlPin, LOW);
    readLineFromSerial("PB DONE", 20000);
    for (int i=0; i<3; i++) {
      if (checkGsmState()) {
         return true;
      }  
    }
    return false;
  }

boolean checkGsmState(){
    String result = sendATcommand("AT","OK",5000);
    boolean ok = result.indexOf("OK") > 0;
    if (ok) {
      Serial.println("GSM module started");
      return true;
    } else {
      Serial.println("GSM module not responding");
      return false;
    }
  }

void powerDownGsm(){
   Serial.println("Powering down GSM module ...");
   bool ok = false;
   String result = sendATcommand("AT+CPOF","NW PDN DEACT 1", 20000);
  }

boolean waitForGsmNetwork(){
  sendATcommand("AT","OK", 5000);
  String result;
  for (int i=1; i < 10; i++){ //Try 10 times…
      Serial.println("Waiting for network, attempt " + String(i) );
      result = sendATcommand("AT+CREG?","+CREG: 0,1", 5000);
      if (result.indexOf("+CREG: 0,1") > 0) {
        return true;
      }
      delay(2000);
  }
  return false;
}


boolean sendPhoto(camera_fb_t * fb){
  String imageFileName = getFileNameFromTimeResp();
  if (!sendFileToEFS(imageFileName, fb)){
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

boolean sendFileToEFS(String imageFileName, camera_fb_t * fb) {

    uint8_t *fbBuf = fb->buf;
    size_t len = fb->len;
    
    Serial.print("File length: ");
    Serial.println(len);
    String uploadCommand = "AT+CFTRANRX=";
    uploadCommand = uploadCommand + "\"d:/" + imageFileName + "\"," + len;
    Serial.println("upload command: " + uploadCommand);
    Serial2.println(uploadCommand);
    String resp = Serial2.readString();
    Serial.println(resp);

    Serial2.write(fbBuf, len);

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

void blinkRed(int count, int onTime, int offTime) {
  for (int i = 1; i <= count; i++) {
    digitalWrite(33, LOW); // RED diode on 
    delay(onTime);
    digitalWrite(33, HIGH); // RED diode off 
    delay(offTime);
  }  
}
