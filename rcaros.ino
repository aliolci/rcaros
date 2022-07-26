/*
  ESP32 Web Server - STA Mode
  modified on 25 MAy 2019
  by Mohammadreza Akbari @ Electropeak
  Home
*/

#include <WiFi.h>
#include <WebServer.h>
#include <SD.h>
#include <ESP32Servo.h>
#include "esp_camera.h"
#include "soc/soc.h" //disable brownout problems
#include "soc/rtc_cntl_reg.h"  //disable brownout problems
#include "esp_http_server.h"

#define PART_BOUNDARY "123456789000000000000987654321"

// SSID & Password
const char* ssid = "ollytest";  // Enter your SSID here
const char* password = "123qweasd12";  //Enter your Password here

// PIN
const int LED_DEBUG = 21;
const int SERVO_PIN = 15;   // 75 135  175
const int MOTOR_SLEEP = 19;
const int MOTOR_AIN1 = 2; // 1
const int MOTOR_AIN2 = 22; // 3

const IPAddress local_IP(192,168,4,22);
const IPAddress gateway(192,168,4,9);
const IPAddress subnet(255,255,255,0);

static const char* _TEXT_HTML= "text/html";
static const char* _TEXT_PLAIN = "text/plain";
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";
static const char* _CORS = "Access-Control-Allow-Origin: *";

Servo steeringServo;
httpd_handle_t server_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

void setup() {
  
  Serial.begin(115200);
  Serial.println("Try Connecting to ");
  Serial.println(ssid);

  pinMode(LED_DEBUG, OUTPUT);
  pinMode(MOTOR_AIN1,OUTPUT);
  pinMode(MOTOR_AIN2,OUTPUT);
  pinMode(MOTOR_SLEEP,OUTPUT);
  digitalWrite(MOTOR_SLEEP, HIGH);
  steeringServo.setPeriodHertz(50);    // standard 50 hz servo
  steeringServo.attach(SERVO_PIN, 500, 2400); //

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = 34; //Y2_GPIO_NUM;
  config.pin_d1 = 13; //Y3_GPIO_NUM;
  config.pin_d2 = 14; //Y4_GPIO_NUM;
  config.pin_d3 = 35; //Y5_GPIO_NUM;
  config.pin_d4 = 39; //Y6_GPIO_NUM;
  config.pin_d5 = 38; //Y7_GPIO_NUM;
  config.pin_d6 = 37; //Y8_GPIO_NUM;
  config.pin_d7 = 36; //Y9_GPIO_NUM;
  config.pin_xclk = 4; //XCLK_GPIO_NUM;
  config.pin_pclk = 25; //PCLK_GPIO_NUM;
  config.pin_vsync = 5; //VSYNC_GPIO_NUM;
  config.pin_href = 27; //HREF_GPIO_NUM;
  config.pin_sscb_sda = 18; //SIOD_GPIO_NUM;
  config.pin_sscb_scl = 23; //SIOC_GPIO_NUM;
  config.pin_pwdn = -1; //PWDN_GPIO_NUM;
  config.pin_reset = -1; //RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_UXGA;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if(psramFound()){
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }

  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
  }

  // Connect to your wi-fi modem
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  httpd_uri_t debug_uri = {
    .uri       = "/debug",
    .method    = HTTP_GET,
    .handler   = debug_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t ping_uri = {
    .uri       = "/ping",
    .method    = HTTP_GET,
    .handler   = ping_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t speed_uri = {
    .uri       = "/speed",
    .method    = HTTP_POST,
    .handler   = speed_handler,
    .user_ctx  = NULL
  };

    httpd_uri_t steer_uri = {
    .uri       = "/steer",
    .method    = HTTP_POST,
    .handler   = steer_handler,
    .user_ctx  = NULL
  };

  httpd_config_t httpServerConfig = HTTPD_DEFAULT_CONFIG();
  httpServerConfig.server_port = 80;

  if (httpd_start(&server_httpd, &httpServerConfig) == ESP_OK) {
    httpd_register_uri_handler(server_httpd, &debug_uri);
    httpd_register_uri_handler(server_httpd, &ping_uri);
    httpd_register_uri_handler(server_httpd, &speed_uri);
    httpd_register_uri_handler(server_httpd, &steer_uri);    
    Serial.println("Server started!");
  } else {
    Serial.println("Failed to start server");
  }

  httpd_uri_t camera_stream_uri = {
    .uri       = "/camera",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };

  httpServerConfig.server_port += 1;
  httpServerConfig.ctrl_port += 1;
  if (httpd_start(&camera_httpd, &httpServerConfig) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &camera_stream_uri);
    Serial.println("Camera Server started!");
  } else {
    Serial.println("Failed to camera server");
  }
  
}

void loop() {

}



// Handle debug url (/)
static esp_err_t debug_handler(httpd_req_t *req) {
  File file = SD.open("debug.html", FILE_READ);
  String debug = file.readString();
  esp_err_t res = ESP_OK;
  res = httpd_resp_set_type(req, _TEXT_HTML);
  char buf[debug.length()];
  debug.toCharArray(buf, debug.length());
  httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
  return res;
}

static esp_err_t ping_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  res = httpd_resp_set_type(req, _TEXT_PLAIN);
  httpd_resp_send(req, "pong", HTTPD_RESP_USE_STRLEN);
  return res;
}

// Set speed
static esp_err_t speed_handler(httpd_req_t *req) {
  char content[req->content_len];
  httpd_req_recv(req, content, req->content_len);
  int speed = atoi(content);
  if(speed > 255 || speed < -255){
    return ESP_OK;
  }
    
  if (speed > 0) {
    analogWrite(MOTOR_AIN1, speed);
    digitalWrite(MOTOR_AIN2, LOW);
  } else {
    digitalWrite(MOTOR_AIN1, LOW);
    analogWrite(MOTOR_AIN2, -speed);
  }

  esp_err_t res = ESP_OK;
  res = httpd_resp_set_type(req, _TEXT_PLAIN);
  httpd_resp_send(req, content, HTTPD_RESP_USE_STRLEN);
  return res;
}

// Set steer
static esp_err_t steer_handler(httpd_req_t *req) {
  char content[req->content_len];
  httpd_req_recv(req, content, req->content_len);
  int steeringAngle = atoi(content);
  if(steeringAngle > 180 || steeringAngle < 0){
    return ESP_OK;
  }
  steeringServo.write(steeringAngle);
  esp_err_t res = ESP_OK;
  res = httpd_resp_set_type(req, _TEXT_PLAIN);
  httpd_resp_send(req, content, HTTPD_RESP_USE_STRLEN);
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req){
  Serial.println("Camera handler invoked");
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[128];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");
  if(res != ESP_OK){
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      if(fb->width > 400){
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if(res != ESP_OK){
      break;
    }
  }
  return res;
}
