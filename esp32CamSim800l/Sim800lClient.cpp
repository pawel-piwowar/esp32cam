#include "Sim800lClient.h"

Sim800lClient::Sim800lClient(void){
  }

void Sim800lClient::resetGsm(int resetPinNum){
      Serial.println("Reseting GSM module ...");
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
  sendATcommand("AT+CSQ","+CSQ:", 2000);
  String response = sendATcommand("AT+SAPBR=1,1","OK", 5000);
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

boolean Sim800lClient::sendFileToFtp(String localFileName, String remoteFileName){

   Serial.print("Start reading file from LITTLEFS , file name: ");
   Serial.println(localFileName);
   File dataFile = LITTLEFS.open(localFileName, FILE_READ);
   Serial.println("File opened");
   int len = dataFile.size();
   Serial.print("File length: ");
   Serial.println(len);
  
  String response = sendATcommand(String("AT+FTPPUTNAME=") + remoteFileName ,"OK",2000);
  response = sendATcommand("AT+FTPCID=1" ,"OK",2000);
  response = sendATcommand("AT+FTPPUTPATH=\"/\"" ,"OK",2000);
  response = sendATcommand("AT+FTPPUT=1","+FTPPUT: 1",5000);
  String putOkResp = "+FTPPUT: 1,1,";
  int putOkRespIndex = response.indexOf(putOkResp);
  if ( putOkRespIndex <= 0) {
    blinkRed(3, 100, 100);
    Serial.println("FTP PUT error: " + response);
    return false;
    }
  String maxLengthString = response.substring(putOkRespIndex + putOkResp.length());
  int bufLength = maxLengthString.toInt();
  Serial.println("Data buffer lenght : " + String(bufLength));
  int remainingBytes = len;
  int bytesToSend = 0;
  unsigned long startTime = millis();
  int page = 0;
  int timeout = 1000000;
  boolean timeoutOccurred = false;
  do {  
    if (remainingBytes >= bufLength ) {
      bytesToSend = bufLength;
    } else {
      bytesToSend = remainingBytes;
    }    
    int bytesSent = sendDataPage(dataFile, bytesToSend);
    page++;
    if (bytesSent < 0) {
      Serial.println("Error transferring data");
      return false;
    }
    remainingBytes = remainingBytes - bytesSent;
    Serial.println("Page number: " + String(page));    
    Serial.println("Remaining bytes : " + String(remainingBytes));
    timeoutOccurred = (millis() - startTime > timeout);
  } while (remainingBytes > 0  &&  !timeoutOccurred);

  if (timeoutOccurred) {
    Serial.println("Timeout sending file after miliseconds : " + String(timeout));
    }

  response = sendATcommand(String("AT+FTPPUT=2,0"),"OK", 5000); 
  if (response.indexOf("OK") <= 0) {
      dataFile.close();
      Serial.println("Error while closing FTP transfer");
      return false;
  }
  dataFile.close();
  blinkRed(1, 1000, 0);
  return true;

}

int Sim800lClient::sendDataPage(File dataFile, int len){
    Serial.println(String("Sending next page of data with length: ") + String(len));
    int counter = 0;
    String response = sendATcommand(String("AT+FTPPUT=2,") + len, String("+FTPPUT: 2,") + len,5000); 
    if (response.indexOf("+FTPPUT: 2,") <= 0) {
      return -1;
      }
    while (counter < len) {
        Serial2.write(dataFile.read());
        counter++; 
    } 
   Serial.println(String("Transferred bytes: ") + String(counter));
   Serial.println("Waiting for page confirmation");
   String sendResp = readLineFromSerial("OK", 60000);
   if (sendResp.indexOf("OK") <= 0 ) {
     Serial.println("Page NOT confirmed, reporting error"); 
     return -1;
    }
   Serial.println("Page confirmed"); 
   blinkRed(2, 50, 50);
   return counter;  
  }


String Sim800lClient::readLineFromSerial(String stringToRead, unsigned long timeoutMillis){
    String result;
    unsigned long startTime = millis();
    boolean ok = false;
    boolean timeoutReached = false;
    Serial.println("Received: ");
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
      Serial.println("Timeout detected after wating for " + String(timeoutMillis) + " milliseconds");
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
