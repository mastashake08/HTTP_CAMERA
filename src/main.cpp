#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <SPI.h>
#include "esp_camera.h"
#include "camera_pins.h"

// AP Configuration
const char* AP_SSID = "ESP32-Camera";
const char* AP_PASSWORD = "";  // Open network

// Web Server
AsyncWebServer server(80);

// Camera frame buffer for streaming
camera_fb_t * fb = NULL;

// Forward declarations
void setupRoutes();

// Initialize camera
bool initCamera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_QXGA;  // 2048x1536 - Max for OV2640
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 10;  // 0-63, lower = higher quality
  config.fb_count = 2;  // Double buffering for streaming

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);  // Flip vertically if needed
    s->set_hmirror(s, 1);  // Mirror horizontally if needed
  }

  Serial.println("Camera initialized successfully");
  return true;
}

// Initialize SD card
bool initSDCard() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card Mount Failed");
    return false;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.println("SD Card initialized successfully");
  Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== ESP32-S3 Camera Server ===");
  
  // Initialize camera
  if (!initCamera()) {
    Serial.println("FATAL: Camera initialization failed!");
    while(1) { delay(1000); }
  }
  
  // Initialize SD card
  if (!initSDCard()) {
    Serial.println("WARNING: SD Card not available");
  }
  
  // Setup WiFi AP
  Serial.println("Setting up Access Point...");
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.printf("Connect to WiFi: %s\n", AP_SSID);
  
  // Setup HTTP routes (to be implemented)
  setupRoutes();
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
  Serial.println("Ready to serve requests!");
}

void setupRoutes() {
  // MJPEG Streaming endpoint
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginChunkedResponse("multipart/x-mixed-replace; boundary=frame", 
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
          Serial.println("Camera capture failed");
          return 0;
        }
        
        size_t len = 0;
        if (maxLen > fb->len + 64) {
          len = snprintf((char *)buffer, maxLen,
            "--frame\r\n"
            "Content-Type: image/jpeg\r\n"
            "Content-Length: %u\r\n\r\n", fb->len);
          memcpy(buffer + len, fb->buf, fb->len);
          len += fb->len;
          len += snprintf((char *)buffer + len, maxLen - len, "\r\n");
        }
        
        esp_camera_fb_return(fb);
        return len;
      });
    request->send(response);
  });
  
  // Capture photo and save to SD card
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }
    
    // Generate filename with timestamp
    String filename = "/photo_" + String(millis()) + ".jpg";
    File file = SD.open(filename, FILE_WRITE);
    if (!file) {
      esp_camera_fb_return(fb);
      request->send(500, "text/plain", "Failed to open file on SD card");
      return;
    }
    
    file.write(fb->buf, fb->len);
    file.close();
    
    // Send the image back to client
    AsyncWebServerResponse *response = request->beginResponse(200, "image/jpeg", fb->buf, fb->len);
    response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
    response->addHeader("X-Filename", filename);
    
    esp_camera_fb_return(fb);
    request->send(response);
    
    Serial.printf("Photo saved: %s (%u bytes)\n", filename.c_str(), fb->len);
  });
  
  // List files on SD card
  server.on("/files", HTTP_GET, [](AsyncWebServerRequest *request){
    String json = "[";
    File root = SD.open("/");
    if (!root) {
      request->send(500, "application/json", "{\"error\":\"Failed to open SD card\"}");
      return;
    }
    
    File file = root.openNextFile();
    bool first = true;
    while (file) {
      if (!file.isDirectory()) {
        if (!first) json += ",";
        json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
        first = false;
      }
      file = root.openNextFile();
    }
    json += "]";
    request->send(200, "application/json", json);
  });
  
  // Download file from SD card
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Missing file parameter");
      return;
    }
    
    String filename = request->getParam("file")->value();
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    
    if (!SD.exists(filename)) {
      request->send(404, "text/plain", "File not found");
      return;
    }
    
    request->send(SD, filename, "application/octet-stream");
  });
  
  // Delete file from SD card
  server.on("/delete", HTTP_DELETE, [](AsyncWebServerRequest *request){
    if (!request->hasParam("file")) {
      request->send(400, "text/plain", "Missing file parameter");
      return;
    }
    
    String filename = request->getParam("file")->value();
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    
    if (SD.remove(filename)) {
      request->send(200, "text/plain", "File deleted");
    } else {
      request->send(500, "text/plain", "Failed to delete file");
    }
  });
  
  // Web UI
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Camera</title>
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; background: #222; color: #fff; }
    h1 { color: #4CAF50; }
    .container { max-width: 800px; margin: 0 auto; }
    #stream { width: 100%; max-width: 640px; border: 3px solid #4CAF50; }
    button { background: #4CAF50; color: white; border: none; padding: 12px 24px; 
             font-size: 16px; margin: 5px; cursor: pointer; border-radius: 4px; }
    button:hover { background: #45a049; }
    #files { margin-top: 20px; text-align: left; }
    .file { background: #333; padding: 10px; margin: 5px 0; border-radius: 4px; }
    .file button { padding: 6px 12px; font-size: 14px; margin-left: 10px; }
    .delete { background: #f44336; }
    .delete:hover { background: #da190b; }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32-S3 Camera Server</h1>
    <p>Connect to: <strong>ESP32-Camera</strong> | IP: <strong>192.168.4.1</strong></p>
    
    <img id="stream" src="/stream" onerror="this.src='/stream';">
    
    <div>
      <button onclick="capture()">📸 Capture Photo</button>
      <button onclick="loadFiles()">📁 Refresh Files</button>
    </div>
    
    <div id="files"></div>
  </div>
  
  <script>
    function capture() {
      fetch('/capture')
        .then(response => {
          if (response.ok) {
            alert('Photo captured and saved to SD card!');
            loadFiles();
          } else {
            alert('Capture failed');
          }
        });
    }
    
    function loadFiles() {
      fetch('/files')
        .then(r => r.json())
        .then(files => {
          const div = document.getElementById('files');
          if (files.length === 0) {
            div.innerHTML = '<p>No files on SD card</p>';
            return;
          }
          div.innerHTML = '<h3>Files on SD Card:</h3>' + 
            files.map(f => `
              <div class="file">
                ${f.name} (${(f.size/1024).toFixed(1)} KB)
                <button onclick="window.open('/download?file=${f.name}')">Download</button>
                <button class="delete" onclick="deleteFile('${f.name}')">Delete</button>
              </div>
            `).join('');
        });
    }
    
    function deleteFile(name) {
      if (!confirm('Delete ' + name + '?')) return;
      fetch('/delete?file=' + encodeURIComponent(name), {method: 'DELETE'})
        .then(r => {
          if (r.ok) loadFiles();
          else alert('Delete failed');
        });
    }
    
    loadFiles();
  </script>
</body>
</html>
)rawliteral";
    request->send(200, "text/html", html);
  });
}

void loop() {
  // Main loop - server runs in background
  delay(100);
}