#include "esp_camera.h"
#include "SPI.h"
#include "driver/rtc_io.h"
#include <FS.h>
#include "ftpParams.h"
#include "Sim800lClient.h"

#define uS_TO_M_FACTOR 60000000ULL    // Conversion factor for micro seconds to minutes

#define CLIENT_ID "B" // client id added to picture file name 
#define TIME_TO_SLEEP_MINUTES  300     // Time ESP32 will go to sleep in minutes
#define PIR_SENSOR_ENABLED false // set to true if PIR sensor is connected to PIN 13
#define UART_WAKEUP_ENABLED false // set to true if ESP32 should wakeup on serial UART data
#define LIGTH_SLEEP false // if set to true light sleep is used (required by UART wakeup), if false : deep sleep 
#define SKIP_SMALL_FILES true        // set to true if files below given size should be skipped
#define MIN_FILE_SIZE_TO_SEND 16000   // Pictures with size below this value will not be sent to FTP server (e.g dark images taken during the night).

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

int GSM_RESET_PIN = 2;
Sim800lClient sim800lClient;

  
void setup() {
  pinMode(33, OUTPUT); //RED led
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  pinMode(GSM_RESET_PIN, OUTPUT);
  digitalWrite(GSM_RESET_PIN, HIGH);
  rtc_gpio_hold_dis(GPIO_NUM_2);
  rtc_gpio_hold_dis(GPIO_NUM_13);
  
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
   config.frame_size = FRAMESIZE_SVGA;
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
  if (PIR_SENSOR_ENABLED) {
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);
  }  

  if (UART_WAKEUP_ENABLED) {
    gpio_wakeup_enable(GPIO_NUM_14, GPIO_INTR_HIGH_LEVEL);
    esp_sleep_enable_gpio_wakeup();;
  } 
  
}

void loop() {
  digitalWrite(33, LOW); // RED diode on 
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  printWakeupReason(wakeup_reason);

  if ( wakeup_reason == ESP_SLEEP_WAKEUP_TIMER || wakeup_reason == 0) {
    Serial.println("Starting ...");
    delay(60000);
  } else {
    setupCamera();
  }
  if ( wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
     if (Serial2.available()>0){
        Serial.write(Serial2.read());
    }
  }

   // Take a photo with the camera
  Serial.println("Taking a photo");
  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
      Serial.println("Camera capture failed");
      goToSleep();
      return;
  }

  size_t fbLen = fb->len;
  Serial.println("Captured image size: " + String(fbLen));
  if (SKIP_SMALL_FILES && fbLen < MIN_FILE_SIZE_TO_SEND) {
      Serial.println("Captured image size to small to send (dark image ? ). Defined minimal :  " + String(MIN_FILE_SIZE_TO_SEND));
      esp_camera_fb_return(fb);
      goToSleep();
      return;
  }

  boolean photoSent = false;
  for (int i=1; i<=3; i++) {
    Serial.println("Sending photo, attempt no: " + String(i) );
    photoSent = sendPhoto(fb);
    if (photoSent) {
      Serial.println("Photo sent in " + String(i) + " attempt");
      break;
    }
    Serial.println("Error while sending photo, resetting GSM ...");
    sim800lClient.resetGsm(GSM_RESET_PIN);
    delay(60000);
  }  
  
  esp_camera_fb_return(fb);
  sim800lClient.goToSleep();
  goToSleep();
}  

void setupCamera(void){
  sensor_t * s = esp_camera_sensor_get();
  s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, 1);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
}

void printWakeupReason(esp_sleep_wakeup_cause_t wakeup_reason){
    switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    case ESP_SLEEP_WAKEUP_GPIO : Serial.println("Wakeup caused by GPIO"); break;
    case ESP_SLEEP_WAKEUP_UART : Serial.println("Wakeup caused by UART"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

boolean sendPhoto(camera_fb_t * fb){

 if (! sim800lClient.waitForGsmNetwork()) {
      Serial.println("Cannot register to GSM network" );
      return false;
  }
  

  if (!sim800lClient.initFtp(ftpServerAddress, ftpServerPort, ftpUser, ftpPassword)) {
     Serial.println("Error while connecting to FTP");
     sim800lClient.stopFtp();
     return false;
  }   

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  String imageFileName = String(CLIENT_ID);
  imageFileName += "_" + String(random(100000, 999999));;
  imageFileName += "_volt_" + sim800lClient.getBatteryVoltage();
  imageFileName += "_sig_" + sim800lClient.getSignalStrength(); 
  imageFileName += "_" + String(wakeup_reason);
  imageFileName += ".jpg";
  if(!sim800lClient.sendFileToFtp(fb, imageFileName)){
       Serial.print("Error sending file to FTP");
       sim800lClient.stopFtp();
       return false;
   }
   
  sim800lClient.stopFtp();
  return true;

}

void goToSleep(){
  Serial.println("Going to sleep for " + String(TIME_TO_SLEEP_MINUTES) + " minutes" );
  rtc_gpio_hold_en(GPIO_NUM_2);
  rtc_gpio_hold_en(GPIO_NUM_13);
  Serial2.flush();
  Serial.println("Sleep mode activated");

  if (LIGTH_SLEEP) {
    esp_light_sleep_start();
  } else {
    esp_deep_sleep_start();
  }  
}
