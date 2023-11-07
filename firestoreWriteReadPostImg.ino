
/**
 * Created by Vito
 */


#include <Arduino.h>
#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif __has_include(<WiFiNINA.h>)
#include <WiFiNINA.h>
#elif __has_include(<WiFi101.h>)
#include <WiFi101.h>
#elif __has_include(<WiFiS3.h>)
#include <WiFiS3.h>
#endif

#include <Firebase_ESP_Client.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

//camera
#include "esp_camera.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <LittleFS.h>
#include <FS.h>

#include <ArduinoJson.h>

// Provide the token generation process info.
#include <addons/TokenHelper.h>

/* 2. Define the API Key */
#define API_KEY "AIzaSyCjr5U8W62B2LGcdZiRl_A9FX5FEUzawRo"

/* 3. Define the project ID */
#define FIREBASE_PROJECT_ID "segrebox"

/* 4. Define the user Email and password that already registered or added in your project */
#define USER_EMAIL "qwerty@gmail.com"
#define USER_PASSWORD "12345678"

// Insert Firebase storage bucket ID, e.g., bucket-name.appspot.com
#define STORAGE_BUCKET_ID "segrebox.appspot.com"

String getPhotoPath() {
  return String("/") + WiFi.macAddress() + ".jpg";
}

String getBucketPhoto() {
  return String("/waste-detections/") + WiFi.macAddress() + ".jpg";
}

// OV2640 camera module pins (CAMERA_MODEL_AI_THINKER)
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

boolean takeNewPhoto = true;

// Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;

void fcsUploadCallback(FCS_UploadStatusInfo info);

bool taskCompletedCam = false;

bool taskCompleted = false;

unsigned long dataMillis = 0;

int population;

String detectionResultString;

#if defined(ARDUINO_RASPBERRY_PI_PICO_W)
WiFiMulti multi;
#endif

void capturePhotoSaveLittleFS(void) {
  // Dispose of the first pictures because of bad quality
  camera_fb_t* fb = NULL;
  // Skip the first 3 frames (increase/decrease the number as needed).
  for (int i = 0; i < 4; i++) {
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
  }

  // Turn on the flash
  pinMode(4, OUTPUT); // Built-in LED pin
  digitalWrite(4, HIGH); // Turn on the flash

  // Take a new photo
  fb = NULL;
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }

  // Turn off the flash
  digitalWrite(4, LOW); // Turn off the flash
  pinMode(4, INPUT); // Release the LED pin

  // Photo file name
  String photoPath = getPhotoPath();
  Serial.printf("Picture file name: %s\n", photoPath.c_str());
  File file = LittleFS.open(photoPath.c_str(), FILE_WRITE);

  // Insert the data into the photo file
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  } else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.print("The picture has been saved in ");
    Serial.print(photoPath.c_str());
    Serial.print(" - Size: ");
    Serial.print(fb->len);
    Serial.println(" bytes");
  }
  // Close the file
  file.close();
  esp_camera_fb_return(fb);
}

void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    ESP.restart();
  } else {
    delay(500);
    Serial.println("LittleFS mounted successfully");
  }
}

void initCamera() {
  // OV2640 camera module
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
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }
}

void numberDetect(int number) {
  if (number == 5705705) {
    Serial.println("This works");
  }
}

void stringDetect(String detectionResultString) {
  if (detectionResultString == "qwerty") {
    Serial.println("This also works");
  }
}

// Function to write data to Firebase Firestore
void writeDataToFirebase() {
  // Create a FirebaseJson object to set the data you want to write
  FirebaseJson content;

  content.set("fields/detail/stringValue", "...");
  content.set("fields/detection-result/stringValue", "plastic");
  content.set("fields/event/stringValue", "uuid event");
  content.set("fields/imageUrl/stringValue", "imageurl");
  content.set("fields/location/geoPointValue/latitude", 1.486284);
  content.set("fields/location/geoPointValue/longitude", 1.5555);
  content.set("fields/name/stringValue", "nama bin");
  content.set("fields/fill-levels/mapValue/fields/other/doubleValue", 0.2);
  content.set("fields/fill-levels/mapValue/fields/paper/doubleValue", 0.3);
  content.set("fields/fill-levels/mapValue/fields/plastic/doubleValue", 0.4);

  // Specify the document path
  String documentPath = "trash-bins/" + WiFi.macAddress();

  Serial.print("Creating document... ");

  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
    Serial.printf("OK\n%s\n\n", fbdo.payload().c_str());
  else
    Serial.println(fbdo.errorReason());
}

// Function to get data from Firebase Firestore
void getDataFromFirebase() {
  String documentPath = "trash-bins/" + WiFi.macAddress();
  //String mask = "`fill-levels`";
  String mask = "`detection-result`";

  if (Firebase.Firestore.getDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), mask.c_str())) {
    Serial.println("Data fetched successfully.");

    // Parse the JSON data using ArduinoJson
    DynamicJsonDocument doc(2048);  // Adjust the size according to your data
    DeserializationError error = deserializeJson(doc, fbdo.payload());

    if (error) {
      Serial.print("Failed to parse JSON data: ");
      Serial.println(error.c_str());
    } else {
      double otherLevel = doc["fields"]["fill-levels"]["mapValue"]["fields"]["other"]["doubleValue"];
      double plasticLevel = doc["fields"]["fill-levels"]["mapValue"]["fields"]["plastic"]["doubleValue"];
      double paperLevel = doc["fields"]["fill-levels"]["mapValue"]["fields"]["paper"]["doubleValue"];

      detectionResultString = doc["fields"]["detection-result"]["stringValue"].as<String>();

      Serial.println("Detection result: ");
      Serial.print(detectionResultString);

      // Serial.print("other fill level: ");
      // Serial.print(otherLevel * 100, 1); // Display with one decimal place
      // Serial.println("%");

      // Serial.print("plastic fill level: ");
      // Serial.print(plasticLevel * 100, 1); // Display with one decimal place
      // Serial.println("%");

      // Serial.print("paper fill level: ");
      // Serial.print(paperLevel * 100, 1); // Display with one decimal place
      // Serial.println("%");
    }
  } else {
    Serial.print("Failed to fetch data: ");
    Serial.println(fbdo.errorReason());
  }
}

void updateFirestoreFieldValue(const String& documentPath, const String& fieldPath, const String& newValue) {
  // Create a FirebaseJson object to hold the data you want to update
  FirebaseJson content;

  content.clear();

  // Set the new value for the field you want to update
  content.set(fieldPath.c_str(), newValue);

  String variablesUpdated = "name";

  Serial.print("Updating document... ");

  if (Firebase.Firestore.patchDocument(&fbdo, FIREBASE_PROJECT_ID, "" /* databaseId can be (default) or empty */, documentPath.c_str(), content.raw(), variablesUpdated.c_str()))
    Serial.printf("OK\n%s\n\n", fbdo.payload().c_str());
  else
    Serial.println(fbdo.errorReason());
}

void shotAndSend() {
  if (takeNewPhoto) {
    capturePhotoSaveLittleFS();
    takeNewPhoto = false;
  }
  delay(1);
  if (Firebase.ready()) {
    Serial.print("Uploading picture... ");

    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, getPhotoPath().c_str(), mem_storage_type_flash, getBucketPhoto().c_str(), "image/jpeg", fcsUploadCallback)) {
      Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
    } else {
      Serial.println(fbdo.errorReason());
    }
  }
  FirebaseJson content;

  content.set("fields/url/stringValue", "waste-detections/" + WiFi.macAddress() + ".jpg" );

  // Specify the document path
  String documentPath = "waste-detections/" + WiFi.macAddress();

  Serial.print("Creating photo document... ");

  if (Firebase.Firestore.createDocument(&fbdo, FIREBASE_PROJECT_ID, "", documentPath.c_str(), content.raw()))
    Serial.printf("OK\n%s\n\n", fbdo.payload().c_str());
  else
    Serial.println(fbdo.errorReason());

}

void setup() {
  Serial.begin(115200);
  WiFiManager wm;

  bool res;
  res = wm.autoConnect("AutoConnectAP", "password"); // Set your SSID and password

  if (!res) {
    Serial.println("Failed to connect");
    // ESP.restart();
  } else {
    Serial.println("Connected...yeey :)");
  }

  initLittleFS();
  // Turn off the 'brownout detector'
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  initCamera();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);

  /* Assign the API key (required) */
  config.api_key = API_KEY;

  /* Assign the user sign-in credentials */
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  /* Assign the callback function for the long-running token generation task */
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h

  // Comment or pass false value when WiFi reconnection will be controlled by your code or third-party library, e.g., WiFiManager
  Firebase.reconnectNetwork(true);

  // Since v4.4.x, BearSSL engine was used, the SSL buffer needs to be set.
  // Large data transmission may require a larger RX buffer, otherwise connection issues or data read timeout can occur.
  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);

  // Limit the size of the response payload to be collected in FirebaseData
  fbdo.setResponseSize(2048);

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
  if (Firebase.ready() && (millis() - dataMillis > 15000 || dataMillis == 0)) {
    dataMillis = millis();

    if (!taskCompleted) {
      taskCompleted = true;
      writeDataToFirebase(); // Call the write data function
    }

    if (detectionResultString == "takePic"){
      shotAndSend();
      takeNewPhoto = true;
    }
    

    getDataFromFirebase(); // Call the get data function

    String documentPath = "trash-bins/" + WiFi.macAddress();
    String fieldPath = "fields/name/stringValue";
    String newValue = "asuuuuuu";

    // updateFirestoreFieldValue(documentPath, fieldPath, newValue);
  }
}



void fcsUploadCallback(FCS_UploadStatusInfo info) {
  if (info.status == firebase_fcs_upload_status_init) {
    Serial.printf("Uploading file %s (%d) to %s\n", info.localFileName.c_str(), info.fileSize, info.remoteFileName.c_str());
  } else if (info.status == firebase_fcs_upload_status_upload) {
    Serial.printf("Uploaded %d%s, Elapsed time %d ms\n", (int)info.progress, "%", info.elapsedTime);
  } else if (info.status == firebase_fcs_upload_status_complete) {
    Serial.println("Upload completed\n");
    FileMetaInfo meta = fbdo.metaData();
    Serial.printf("Name: %s\n", meta.name.c_str());
    Serial.printf("Bucket: %s\n", meta.bucket.c_str());
    Serial.printf("contentType: %s\n", meta.contentType.c_str());
    Serial.printf("Size: %d\n", meta.size);
    Serial.printf("Generation: %lu\n", meta.generation);
    Serial.printf("Metageneration: %lu\n", meta.metageneration);
    Serial.printf("ETag: %s\n", meta.etag.c_str());
    Serial.printf("CRC32: %s\n", meta.crc32.c_str());
    Serial.printf("Tokens: %s\n", meta.downloadTokens.c_str());
    Serial.printf("Download URL: %s\n\n", fbdo.downloadURL().c_str());
  } else if (info.status == firebase_fcs_upload_status_error) {
    Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
  }
}
