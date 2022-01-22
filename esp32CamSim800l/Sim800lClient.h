#include <Arduino.h>
#include "esp_camera.h"

class Sim800lClient
{
    private:
      String ftpServerAddress;
      int ftpServerPort;
      String ftpUser;
      String ftpPassword;
      boolean sendDataPage(uint8_t *fbBuf, size_t bufLength);
      String readLineFromSerial(String stringToRead, unsigned long timeoutMillis);
      String sendATcommand(String toSend, String expectedResponse, unsigned long milliseconds); 
      void blinkRed(int count, int onTime, int offTime);
    public:
      Sim800lClient(void);
      void resetGsm(int resetPinNum);
      boolean waitForGsmNetwork();
      boolean goToSleep();
      boolean initFtp(String _ftpServerAddress, int _ftpServerPort, String _ftpUser, String _ftpPassword);
      void stopFtp(void);
      boolean sendFileToFtp(camera_fb_t * fb, String remoteFileName);
};
