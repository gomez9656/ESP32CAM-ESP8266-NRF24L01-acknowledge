#include <RHReliableDatagram.h>
#include <RH_NRF24.h>
#include <SPI.h>
#include <Arduino.h>
#include <ESP8266WiFi.h>

#define CLIENT_ADDRESS 1
#define SERVER_ADDRESS 2

//function declaration
void init_nrf24();
void print_image();
String sendPhoto();

char* ssid = "TIGO-8E48";
char* password = "4D9697504814";

WiFiClient client;

String serverName = "loona-test.000webhostapp.com";   // OR REPLACE WITH YOUR DOMAIN NAME
String serverPath = "/upload.php";     // The default serverPath should be upload.php

const int serverPort = 80;

// Singleton instance of the radio driver
RH_NRF24 driver(2, 4);  //CSN, CE

// Class to manage message delivery and receipt, using the driver declared above
RHReliableDatagram manager(driver, SERVER_ADDRESS);

//To compare with the received message, and know if the
//communication is starting or finishing
uint8_t start[] = "Start";
uint8_t finish[] = "Finish";
uint8_t last_chunk[] = "Last chunk";

/*The buffer length is being send as an array of characters,
  so first you need to store it in a char array, and then convert
  it into an int for further use
*/
char char_buffer_length[5];
int buffer_length = 0;

//pointer to the image buffer
uint8_t * image = NULL;

// Dont put this on the stack:
uint8_t buf[RH_NRF24_MAX_MESSAGE_LEN];

uint8_t final_pixel_chunk;
uint32_t chunks;

int counter = 0;
int i = 0;
int chunk_iterator = 0;
int m = 0;
bool last = false;

void setup()
{
  Serial.begin(500000);

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  init_nrf24();
}

void loop() {

  if (manager.available()) {

    // Wait for a message addressed to us from the client
    uint8_t len = sizeof(buf);
    if (manager.recvfromAck(buf, &len)) {

      if (memcmp(buf, finish, 6) == 0) {

        Serial.println((char*)buf);

        //print_image();
        sendPhoto();
        ESP.restart();
        //to avoid unexpected behavior
        if (image != NULL) {

          //free the allocated memory for the image buffer
          free(image);
          image = NULL;
        } 
        //reset the counter for the next communication
        counter = 0;
        m = 0;
        last = false;
      }
      else if (last) {
        for (i = 0; i < final_pixel_chunk; i++) {
          image[i + (27 * chunks)] = buf[i];
        }
        //reset the counter for the next communication
        counter = 0;
        m = 0;
        last = false;
      }
      else if (memcmp(buf, last_chunk, 10) == 0) {
        last = true;
      }
      else if (counter > 1) {
        chunk_iterator = buf[0];
        for (i = 1; i < 28; i++) {
          image[(i - 1) + (27 * chunk_iterator) + (27 * m * 256)] = buf[i];
        }
        if (chunk_iterator == 255) {
          m += 1;
        }
      }
      //this would be buffer length
      else if (counter == 1) {

        Serial.println((char*)buf);

        //the buffer length comes as an array of characters,
        //so convert it into an int
        memcpy(char_buffer_length, buf, 5);
        buffer_length = atoi(char_buffer_length);

        //how many pixels are left in the final chunk
        final_pixel_chunk = buffer_length % 27;

        //how many chunks of 27 bytes are
        chunks = buffer_length / 27;

        //allocate a very big memory for the image buffer
        image = (uint8_t*)calloc(buffer_length, 1);

        counter = 2;
      }
      //if the received message is equal to "Start" then the communication started
      else if (memcmp(buf, start, 5) == 0) {

        Serial.println((char*)buf);
        //add some image pointer conditional here for
        //if the connection is lost at some point
        counter = 1;

        if (image != NULL) {

          //free the allocated memory for the image buffer
          free(image);
          image = NULL;
        } 
      }
    }
  }
}

void print_image() {

  if ( image != NULL) {
    for (i = 0; i < buffer_length; i++) {
      Serial.println(image[i]);
    }
  }

}

String sendPhoto() {
  String getAll;
  String getBody;

  Serial.println("Connecting to server: " + serverName);

  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connection successful!");
    String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--RandomNerdTutorials--\r\n";

    uint32_t imageLen = buffer_length;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=RandomNerdTutorials");
    client.println();
    client.print(head);

    uint8_t *fbBuf = image;
    size_t fbLen = buffer_length;
    for (size_t n = 0; n < fbLen; n = n + 1024) {
      if (n + 1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      }
      else if (fbLen % 1024 > 0) {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }

    client.print(tail);

    int timoutTimer = 10000;
    long startTimer = millis();
    boolean state = false;

    while ((startTimer + timoutTimer) > millis()) {
      delay(100);
      while (client.available()) {

        char c = client.read();
        if (c == '\n') {
          if (getAll.length() == 0) {
            state = true;
          }
          getAll = "";
        }
        else if (c != '\r') {

          getAll += String(c);
        }
        if (state == true) {

          getBody += String(c);
        }
        startTimer = millis();
      }
      if (getBody.length() > 0) {

        break;
      }
    }

    Serial.println();
    client.stop();
    Serial.println(getBody);
  }
  else {

    getBody = "Connection to " + serverName +  " failed.";
    Serial.println(getBody);
  }
  return getBody;
}

/*
   Initialize the NRF24L01 with the RHReliableDatagram
   This class works with acknowledge bit
*/
void init_nrf24() {

  Serial.println("");
  if (!manager.init()) {

    Serial.println("init failed");
  }
  else {

    Serial.println("init succeed");
  }
  // Defaults after init are 2.402 GHz (channel 2), 2Mbps, 0dBm
  if (!driver.setChannel(125)) {
    Serial.println("change channel failed");
  }
  else {
    Serial.println("change channel succeed");
  }
}
