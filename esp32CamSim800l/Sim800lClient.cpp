#include "Sim800lClient.h"

Sim800lClient::Sim800lClient(void){
  }

void Sim800lClient::resetGsm(int resetPinNum){
      Serial.println("Resetting GSM module ...");
      digitalWrite(resetPinNum, LOW);
          delay(500);
      digitalWrite(resetPinNum, HIGH);  
}  


boolean Sim800lClient::waitForGsmNetwork(){
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

boolean Sim800lClient::goToSleep(){
  String result = sendATcommand("AT+CSCLK=2","OK", 5000);
  if (result.indexOf("OK") <= 0) {
      Serial.println("Cannot set SIM800l to sleep mode");
      return false;
  }
  return true;
}

boolean Sim800lClient::initFtp(String _ftpServerAddress, int _ftpServerPort, String _ftpUser, String _ftpPassword) {
  ftpServerAddress = _ftpServerAddress;
  ftpServerPort =  _ftpServerPort;
  ftpUser = _ftpUser;
  ftpPassword = _ftpPassword;
  String response = sendATcommand("AT+SAPBR=1,1","OK", 10000);
  if (response.indexOf("OK") <= 0) {
    return false;
  }

  sendATcommand(String("AT+FTPSERV=")  + "\"" + ftpServerAddress + "\"","OK", 5000);
  sendATcommand(String("AT+FTPPORT=")  + String(ftpServerPort),"OK", 5000);
  sendATcommand(String("AT+FTPUN=")  + "\"" + ftpUser + "\"" ,"OK", 5000);
  response = sendATcommand(String("AT+FTPPW=")  + "\"" + ftpPassword + "\"","OK", 5000);

  if (response.indexOf("OK") > 0) {
      return true;
    } else {
      Serial.println("FTP login error, response: " + response);
      return false;
    }
}

void Sim800lClient::stopFtp(void){
  String response = sendATcommand("AT+FTPQUIT","OK", 5000);
  if (response.indexOf("OK") <= 0) {
    Serial.println("Cannot close FTP session");
  }
  
  response = sendATcommand("AT+SAPBR=0,1","OK", 5000);
  if (response.indexOf("OK") <= 0) {
    Serial.println("Cannot close network connection");
  }
}

boolean Sim800lClient::sendFileToFtp(camera_fb_t * fb, String remoteFileName){
  String response = sendATcommand(String("AT+FTPPUTNAME=") + remoteFileName ,"OK",5000);
  response = sendATcommand("AT+FTPCID=1" ,"OK",5000);
  response = sendATcommand("AT+FTPPUTPATH=\"/\"" ,"OK",5000);
  response = sendATcommand("AT+FTPPUT=1","+FTPPUT: 1",10000);
  String putOkResp = "+FTPPUT: 1,1,";
  int putOkRespIndex = response.indexOf(putOkResp);
  if ( putOkRespIndex <= 0) {
    blinkRed(5, 200, 200);
    Serial.println("FTP PUT error: " + response);
    return false;
    }
  String maxLengthString = response.substring(putOkRespIndex + putOkResp.length());
  int bufLength = maxLengthString.toInt();

    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    int page=0;

    Serial.println("Data length : " + String(fbLen));
    Serial.println("Data buffer length : " + String(bufLength));
    
    for (size_t n=0; n<fbLen; n=n+bufLength) {
      page++;
      if ( n+bufLength > fbLen ) {
        bufLength = fbLen%bufLength;
      }
      if (! sendDataPage(fbBuf, bufLength)) {
           return false;
       }
      fbBuf += bufLength;
      Serial.println("Page no: " + String(page) + " sent, bytes: " + String(n+bufLength) + "/" + fbLen);
      String sendResp = readLineFromSerial("OK", 60000);
      if (sendResp.indexOf("OK") <= 0 ) {
        Serial.println("Page NOT confirmed, reporting error"); 
        return false;
      }  
    }   

  response = sendATcommand(String("AT+FTPPUT=2,0"),"OK", 5000); 
  if (response.indexOf("OK") <= 0) {
      Serial.println("Error while closing FTP transfer");
      return false;
  }
  Serial.println("Picture sent to FTP");
  blinkRed(1, 1000, 0);
  return true;

}

boolean Sim800lClient::sendDataPage(uint8_t *fbBuf, size_t len){
   Serial.println(String("Sending next page of data with length: ") + String(len));
   String response = sendATcommand(String("AT+FTPPUT=2,") + len, String("+FTPPUT: 2,") + len,5000); 
   if (response.indexOf("+FTPPUT: 2,") <= 0) {
      Serial.println("Error sending data page"); 
      return false;
   }
   Serial2.write(fbBuf, len);
}

String Sim800lClient::getBatteryVoltage(void) {
  String response = sendATcommand("AT+CBC","OK", 5000); 
  int voltStartIndex = response.lastIndexOf(",") + 1;
  String voltageStr = response.substring(voltStartIndex, voltStartIndex + 4);
  float voltageFloat = voltageStr.toFloat() / 1000;
  Serial.println("Voltage: " + String(voltageFloat, 2));
  return voltageStr;
}

String Sim800lClient::getSignalStrength(void) {
  String response =   sendATcommand("AT+CSQ","+CSQ:", 5000);
  int startIndex = response.lastIndexOf(":") + 2;
  String signalStr = response.substring(startIndex, startIndex + 2);
  Serial.println("Signal Strength: " + signalStr);
  return signalStr;
}


String Sim800lClient::readLineFromSerial(String stringToRead, unsigned long timeoutMillis){
    String result;
    unsigned long startTime = millis();
    boolean ok = false;
    boolean timeoutReached = false;
    Serial.print("Received: ");
    while (!ok & !timeoutReached) {
     if (Serial2.available()) {
       String s = Serial2.readString();
       ok = s.indexOf(stringToRead) > 0;
       Serial.print(s);
       result += s;  // append to the result string
     }
     timeoutReached = millis() - startTime > timeoutMillis;
   }
   if (timeoutReached) {
      Serial.println("Timeout detected after waiting for " + String(timeoutMillis) + " milliseconds");
   } 
   return result;
  }

String Sim800lClient::sendATcommand(String toSend, String expectedResponse, unsigned long milliseconds) {
  Serial.println("Sending AT command: [" + toSend + "]" 
    + " , expect : [" + expectedResponse + "]" 
    + " in " + String(milliseconds) + " milliseconds");
  Serial2.println(toSend);
  String result = readLineFromSerial(expectedResponse, milliseconds);
return result;
}


void Sim800lClient::blinkRed(int count, int onTime, int offTime) {
  for (int i = 1; i <= count; i++) {
    digitalWrite(33, LOW); // RED diode on 
    delay(onTime);
    digitalWrite(33, HIGH); // RED diode off 
    delay(offTime);
  }  
}
