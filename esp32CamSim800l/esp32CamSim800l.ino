#include "esp_camera.h"
#include "SPI.h"
#include "driver/rtc_io.h"
#include <FS.h>
#include <LITTLEFS.h>
#include "ftpParams.h"
#include "Sim800lClient.h"

#define uS_TO_M_FACTOR 60000000ULL    //Conversion factor for micro seconds to minutes
#define TIME_TO_SLEEP_MINUTES  20     //Time ESP32 will go to sleep (in minutes)

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

// Photo File Name to save in LITTLEFS
#define FILE_PHOTO "/photo.jpg"
int GSM_RESET_PIN = 2;
Sim800lClient sim800lClient;

  
void setup() {
  pinMode(33, OUTPUT);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  pinMode(GSM_RESET_PIN, OUTPUT);
  digitalWrite(GSM_RESET_PIN, HIGH);
  rtc_gpio_hold_dis(GPIO_NUM_2);
  
  Serial2.begin(115200,SERIAL_8N1,14,15);
  Serial.begin(115200);
  Serial.println();
  
  if (!LITTLEFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LITTLEFS");
    ESP.restart();
  }
  else {
    delay(500);
    Serial.println("LITTLEFS mounted successfully");
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
  
   config.frame_size = FRAMESIZE_VGA;
   config.jpeg_quality = 12;
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
  Serial.println("Start taking picture");
  delay(5000);
  if (! capturePhotoSaveLITTLEFS()) {
        Serial.println("Cannot take photo" );
        goToSleep();
  }
  
  if (! sim800lClient.waitForGsmNetwork()) {
    sim800lClient.resetGsm(GSM_RESET_PIN);
    if (! sim800lClient.waitForGsmNetwork()) {
      Serial.println("Cannot register to GSM network" );
      goToSleep();
    }
  }

  sendPhoto();
  goToSleep();
}  


// Capture Photo and Save it to LITTLEFS
boolean capturePhotoSaveLITTLEFS( void ) {
  camera_fb_t * fb = NULL; // pointer

    // Take a photo with the camera
    Serial.println("Taking a photo");
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      return false;
    }

    // Photo file name
    Serial.printf("Picture file name: %s\n", FILE_PHOTO);
    File file = LITTLEFS.open(FILE_PHOTO, FILE_WRITE);

    // Insert the data in the photo file
    if (!file) {
      Serial.println("Failed to open file in writing mode");
    }
    else {
      file.write(fb->buf, fb->len); // payload (image), payload length
      Serial.print("The picture has been saved in ");
      Serial.println(FILE_PHOTO);
    }
    // Close the file
    file.close();
    esp_camera_fb_return(fb);
  return true;  
}

boolean sendPhoto(){
  String imageFileName = String(millis()) + ".jpg";;

  boolean initResult = false;
  int initRetries = 3;
  while (! initResult && initRetries >= 0) {
       initResult = sim800lClient.initFtp(ftpServerAddress, ftpServerPort, ftpUser, ftpPassword);
       initRetries --;
       if (! initResult) {
          Serial.println("Error while connecting to FTP");
          sim800lClient.stopFtp();
       }   
  }
  
  boolean ftpResult = false;
  int retries = 3;
  while (! ftpResult && retries >= 0) {  
      ftpResult = sim800lClient.sendFileToFtp(FILE_PHOTO, imageFileName);
      retries--;
      if(! ftpResult){
       Serial.print("Error sending file to FTP, retrying, number of retires left : ");
       Serial.println(retries); 
      }
    }
    
  sim800lClient.stopFtp();
  if (ftpResult){
    return true;
  } else {
    Serial.println("Cannot send file to FTP");
     return false;
  }
}

void goToSleep(){
  Serial.println("Going to sleep for " + String(TIME_TO_SLEEP_MINUTES) + " minutes" );
  sim800lClient.goToSleep();
  rtc_gpio_hold_en(GPIO_NUM_2);
  Serial.println("Sleep mode activated");
  esp_deep_sleep_start();
}
