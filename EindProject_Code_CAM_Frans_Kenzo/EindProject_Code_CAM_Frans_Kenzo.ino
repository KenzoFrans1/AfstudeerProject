#include "esp_camera.h"
#include <WiFi.h>
#include "Arduino.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

// ===== CONFIGURATION SECTION =====
// WiFi credentials
// const char* ssid = "telenet-1683985";
// const char* password = "Ffncutzz8z5X";

const char* ssid = "IoT2";
const char* password = "KdGIoT69!";

// WEB AUTHENTICATION CREDENTIALS
const char* web_username = "admin";
const char* web_password = "admin"; // CHANGE THIS!

// Telegram settings
const char* BOTtoken = "7574163261:AAF1WG7Cy4XTxb28mc-4X3QUOI4a2GMiRSo";
const char* CHAT_ID = "6741274207";

// Camera pins (AI Thinker ESP32-CAM module)
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

// Flash LED Pin
#define FLASH_LED_PIN 4
bool flashState = LOW;

// ===== GLOBAL VARIABLES =====
WiFiClientSecure clientTCP;
UniversalTelegramBot bot(BOTtoken, clientTCP);
httpd_handle_t stream_httpd = NULL;
httpd_handle_t web_httpd = NULL;

// ===== AUTHENTICATION FUNCTIONS =====
bool checkAuth(httpd_req_t *req) {
  char auth_header[256];
  size_t auth_len = httpd_req_get_hdr_value_len(req, "Authorization");
  
  if (auth_len == 0) {
    return false; // No authorization header
  }
  
  if (auth_len >= sizeof(auth_header)) {
    return false; // Header too long
  }
  
  if (httpd_req_get_hdr_value_str(req, "Authorization", auth_header, sizeof(auth_header)) != ESP_OK) {
    return false; // Failed to get header
  }
  
  // Check if it starts with "Basic "
  if (strncmp(auth_header, "Basic ", 6) != 0) {
    return false;
  }
  
  // Decode Base64
  const char* encoded = auth_header + 6; // Skip "Basic "
  size_t encoded_len = strlen(encoded);
  size_t decoded_len;
  
  // Calculate required buffer size
  if (mbedtls_base64_decode(NULL, 0, &decoded_len, (const unsigned char*)encoded, encoded_len) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    return false;
  }
  
  char decoded[256];
  if (decoded_len >= sizeof(decoded)) {
    return false;
  }
  
  if (mbedtls_base64_decode((unsigned char*)decoded, sizeof(decoded), &decoded_len, (const unsigned char*)encoded, encoded_len) != 0) {
    return false;
  }
  
  decoded[decoded_len] = '\0';
  
  // Expected format: "username:password"
  char expected[256];
  snprintf(expected, sizeof(expected), "%s:%s", web_username, web_password);
  
  return strcmp(decoded, expected) == 0;
}

esp_err_t sendAuthRequired(httpd_req_t *req) {
  httpd_resp_set_status(req, "401 Unauthorized");
  httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32-CAM Security\"");
  httpd_resp_set_type(req, "text/plain");
  const char* resp = "401 Unauthorized - Authentication Required";
  return httpd_resp_send(req, resp, strlen(resp));
}

// HTML for the web interface - simplified and improved
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-CAM BEVEILIGING</title>
  <style>
    body {
      text-align: center;
      font-family: Arial, sans-serif;
      margin: 0;
      padding: 15px;
      background-color: #f5f5f5;
      color: #333;
    }
    h1 {
      color: #2c3e50;
      margin-bottom: 20px;
    }
    .container {
      max-width: 400px;
      margin: 0 auto;
      background: white;
      border-radius: 10px;
      padding: 20px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }
    .stream-container {
      position: relative;
      min-height: 120px;
      margin-bottom: 20px;
      background-color: #eee;
      border-radius: 8px;
      overflow: hidden;
    }
    #stream {
      width: 100%;
      border-radius: 8px;
      display: block;
    }
    #loading {
      position: absolute;
      top: 50%;
      left: 50%;
      transform: translate(-50%, -50%);
      display: none;
      color: #666;
    }
    .buttons {
      display: flex;
      flex-wrap: wrap;
      justify-content: center;
      gap: 10px;
      margin: 15px 0;
    }
    button {
      padding: 12px 20px;
      background-color: #3498db;
      color: white;
      border: none;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      transition: background-color 0.3s;
      min-width: 150px;
    }
    button:hover {
      background-color: #2980b9;
    }
    #captureBtn { background-color: #27ae60; }
    #captureBtn:hover { background-color: #219955; }
    #flashBtn { background-color: #f39c12; }
    #flashBtn:hover { background-color: #e67e22; }
    #status {
      padding: 10px;
      border-radius: 5px;
      background-color: #ecf0f1;
      margin: 15px auto;
      font-size: 16px;
      max-width: 400px;
      transition: all 0.3s;
    }
    .success { background-color: #d5f5e3 !important; color: #27ae60; }
    .error { background-color: #fadbd8 !important; color: #e74c3c; }
    .info { background-color: #d6eaf8 !important; color: #2980b9; }
    .footer {
      margin-top: 30px;
      font-size: 14px;
      color: #7f8c8d;
    }
    .security-indicator {
      background-color: #d5f5e3;
      color: #27ae60;
      padding: 8px 16px;
      border-radius: 20px;
      font-size: 12px;
      margin-bottom: 15px;
      display: inline-block;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="security-indicator">SECURED CONNECTION</div>
    <h1>ESP32-CAM BEVEILIGING</h1>
    <div class="stream-container">
      <img src="" id="stream">
      <div id="loading">Inladen stream...</div>
    </div>
    <div class="buttons">
      <button id="captureBtn" onclick="capturePhoto()">CAPTURE & VERSTUUR</button>
      <button id="flashBtn" onclick="toggleFlash()">FLASH</button>
      <button onclick="refreshStream()">VERNIEUW STREAM</button>
    </div>
    <div id="status">System klaar</div>
    <div class="footer">
      <p>ESP32-CAM | IP: <span id="ipAddress"></span></p>
    </div>
  </div>

  <script>
    window.onload = function() {
      // Display IP address
      document.getElementById('ipAddress').textContent = window.location.hostname;
      
      // Load stream
      loadStream();
      
      // Set status to system ready
      showStatus('System ready', 'info');
    }
    
    function loadStream() {
      showStatus('Connecting to stream...', 'info');
      document.getElementById('loading').style.display = 'block';
      const streamUrl = 'http://' + window.location.hostname + ':81/stream';
      const img = document.getElementById('stream');
      
      img.onload = function() {
        document.getElementById('loading').style.display = 'none';
        showStatus('Stream connected', 'success');
      };
      
      img.onerror = function() {
        document.getElementById('loading').style.display = 'none';
        showStatus('Stream error. Please try refreshing.', 'error');
      };
      
      img.src = streamUrl;
    }
    
    function refreshStream() {
      document.getElementById('stream').src = '';
      setTimeout(loadStream, 500);
    }
    
    function capturePhoto() {
      showStatus('Capturing and sending to Telegram...', 'info');
      document.getElementById('captureBtn').disabled = true;
      
      fetch('/capture')
        .then(response => response.text())
        .then(data => {
          document.getElementById('captureBtn').disabled = false;
          showStatus('Photo sent to Telegram!', 'success');
        })
        .catch(error => {
          document.getElementById('captureBtn').disabled = false;
          showStatus('Error sending photo', 'error');
        });
    }
    
    function toggleFlash() {
      document.getElementById('flashBtn').disabled = true;
      
      fetch('/flash')
        .then(response => response.text())
        .then(data => {
          document.getElementById('flashBtn').disabled = false;
          showStatus('Flash toggled', 'success');
        })
        .catch(error => {
          document.getElementById('flashBtn').disabled = false;
          showStatus('Error toggling flash', 'error');
        });
    }
    
    function showStatus(message, type) {
      const status = document.getElementById('status');
      status.textContent = message;
      status.className = '';
      status.classList.add(type);
      
      // Auto-clear success and error messages after 3 seconds
      if (type === 'success' || type === 'error') {
        setTimeout(() => {
          status.textContent = 'System ready';
          status.className = 'info';
        }, 3000);
      }
    }
  </script>
</body>
</html>
)rawliteral";

// ===== CAMERA FUNCTIONS =====
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
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  
  // Set optimal resolution based on available memory
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 12;  // 0-63 (lower is better quality)
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }
  
  // Optimize camera settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);     // Increase brightness slightly
    s->set_contrast(s, 1);       // Increase contrast slightly
    s->set_saturation(s, 0);     // Normal saturation
    s->set_special_effect(s, 0); // No special effect
    s->set_whitebal(s, 1);       // Enable white balance
    s->set_awb_gain(s, 1);       // Enable AWB gain
    s->set_wb_mode(s, 0);        // Auto white balance
    s->set_exposure_ctrl(s, 1);  // Enable auto exposure
    s->set_aec2(s, 0);           // Disable AEC DSP
    s->set_gain_ctrl(s, 1);      // Enable auto gain
    s->set_vflip(s, 0);          // No vertical flip
    s->set_hmirror(s, 0);        // No horizontal mirror
  }

  Serial.println("Camera initialized successfully");
  return true;
}

// ===== TELEGRAM FUNCTIONS =====
String sendPhotoTelegram() {
  const char* myDomain = "api.telegram.org";
  String getAll = "";
  String getBody = "";

  // Prepare camera and flash
  Serial.println("Taking photo...");
  bool originalFlashState = flashState;
  digitalWrite(FLASH_LED_PIN, HIGH);  // Turn on flash for photo
  delay(200);  // Give camera time to adjust

  // Take photo
  camera_fb_t * fb = NULL;
  
  // Dispose first picture because of bad quality
  fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
    delay(200);  // Short delay before taking actual photo
  }
  
  // Take the actual photo
  fb = esp_camera_fb_get();
  if (!fb) {
    digitalWrite(FLASH_LED_PIN, originalFlashState);  // Restore flash state
    Serial.println("Camera capture failed");
    return "Camera capture failed";
  }
  
  // Restore flash state
  digitalWrite(FLASH_LED_PIN, originalFlashState);
  
  // Send to Telegram
  Serial.println("Connecting to Telegram...");
  
  if (clientTCP.connect(myDomain, 443)) {
    Serial.println("Connected to Telegram server");
    
    String head = "--ESP32CAM\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + String(CHAT_ID) + 
                  "\r\n--ESP32CAM\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--ESP32CAM--\r\n";

    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;
  
    clientTCP.println("POST /bot" + String(BOTtoken) + "/sendPhoto HTTP/1.1");
    clientTCP.println("Host: " + String(myDomain));
    clientTCP.println("Content-Length: " + String(totalLen));
    clientTCP.println("Content-Type: multipart/form-data; boundary=ESP32CAM");
    clientTCP.println();
    clientTCP.print(head);
  
    // Send the image data in chunks to avoid memory issues
    uint8_t *fbBuf = fb->buf;
    const size_t bufSize = 1024;
    for (size_t n = 0; n < imageLen; n += bufSize) {
      size_t len = (n + bufSize < imageLen) ? bufSize : (imageLen - n);
      clientTCP.write(fbBuf + n, len);
    }
    
    clientTCP.print(tail);
    esp_camera_fb_return(fb);
    
    // Wait for response with timeout
    const unsigned long timeout = 10000;  // 10 second timeout
    unsigned long startTime = millis();
    bool state = false;
    
    while ((millis() - startTime) < timeout) {
      delay(100);
      
      while (clientTCP.available()) {
        char c = clientTCP.read();
        if (state) getBody += String(c);
        if (c == '\n') {
          if (getAll.length() == 0) state = true;
          getAll = "";
        } 
        else if (c != '\r') {
          getAll += String(c);
        }
        startTime = millis();  // Reset timeout while receiving data
      }
      
      if (getBody.length() > 0) break;  // Got a response
    }
    
    clientTCP.stop();
    Serial.println("Photo sent to Telegram");
    return "Photo sent successfully";
  }
  else {
    esp_camera_fb_return(fb);
    Serial.println("Connection to Telegram failed");
    return "Failed to connect to Telegram";
  }
}

// ===== WEB SERVER HANDLERS =====
static esp_err_t index_handler(httpd_req_t *req) {
  // Check authentication first
  if (!checkAuth(req)) {
    return sendAuthRequired(req);
  }
  
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "identity");
  return httpd_resp_send(req, (const char *)index_html, strlen(index_html));
}

static esp_err_t stream_handler(httpd_req_t *req) {
  // NOTE: Stream is not authenticated to allow embedding
  // If you want to secure the stream too, uncomment these lines:
  /*
  if (!checkAuth(req)) {
    return sendAuthRequired(req);
  }
  */
  
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  static int64_t last_frame = 0;
  
  // Set content type and CORS headers
  res = httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=ESP32CAM-stream");
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  
  // Stream boundary definition
  const char* _STREAM_BOUNDARY = "\r\n--ESP32CAM-stream\r\n";
  const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

  while (true) {
    // Add small delay to reduce CPU usage
    int64_t fr_start = esp_timer_get_time();
    
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
      
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    
    // Clean up
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    
    if (res != ESP_OK) {
      break;
    }
    
    // Limit framerate to reduce CPU load (aim for ~15-20 FPS)
    int64_t fr_end = esp_timer_get_time();
    int64_t frame_time = fr_end - fr_start;
    last_frame = fr_end;
    
    // If frame was processed faster than 50ms, add delay
    if (frame_time < 50000) {
      delayMicroseconds(50000 - frame_time);
    }
  }
  
  return res;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  // Check authentication first
  if (!checkAuth(req)) {
    return sendAuthRequired(req);
  }
  
  String result = sendPhotoTelegram();
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, result.c_str(), result.length());
}

static esp_err_t flash_handler(httpd_req_t *req) {
  // Check authentication first
  if (!checkAuth(req)) {
    return sendAuthRequired(req);
  }
  
  flashState = !flashState;
  digitalWrite(FLASH_LED_PIN, flashState);
  
  String response = flashState ? "Flash ON" : "Flash OFF";
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, response.c_str(), response.length());
}

// ===== SERVER INITIALIZATION =====
void startCameraServer() {
  // Configure and start stream server (port 81)
  httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
  stream_config.server_port = 81;
  stream_config.ctrl_port = 32123;
  stream_config.core_id = 1;  // Run on second core if available
  stream_config.stack_size = 8192;  // Increased stack size

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&stream_httpd, &stream_config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("Stream server started on port: %d\n", stream_config.server_port);
  }

  // Configure and start web server (port 80)
  httpd_config_t web_config = HTTPD_DEFAULT_CONFIG();
  web_config.core_id = 0;  // Run on first core

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t flash_uri = {
    .uri       = "/flash",
    .method    = HTTP_GET,
    .handler   = flash_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&web_httpd, &web_config) == ESP_OK) {
    httpd_register_uri_handler(web_httpd, &index_uri);
    httpd_register_uri_handler(web_httpd, &capture_uri);
    httpd_register_uri_handler(web_httpd, &flash_uri);
    Serial.printf("Web server started on port: %d\n", web_config.server_port);
  }
}

// ===== WIFI CONNECTION =====
bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  const int maxAttempts = 20;
  
  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    Serial.println("\nWiFi connection failed");
    return false;
  }
}

// ===== MAIN FUNCTIONS =====
void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Initialize serial
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n====== ESP32-CAM Surveillance System (SECURED) ======");
  
  // Initialize LED Flash
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, flashState);
  
  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera initialization failed! Restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
    return;
  }
  
  // Connect to WiFi
  if (!connectWiFi()) {
    Serial.println("WiFi connection failed! Restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
    return;
  }
  
  // Setup secure client for Telegram
  clientTCP.setInsecure();
  
  // Start web and stream servers
  startCameraServer();
  
  Serial.println("System ready - Web interface secured with authentication");
  Serial.printf("Protected endpoints: / /capture /flash\n");
  Serial.printf("Credentials: %s / [password hidden]\n", web_username);
}

void loop() {
  // Check WiFi connection
  static unsigned long lastWifiCheck = 0;
  if (millis() - lastWifiCheck > 30000) {  // Check every 30 seconds
    lastWifiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.reconnect();
      
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 10) {
        delay(500);
        Serial.print(".");
        attempts++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi reconnected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
      }
    }
  }
  
  // Allow background tasks to run
  delay(100);
}