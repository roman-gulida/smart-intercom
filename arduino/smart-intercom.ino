/*//////////////////////////////////////////////////////////////
                            IMPORTS
//////////////////////////////////////////////////////////////*/
#include "esp_camera.h" 
#include <WiFi.h> 
#include <WebServer.h> 
#include "img_converters.h" 
#include "soc/soc.h"  //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h" 

// Camera configuration
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

/*//////////////////////////////////////////////////////////////
                        STATE VARIABLES
//////////////////////////////////////////////////////////////*/
// Wi-Fi credentials
const char* ssid = "******";
const char* password = "******";

// Stream configuration
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
httpd_handle_t stream_httpd = NULL;

// Webservers for notifications and unlocking
WebServer notificationServer(81); 
WebServer unlockServer(82);

// GPIO pins
const int switchPin = 14; // Toggle switch
const int buzzerPin = 13; // Buzzer
const int ledPin = 2; // Unlock LED

/*//////////////////////////////////////////////////////////////
                           FUNCTIONS
//////////////////////////////////////////////////////////////*/

/* CAMERA */
void setupCameraConfig(camera_config_t &config) {
  // Initialize the camera config
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

  // Camera quality settings
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA; 
    config.jpeg_quality = 10;  
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
}  

void optimizeCameraSettings() {
  sensor_t * s = esp_camera_sensor_get();
  s->set_saturation(s, 0);      // -2 to 2
  s->set_brightness(s, 0);      // -2 to 2
  s->set_contrast(s, 0);        // -2 to 2
  s->set_sharpness(s, 2);       // -2 to 2
  s->set_special_effect(s, 0);  // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_wb_mode(s, 1);         // 0 = disable , 1 = enable
  s->set_whitebal(s, 1);        // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);        // 0 = disable , 1 = enable
  s->set_exposure_ctrl(s, 1);   // 0 = disable , 1 = enable
  s->set_aec2(s, 0);           // 0 = disable , 1 = enable
  s->set_gain_ctrl(s, 1);       // 0 = disable , 1 = enable
  s->set_bpc(s, 1);            // 0 = disable , 1 = enable
  s->set_wpc(s, 1);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
  s->set_lenc(s, 1);           // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  s->set_dcw(s, 1);            // 0 = disable , 1 = enable
  s->set_colorbar(s, 0);       // 0 = disable , 1 = enable
}

static esp_err_t stream_handler(httpd_req_t *req) {
  // Initialize variables
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  // Set the HTTP response content type 
  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK) {
    return res;
  }

  while(true) {
    // Capture a frame from the camera
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      // Convert frame to JPEG if not already in JPEG format
      if(fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if(!jpeg_converted) {
          Serial.println("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        // Use frame buffer directly if already in JPEG
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }

    // Send frame header
    if(res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }

    // Send JPEG image data
    if(res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    // Send stream boundary
    if(res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }

    // Clean up frame buffer and JPEG buffer
    if(fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }

    // Break if any error occurs
    if(res != ESP_OK) {
      break;
    }
  }
  return res;
}

void setupCameraServer() {
  // Create HTTP server configuration
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;

  // Define URI route for the root endpoint
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  // Start the HTTP server
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &index_uri);
  }
}

/* NOTIFICATIONS */
void setupNotificationServer() {
  notificationServer.on("/notification", HTTP_GET, []() {
    notificationServer.send(200, "application/json", "{\"event\":\"switch_pressed\"}");
    Serial.println("Senidng notification...");
  });

  notificationServer.begin();
}

/* UNLOCKING */
void setupUnlockServer() {
  unlockServer.on("/unlock", HTTP_GET, []() {
    Serial.println("Unlock command received");
    digitalWrite(ledPin, HIGH); 
    delay(3000); // 3 sec 
    digitalWrite(ledPin, LOW);
    unlockServer.send(200, "text/plain", "Door unlocked");
  });

  unlockServer.begin();
}

/* SETUP & LOOP */
void setup() {
  // Initialize serial communication with computer
  Serial.begin(115200);

  // Initialize camera
  camera_config_t config;
  setupCameraConfig(config);

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  optimizeCameraSettings();

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(5000); // 5 sec
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());

  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 

  // Setup servers
  setupCameraServer();
  setupNotificationServer();
  setupUnlockServer();

  // Set up GPIO pins
  pinMode(switchPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin, OUTPUT);
}

void loop() {
  if (digitalRead(switchPin) == LOW) { 
    Serial.println("Toggle switch activated");
    digitalWrite(buzzerPin, HIGH);
    notificationServer.handleClient();
    delay(3000); // 3 sec
    digitalWrite(buzzerPin, LOW);
  }  

  unlockServer.handleClient();

  delay(1);
}