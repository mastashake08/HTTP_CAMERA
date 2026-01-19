# HTTP ESP Camera - AI Agent Instructions

## Project Overview
ESP32-S3 based HTTP camera server for Seeed Xiao ESP32S3 Sense. Serves web interface for remote photo/video capture with SD card storage.

## Hardware Platform
- **Board**: Seeed Xiao ESP32S3 Sense (ESP32-S3 w/ OV2640 camera, SD card slot)
- **Key Features**: WiFi, onboard camera, microSD card support, PSRAM
- **Pin Constraints**: Camera pins are hardwired (check Xiao S3 Sense pinout), SD card uses SPI interface

## Build System
- **Framework**: PlatformIO with Arduino framework
- **Build**: `pio run` or use PlatformIO IDE tasks
- **Upload**: `pio run --target upload` (ensure correct USB port selected)
- **Monitor**: `pio device monitor` for serial output
- **Platform**: Uses custom ESP32 platform from pioarduino (see platformio.ini)

## Architecture Pattern
```
Web Client → HTTP Server → Camera Controller → OV2640 Camera
                 ↓
            SD Card Manager → microSD Card
```

### Expected Components
1. **WiFi Management**: **AP mode** - Device creates its own WiFi network (e.g., "ESP32-Camera")
2. **HTTP Server**: **ESPAsyncWebServer** - async handler for non-blocking operations
3. **Camera Interface**: ESP32 camera driver (esp_camera.h) with **OV2640 at max resolution**
4. **SD Card Handler**: File operations for saving JPEG captures
5. **Web UI**: HTML/CSS/JS served for camera control and MJPEG streaming

## Key ESP32-S3 Considerations
- **PSRAM**: Critical for camera framebuffer - must enable in platformio.ini with `-DBOARD_HAS_PSRAM`
- **Partition Scheme**: Need sufficient space for code + web assets (use `default_16MB.csv` or similar)
- **Camera Init Timing**: Initialize after WiFi but with proper power sequencing
- **Memory Management**: Watch heap fragmentation; use DMA buffers for camera

## Common ESP32 Camera Patterns
```cpp
// Camera config must match Xiao S3 Sense pins
camera_config_t config;
config.pin_d0 = 15;  // Example - verify against Xiao pinout
config.pin_vsync = 6;
config.frame_size = FRAMESIZE_QXGA;  // Max: 2048x1536 for OV2640
config.jpeg_quality = 10;  // 0-63, lower = higher quality
config.fb_count = 2;  // Use double buffering for MJPEG streaming
config.grab_mode = CAMERA_GRAB_LATEST;  // For streaming
```
Required)
- `GET /` → Web UI (HTML with video stream and controls)
- `GET /stream` → **MJPEG stream** (multipart/x-mixed-replace boundary)
- `GET /capture` → Take photo, save to SD, return JPEG
- `GET /files` → List SD card files (JSON array)
- `GET /download?file={filename}` → Download file from SD
- `DELETE /delete?file={filename}` → Delete file from SD1
```
(Required)
Add to `platformio.ini`:
```ini
lib_deps = 
    espressif/esp32-camera
    me-no-dev/ESP Async WebServer
    me-no-dev/AsyncTCP
build_flags = 
    -DBOARD_HAS_PSRAM
    -DCAMERA_MODEL_XIAO_ESP32S3les
- `GET /download/{filename}` → Download file

## Dependencies to Add
When implementing, add to `platformio.ini`:
```ini
lib_deps = 
   MJPEG Streaming Pattern
```cpp
// AsyncWebServer handler for MJPEG stream
server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request){
  AsyncWebServerResponse *response = request->beginChunkedResponse(
    "multipart/x-mixed-replace; boundary=frame",
    [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      camera_fb_t *fb = esp_camera_fb_get();
      // Write JPEG frame with boundary
      esp_camera_fb_return(fb);
    }
  );
  request->send(response);
});
```

## File Organization
- Camera pins config: `include/camera_pins.h` (Xiao S3 Sense specific)
- AP settings: hardcode in `src/main.cpp` (SSID: "ESP32-Camera")
- Web UI: embed HTML as string literal or use PROGMEM for large files

## Critical Gotchas
- **MJPEG streaming blocks if client disconnects** - use `fb_count = 2` and `CAMERA_GRAB_LATEST`
- **OV2640 max is QXGA (2048x1536)** - higher settings will fail init
- **SD card CS pin** - Usually GPIO21 on Xiao S3 Sense, verify schematic
- **AP mode default IP** - 192.168.4.1, document this in UI SPI speed (start at 4MHz)
4. **WiFi Connection**: Print IP address once connected
5. **Memory**: Monitor free heap with `ESP.getFreeHeap()`

## File Organization
- Keep camera config in separate header: `include/camera_config.h`
- WiFi credentials: use `include/wifi_credentials.h` (git-ignore this)
- Web assets: consider SPIFFS or embedded HTML strings for small UIs

## Testing Without Hardware
- Serial output verification for logic flow
- Mock camera frames for server testing
- Validate HTTP responses and headers
