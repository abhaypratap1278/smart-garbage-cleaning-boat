/*
  Smart Garbage Cleaning Boat - Live Camera (ESP32-CAM, AI-Thinker)

  Upload hone ke baad Serial Monitor (115200) me IP address aayega.
  Chrome me kholo:
    http://<IP>          -> video page
    http://<IP>:81/stream -> direct video stream

  Arduino IDE settings:
    Board: "AI Thinker ESP32-CAM"
    Upload ke time GPIO0 ko GND se jodna padta hai (ya ESP32-CAM-MB programmer use karo),
    upload ke baad hata do aur reset dabao.

  ================= WIRING =================
  Normal use (no wire to the main ESP32, video goes over WiFi):
    ESP32-CAM 5V  -> stable 5V supply (at least 1A)
    ESP32-CAM GND -> supply GND

  Uploading code (FTDI adapter):
    ESP32-CAM 5V   -> FTDI 5V
    ESP32-CAM GND  -> FTDI GND
    ESP32-CAM U0R  -> FTDI TX
    ESP32-CAM U0T  -> FTDI RX
    ESP32-CAM GPIO0 -> GND (only while uploading, then remove and press RESET)
  ==========================================
  Full details with tables: see README.md
*/

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ---------- WiFi settings (apne badal lo) ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---------- AI-Thinker ESP32-CAM pins ----------
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

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t index_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// Video page (port 80)
static esp_err_t index_handler(httpd_req_t *req) {
  const char* html =
    "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Boat Camera</title></head>"
    "<body style='margin:0;background:#101820;color:#fff;text-align:center;font-family:Arial'>"
    "<h3>Smart Garbage Cleaning Boat - Live Camera</h3>"
    "<img id='v' style='width:100%;max-width:640px;border-radius:8px'>"
    "<script>document.getElementById('v').src='http://'+location.hostname+':81/stream';</script>"
    "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

// MJPEG stream (port 81)
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char part_buf[64];
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, part_buf, hlen);
      if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      esp_camera_fb_return(fb);
      fb = NULL;
    }
    if (res != ESP_OK) break;  // viewer ne page band kar diya
  }
  return res;
}

void startServers() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
  config.server_port = 80;
  if (httpd_start(&index_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(index_httpd, &index_uri);
  }

  httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
  config.server_port = 81;
  config.ctrl_port += 1;
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  Serial.begin(115200);

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
  config.frame_size = FRAMESIZE_QVGA;   // 320x240, smooth video ke liye. Zyada clarity chahiye to FRAMESIZE_VGA
  config.jpeg_quality = 12;             // chhota number = better quality
  config.fb_count = 1;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  startServers();
  Serial.print("Camera ready! Open in Chrome: http://");
  Serial.println(WiFi.localIP());
}

void loop() {
  delay(10000);  // sab kaam server tasks me ho raha hai
}
