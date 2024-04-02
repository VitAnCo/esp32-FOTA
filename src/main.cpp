#include <Arduino.h>

#include <WiFi.h>
#include <FirebaseESP32.h>
#include "FS.h"
#include "SPIFFS.h"
#include "HTTPClient.h"

#include <HardwareSerial.h>
// Provide the token generation process info.
#include <addons/TokenHelper.h>


/* States */
#define     WAIT_NEW_UPDATE     0
#define     NEW_UPDATE_DWN      1
#define     RQST_NEW_UPDATE     2
#define     SEND_HEADER         0x50
#define     SEND_CODE           0x74
#define     NEW_UPDATE_DENIED   0x06


/* NodeMcu To Gateway messages */
#define   NEW_UPDATE_REQUEST     0x01u

/* Gateway To NodeMCU errors */
#define   SYSTEM_STATE_UNDIFINED     0x02u
#define   GATEWAY_BUSY               0x03u
#define   INVALID_REQUEST            0X04u
#define   REQUEST_ACCEPTED           0x05u
#define   REQUEST_REFUSED            0x06u
#define   ESP_SEND_HEADER            0x07u
#define   HEADR_RECEIVED             0x08u
#define   HEADR_ERROR                0x09u
#define   PACKET_RECEIVED            0x0Au
#define   LAST_PACKET_RECEIVED       0x0Bu
#define   SEND_NEXT_PACKET           0x0Cu
#define   ESP_DOWNLOAD_DONE          0x0Du


#define FORMAT_SPIFFS_IF_FAILED true

#define WIFI_SSID "NhaTro_107_TangTret"
#define WIFI_PASSWORD "68686868"

#define FIREBASE_HOST "fota-28ca6-default-rtdb.firebaseio.com"             // the project name address from firebase id
#define FIREBASE_AUTH "FPMJIFd7rPPSoGV3865kkl4wDCE94znk3ETuMoHe"       // the secret key generated from firebase
#define STORAGE_BUCKET_ID "https://firebasestorage.googleapis.com/v0/b/fota-28ca6.appspot.com/o/blink1.hex?alt=media&token=4e5113c6-6139-462c-b58c-4e0514dfe305"

#define FORMAT_SPIFFS_IF_FAILED true
const char* filename = "/blink1.bin";


HardwareSerial S(2); //rx, tx

File f;
/* 3. Define the Firebase Data object */
FirebaseData fbdo;

/* 4, Define the FirebaseAuth data for authentication data */
FirebaseAuth auth;

/* Define the FirebaseConfig data for config data */
FirebaseConfig config;

int new_flag;

int state = WAIT_NEW_UPDATE;

unsigned int app_crc, app_size;

unsigned char app_nodeID;

unsigned char header[4];

String firmware_url = "";

void send_file();
int download_firmware();

void setup() {
  // put your setup code here, to run once:
  /* Intitialize Serial Communication */
  Serial.begin(115200);
  S.begin(115200,SERIAL_8N1, 16, 17); //rx, tx
  
  /* Connect to Wifi */
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("\n");
  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }
  
  /* Connect to Firebase */
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.reconnectNetwork(true);

  // Since v4.4.x, BearSSL engine was used, the SSL buffer need to be set.
  // Large data transmission may require larger RX buffer, otherwise connection issue or data read time out can be occurred.
  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);

  /* Initialize the library with the Firebase authen and config */
  Firebase.begin(&config, &auth);
  delay(500);

  Serial.println("SPIFFS Init");
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
        return;
   }
  Serial.println("INIT Done");
  delay(5000);
}


void loop() {
  switch (state){
    case (WAIT_NEW_UPDATE):{
      if (Firebase.getInt(fbdo,"NewUpdate")){
        new_flag = fbdo.intData();
      }
      Serial.println(new_flag);
      if (new_flag == 0){
        Serial.println(new_flag);
        state = RQST_NEW_UPDATE;
      }
    }
    break;

    case RQST_NEW_UPDATE: {
      Serial.println("Send update request to Gateway ...");
      S.write(NEW_UPDATE_REQUEST);
      delay(100);
      while (S.available() == 0);
      unsigned char response = S.read();
      if (response == SYSTEM_STATE_UNDIFINED){
        delay(100);
      }

      else if (response == GATEWAY_BUSY) {
        delay(10000);
      }

      else if (response == INVALID_REQUEST) {
        state == WAIT_NEW_UPDATE;
      }

      else if (response = REQUEST_ACCEPTED){
        state = NEW_UPDATE_DWN;
      }

      else if (response = REQUEST_REFUSED){
        state = NEW_UPDATE_DENIED;
      }
      
    }
    break;

    case NEW_UPDATE_DENIED: {
      Firebase.setInt(fbdo,"NewUpdate", 0);
      Firebase.setInt(fbdo,"NewUpdate", 0);
      state = WAIT_NEW_UPDATE;
    }

    break;

    case NEW_UPDATE_DWN: {
      if (Firebase.getInt(fbdo,"App_crc")){
        app_crc = fbdo.intData();
      }
      if (Firebase.getInt(fbdo,"Node_ID")){
        app_nodeID = fbdo.intData();
      }
      Serial.println(app_crc);
      Serial.println(app_nodeID);

      if (Firebase.getString(fbdo,"url")){
        firmware_url = fbdo.stringData();
      }
      Serial.println(firmware_url);

      int dowload_check = 1;
      while (dowload_check){
        dowload_check = download_firmware();
      }
      state = SEND_HEADER;
    }
    break;

    case SEND_HEADER : {
      int Ack = 0 ;
      while (S.available() == 0);
      Ack = S.read();
      if (Ack == ESP_SEND_HEADER){
        Serial.println("Sending header");
        header[0] = app_size & 0xff ;
        header[1] = (app_size>>8) & 0xff ;
        header[2] = (app_size>>16) & 0xff ;
        header[3] = (app_size>>24) & 0xff ;

        // header[4] = app_crc & 0xff ;
        // header[5] = (app_crc>>8) & 0xff ;
        // header[6] = (app_crc>>16) & 0xff ;
        // header[7] = (app_crc>>24) & 0xff ;

        header[4] = app_nodeID ;

        /* Send header */
        for (int i = 0 ; i < 5 ; i++){
          S.write(header[i]);
        }
        Serial.print("Done sending !");
        
        /* wait for ACK*/
        while ( S.available() == 0);
        Ack = S.read();
        Serial.println(Ack);
        
        if (Ack == HEADR_RECEIVED) {
          state = SEND_CODE;
        }
       }
    }
    break ;

    case SEND_CODE : 
        {
          /* Send new file and clear flag */    
          send_file();
          while ( S.available() == 0);
          unsigned char Ack = S.read();
          Serial.println (Ack); 
          if (Ack == ESP_DOWNLOAD_DONE)
          {
            Firebase.setInt(fbdo,"NewUpdate" , 1); 
          }
          state = WAIT_NEW_UPDATE ;
        }
        break;
  }
  
}

int download_firmware() {
  int error_code = 0;
  HTTPClient http;
  SPIFFS.format();
  f = SPIFFS.open(filename, "w");

  Serial.println("\nDownload firmware file...\n");
  http.begin(firmware_url);

  int httpCode = http.GET();
  if(httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if(httpCode == HTTP_CODE_OK) {
          http.writeToStream(&f);
          app_size = f.size();
          Serial.printf("Content-Length: %d\n", (http.getSize()));
          Serial.printf("file-size: %d\n", app_size);
      }
      f.close();
      http.end();
  } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
      error_code = 1;
  }
  return error_code;
}


void send_file(){
   unsigned char x = 0 ;
   unsigned char y = 0 ;
   Serial.print("here");
   f = SPIFFS.open(filename, "r");
   if (!f){
    Serial.println("file open failed");
    return;
   }
   else {
    Serial.println("Reading Data from File:");
    Serial.println(app_size);
    Serial.println((f.size()));
    int xsize = app_size ;
    int num_packets = (app_size / 1024) + 1 ;
    int Ack = 0 ;
    //Data from file
    for(int i=0;i<num_packets;i++) //Read upto complete file size
    {
    while ( S.available() == 0);
    Ack = S.read();
    Serial.println (Ack);
    if (Ack == SEND_NEXT_PACKET) {
      if (xsize > 1024){
            for (int j = 0; j <1024 ; j++)
            {
                x = f.read();
                Serial.println (x);
                S.write(x);
            }
            xsize -= 1024 ;
            while ( S.available() == 0);
            Ack = S.read();
            Serial.println (Ack);  
        }
        else if ((xsize < 1024) ,(xsize > 0)) {
          int remaining = app_size % 1024 ;
          for (int j = 0 ; j <remaining ; j++ )
          {
            x = f.read();
            S.write(x);
            Serial.println (x);
          }
          xsize -= remaining ;
          while ( S.available() == 0);
          Ack = S.read();
          Serial.println (Ack); 
        }
    }
  }
  f.close();  //Close file
  Serial.println("File Closed");
  }
}