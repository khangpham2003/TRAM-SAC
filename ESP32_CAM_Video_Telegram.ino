#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
WiFiManager wm;
#include "esp_camera.h"
#include "UniversalTelegramBot.h"  
#include <ArduinoJson.h>

static const char vernum[] = "fire-cam 9.6";
String devstr =  "FIRE-CAM";

framesize_t configframesize = FRAMESIZE_VGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
int framesize = FRAMESIZE_VGA; //FRAMESIZE_HD;
int quality = 10;
int qualityconfig = 5;
char def_BOTtoken[50];
char def_timezone[50];
char def_chat_id[50];
char def_hdcam[4];

char ssid[] = "";     // your network SSID (name)
char password[] = ""; // your network key
// https://sites.google.com/a/usapiens.com/opnode/time-zones  -- find your timezone here
String timezone = "MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00" ; //"GMT0BST,M3.5.0/01,M10.5.0/02";
String chat_id;
String BOTtoken;

int MagicNumber = 12;
bool reboot_request = false;
bool reset_request = false;

#define CAMERA_MODEL_AI_THINKER
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

#include "esp_system.h"

bool setupCamera() {
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
  config.xclk_freq_hz = 10000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = configframesize;
    config.jpeg_quality = qualityconfig;
    config.fb_count = 4;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  //Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  //Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  static char * memtmp = (char *) malloc(32 * 1024);
  static char * memtmp2 = (char *) malloc(32 * 1024); //32767

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return false;
  }
  free(memtmp2);
  memtmp2 = NULL;
  free(memtmp);
  memtmp = NULL;
  //Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  //Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  sensor_t * s = esp_camera_sensor_get();

  //  drop down frame size for higher initial frame rate
  s->set_framesize(s, (framesize_t)framesize);
  s->set_quality(s, quality);
  delay(200);
  return true;
}

#define FLASH_LED_PIN 4

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

int Bot_mtbs = 5000; //mean time between scan messages
long Bot_lasttime;   //last time messages' scan has been done
long TimePhoto_lasttime;   // last time we sent a timed photo
int TimePhoto_Minutes ;     // 2 minutes between photos

bool flashState = LOW;

camera_fb_t * fb = NULL;

bool tim_enabled = false;
bool hdcam = true;

bool isMoreDataAvailable();

////////////////////////////////  send photo as 512 byte blocks or jzblocksize
int currentByte;
uint8_t* fb_buffer;
size_t fb_length;

bool isMoreDataAvailable() {
  return (fb_length - currentByte);
}

uint8_t getNextByte() {
  currentByte++;
  return (fb_buffer[currentByte - 1]);
}

bool dataAvailable = false;

///////////////////////////////

void handleNewMessages(int numNewMessages) {
  //Serial.println("handleNewMessages");
  //Serial.println(String(numNewMessages));

  for (int i = 0; i < numNewMessages; i++) {

    // This line gets id of the person who sent the message .. it could be any telegram user
    // remove this line, and all messages get send to you

    // chat_id = String(bot.messages[i].chat_id);

    String text = bot.messages[i].text;
    Serial.printf("\nGot a message %s\n", text);

    String from_name = bot.messages[i].from_name;
    if (from_name == "") from_name = "Guest";

    String hi = "Got: ";
    hi += text;
    bot.sendMessage(chat_id, hi, "Markdown");
    client.setHandshakeTimeout(120000);
    if (text == "/flash") {
      flashState = !flashState;
      digitalWrite(FLASH_LED_PIN, flashState);
    }

    if (text.substring(1).toInt() >= 1 && text.substring(1).toInt() <= 1440) {
      TimePhoto_Minutes = text.substring(1).toInt();
      TimePhoto_lasttime = millis();
      do_eprom_write();
    }
    if (text == "/status") {
      String stat = "Device: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " +  WiFi.localIP().toString() + "\nTim Enabled: " + tim_enabled;
      stat = stat + "\nTimer: " + TimePhoto_Minutes;

      bot.sendMessage(chat_id, stat, "");
    }

    if (text == "/reboot") {
      reboot_request = true;
    }

    if (text == "/reset") {
      reset_request = true;
    }

    if (text == "/entim") {
      tim_enabled = true;
      TimePhoto_lasttime = millis();
      do_eprom_write();
    }

    if (text == "/distim") {
      tim_enabled = false;
      do_eprom_write();
    }

    for (int j = 0; j < 4; j++) {
      camera_fb_t * newfb = esp_camera_fb_get();
      if (!newfb) {
        Serial.println("Camera Capture Failed");
      } else {
        //Serial.print("Pic, len="); Serial.print(newfb->len);
        //Serial.printf(", new fb %X\n", (long)newfb->buf);
        esp_camera_fb_return(newfb);
        delay(10);
      }
    }
    if ( text == "/photo" || text == "/caption" ) {

      fb = NULL;

      // Take Picture with Camera
      fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        bot.sendMessage(chat_id, "Camera capture failed", "");
        return;
      }

      currentByte = 0;
      fb_length = fb->len;
      fb_buffer = fb->buf;

      if (text == "/caption") {

        Serial.println("\n>>>>> Sending with a caption, bytes=  " + String(fb_length));

        String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                      "image/jpeg", "Your photo", chat_id, fb_length,
                      isMoreDataAvailable, getNextByte, nullptr, nullptr);

        Serial.println("done!");

      } else {

        Serial.println("\n>>>>> Sending, bytes=  " + String(fb_length));

        bot.sendPhotoByBinary(chat_id, "image/jpeg", fb_length,
                              isMoreDataAvailable, getNextByte,
                              nullptr, nullptr);

        dataAvailable = true;

        Serial.println("done!");
      }
      esp_camera_fb_return(fb);
    }

    if (text == "/vga" ) {

      fb = NULL;

      //sensor_t * s = esp_camera_sensor_get();
      //s->set_framesize(s, FRAMESIZE_VGA);

      Serial.println("\n\n\nSending VGA");

      // Take Picture with Camera
      fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        bot.sendMessage(chat_id, "Camera capture failed", "");
        return;
      }

      currentByte = 0;
      fb_length = fb->len;
      fb_buffer = fb->buf;

      Serial.println("\n>>>>> Sending as 512 byte blocks, with jzdelay of 0, bytes=  " + String(fb_length));

      bot.sendPhotoByBinary(chat_id, "image/jpeg", fb_length,
                            isMoreDataAvailable, getNextByte,
                            nullptr, nullptr);

      esp_camera_fb_return(fb);
    }

    if (text == "/start") {
      String welcome = "ESP32Cam Telegram BOT\n\n";
      welcome += "/photo: take a photo\n";
      welcome += "/flash: toggle flash LED\n";
      welcome += "/caption: photo with caption\n";
      welcome += "\n/entim: enable timed photo\n";
      welcome += "/distim: disable timed photo\n";
      welcome += "/10: 1..10..1440 minutes timed photos\n";
      welcome += "\n/status: status\n";
      welcome += "/start: start\n\n";
      welcome += "/reset: reset wifi params\n";
      welcome += "/reboot: reboot\n";
      //welcome += "\n https://ko-fi.com/jameszah/\n";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }
  }
}

void saveParamCallback() {
  if (wm.server->hasArg("DevName")) {
    String sDevName  = wm.server->arg("DevName");
    devstr = sDevName;
    Serial.println(sDevName);
  }

  if (wm.server->hasArg("chat_id")) {
    String schat_id  = wm.server->arg("chat_id");
    Serial.println(schat_id);
    chat_id = schat_id;
  }
  if (wm.server->hasArg("BOTtoken")) {
    String sBOTtoken  = wm.server->arg("BOTtoken");
    Serial.println(sBOTtoken);
    BOTtoken = sBOTtoken;
    bot.updateToken(BOTtoken);

  }
  if (wm.server->hasArg("phdcam")) {
    String shdcam  = wm.server->arg("phdcam");
    Serial.println(shdcam);
    if (shdcam == "yes") {
      hdcam = true;
      Serial.println("hdcam is yes");
    } else {
      hdcam = false;
      Serial.println("hdcam is not");
    }
  } else {
      hdcam = false;
      Serial.println("no hdcam");
  }
  do_eprom_write();
}

char devname[30];

struct tm timeinfo;
time_t now;

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  eprom functions
//

#include <EEPROM.h>

struct eprom_data {
  bool checktim;
  bool hdcam; // hd=1,vga=0
  int timmin;
  char devname[16];
  char chat_id[16];
  char BOTtoken[52];
  char timzon[52];

  int eprom_good;
};

void do_eprom_read() {

  eprom_data ed;

  EEPROM.begin(200);
  EEPROM.get(0, ed);

  if (ed.eprom_good == MagicNumber) {
    Serial.println("Good settings in the EPROM ");

    /*
      Serial.println(ed.devname);
      Serial.println(ed.BOTtoken);
      Serial.println(ed.chat_id);
      Serial.println(ed.timzon);
      Serial.println(ed.timmin);
    */

    devstr = ed.devname;
    devstr.toCharArray(devname, 12); //devstr.length() + 1);
    BOTtoken = ed.BOTtoken;
    bot.updateToken(BOTtoken);
    chat_id = ed.chat_id;
    timezone = ed.timzon;
    hdcam = ed.hdcam;
    tim_enabled = ed.checktim;
    TimePhoto_Minutes = ed.timmin;

      Serial.println(devstr);
      Serial.println(BOTtoken);
      Serial.println(chat_id);
      Serial.println(timezone);
      Serial.println(TimePhoto_Minutes);
      Serial.println(hdcam);
    
  } else {
    Serial.println("No settings in EPROM ");

    chat_id = "1234567890";
    BOTtoken = "123456789:12345678901234567890123456789012345";
    timezone = "GMT+7";
    devstr = "FIRE-CAM";

    tim_enabled = 1;
    TimePhoto_Minutes = 30 ;
    hdcam = false;
    do_eprom_write();
    wm.resetSettings();
  }
}

void do_eprom_write() {

  eprom_data ed;
  ed.eprom_good = MagicNumber;

  devstr.toCharArray(ed.devname, 12); //devstr.length() + 1);
  BOTtoken.toCharArray(ed.BOTtoken, 50); //BOTtoken.length() + 1);
  chat_id.toCharArray(ed.chat_id, 12); //chat_id.length() + 1);
  timezone.toCharArray(ed.timzon, 50); //timezone.length() + 1);

  ed.checktim = tim_enabled  ;
  ed.hdcam = hdcam;
  ed.timmin = TimePhoto_Minutes;

  Serial.println("Writing to EPROM ...");

  /*
    Serial.println(ed.devname);
    Serial.println(ed.BOTtoken);
    Serial.println(ed.chat_id);
    Serial.println(ed.timzon);
    Serial.println(ed.timmin);
  */

  EEPROM.begin(200);
  EEPROM.put(0, ed);
  EEPROM.commit();
  EEPROM.end();
}

#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include <ESPmDNS.h>

bool init_wifi() {
  uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  do_eprom_read();

  devstr.toCharArray(devname, devstr.length() + 1);        // name of your camera for mDNS, Router, and filenames
  BOTtoken.toCharArray(def_BOTtoken, BOTtoken.length() + 1);
  chat_id.toCharArray(def_chat_id, chat_id.length() + 1);
  timezone.toCharArray(def_timezone, timezone.length() + 1);
  String x = "yes";
  x.toCharArray(def_hdcam,x.length()+1);
  
  WiFiManagerParameter dev("DevName", "Name of Device", devname, 12);
  wm.addParameter(&dev);

  WiFiManagerParameter id("chat_id", "Telegram ID", def_chat_id, 15);
  wm.addParameter(&id);
  WiFiManagerParameter pass ("BOTtoken", "Telegram Pass", def_BOTtoken, 50);
  wm.addParameter(&pass);

  WiFiManagerParameter timzon ("timzon", "Time Zone", def_timezone, 50);
  wm.addParameter(&timzon);
  WiFiManagerParameter hdcam_check("phdcam", "<p>HD? (or VGA)", def_hdcam, 3, "type=\"checkbox\"" );
  wm.addParameter(&hdcam_check);
    
  wm.setSaveParamsCallback(saveParamCallback);

  std::vector<const char *> menu = {"wifi", "info", "sep", "restart", "exit"};
  wm.setMenu(menu);

  // set dark theme
  wm.setClass("invert");

  bool res;
  //wm.resetSettings();  // for debugging

  wm.setConnectTimeout(60 * 5); // how long to try to connect for before continuing
  wm.setConfigPortalTimeout(60 * 5); // auto close configportal after n seconds

  // res = wm.autoConnect(); // auto generated AP name from chipid

  res = wm.autoConnect(devname); // use the devname defined above, with no password
  //res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if (res) {
    Serial.println("Succesful Connection using WiFiManager");
    do_eprom_write();
  } else {

    //InternetFailed = true;
    Serial.println("Internet failed using WiFiManager - not starting Web services");
  }

  wifi_ps_type_t the_type;

  //esp_err_t get_ps = esp_wifi_get_ps(&the_type);
  esp_err_t set_ps = esp_wifi_set_ps(WIFI_PS_NONE);

  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp);

  configTime(0, 0, "pool.ntp.org");
  char tzchar[80];
  timezone.toCharArray(tzchar, timezone.length());          // name of your camera for mDNS, Router, and filenames
  setenv("TZ", tzchar, 1);  // mountain time zone from #define at top
  tzset();

  if (!MDNS.begin(devname)) {
    Serial.println("Error setting up MDNS responder!");
    return false;
  } else {
    Serial.printf("mDNS responder started '%s'\n", devname);
  }
  time(&now);

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  return true;
}
#define TRIGGER_PIN 13         // Chân nhận tín hiệu từ Master
bool isTriggered = false;
unsigned long timeReset=millis();
static void IRAM_ATTR handleButton() {
  Serial.println("Push button 5s for reset default!");
  timeReset=millis();
}
//////////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);
  //wm.resetSettings();
  pinMode(TRIGGER_PIN, INPUT_PULLDOWN);
  Serial.println("---------------------------------");
  Serial.printf("ESP32-CAM Video-Telegram %s\n", vernum);
  Serial.println("---------------------------------");

  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, flashState); //defaults to low

  pinMode(33, OUTPUT);             // little red led on back of chip
  digitalWrite(33, LOW);           // turn on the red LED on the back of chip

  pinMode(14,INPUT_PULLUP);
  attachInterrupt(14,handleButton,FALLING);

  //Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  //Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  bool wifi_status = init_wifi();

  if (hdcam) {
    configframesize = FRAMESIZE_HD; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    framesize = FRAMESIZE_HD; //FRAMESIZE_HD;
    Serial.println("Camera to HD ");
  } else {
    configframesize = FRAMESIZE_VGA; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    framesize = FRAMESIZE_VGA; //FRAMESIZE_HD;
    Serial.println("Camera to VGA ");
  }

  if (!setupCamera()) {
    Serial.println("Camera Setup Failed!");
    while (true) {
      delay(100);
    }
  }

  for (int j = 0; j < 7; j++) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera Capture Failed");
    } else {
      Serial.print("Pic, len="); Serial.print(fb->len);
      Serial.printf(", new fb %X\n", (long)fb->buf);
      esp_camera_fb_return(fb);
      delay(50);
    }
  }

  // Make the bot wait for a new message for up to 60seconds
  //bot.longPoll = 60;
  bot.longPoll = 5;

  client.setInsecure();

  Serial.print("Sending a message to: ");
  Serial.print(chat_id);
  Serial.print(" at ");
  Serial.println(BOTtoken);

  String stat = "Reboot\nDevice: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " +  WiFi.localIP().toString() + "\n/start";
  Serial.println(stat);

  if( bot.sendMessage(chat_id, stat, "")){
    Serial.println("Initial send worked!");
  } else {
    Serial.println("Initial send failed, reseting WiFi Parameters");
    reset_request = true;
  }

  digitalWrite(33, HIGH);
}

int loopcount = 0;

void loop() {
  if(digitalRead(14)==LOW&&millis()-timeReset>5000){
    wm.resetSettings();
    Serial.println("Reset default!");
    timeReset=millis();
  }
  loopcount++;

  client.setHandshakeTimeout(120000); // workaround for esp32-arduino 2.02 bug https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/issues/270#issuecomment-1003795884
  if (digitalRead(TRIGGER_PIN) == HIGH && !isTriggered) {
    isTriggered = true; 
    Serial.println("\n NHẬN TÍN HIỆU BÁO CHÁY TỪ MASTER! Đang chụp ảnh cảnh báo...");

    for (int j = 0; j < 4; j++) {
      camera_fb_t * newfb = esp_camera_fb_get();
      if (newfb) { esp_camera_fb_return(newfb); delay(10); }
    }
    camera_fb_t * tfb = esp_camera_fb_get();
    if (!tfb) {
      Serial.println("Lỗi: Không thể chụp ảnh cảnh báo");
    } else {
      currentByte = 0;
      fb_length = tfb->len;
      fb_buffer = tfb->buf;

      Serial.println(">>>>> Đang gửi ảnh khẩn cấp, dung lượng: " + String(fb_length) + " bytes");

      // 3. Gửi hàm Multipart với Caption Cảnh báo
      String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                    "image/jpeg", "[CẢNH BÁO NGUY HIỂM] - PHÁT HIỆN SỰ CỐ TẠI TRẠM SẠC!", chat_id, fb_length,
                    isMoreDataAvailable, getNextByte, nullptr, nullptr);

      Serial.println("Đã gửi cảnh báo xong!");
    }
    if (tfb) esp_camera_fb_return(tfb); 
  }

  if (digitalRead(TRIGGER_PIN) == LOW && isTriggered) {
    isTriggered = false;
  }

  if (reboot_request) {
    String stat = "Rebooting on request\nDevice: " + devstr + "\nVer: " + String(vernum) + "\nRssi: " + String(WiFi.RSSI()) + "\nip: " +  WiFi.localIP().toString() ;
    bot.sendMessage(chat_id, stat, "");
    delay(10000);
    ESP.restart();
  }

  if (reset_request) {
    wm.resetSettings();
    reset_request = false;
  }

  if (millis() > Bot_lasttime + Bot_mtbs )  {

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("***** WiFi reconnect *****");
      WiFi.reconnect();
      delay(5000);
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("***** WiFi rerestart *****");
        init_wifi();
      }
    }

    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    while (numNewMessages) {
      //Serial.println("got response");
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    Bot_lasttime = millis();
  }

  if (millis() > (TimePhoto_lasttime + TimePhoto_Minutes * 60000) ) {
    if (tim_enabled) {
      for (int j = 0; j < 4; j++) {
        camera_fb_t * newfb = esp_camera_fb_get();
        if (!newfb) {
          Serial.println("Camera Capture Failed");
        } else {
          //Serial.print("Pic, len="); Serial.print(newfb->len);
          //Serial.printf(", new fb %X\n", (long)newfb->buf);
          esp_camera_fb_return(newfb);
          delay(10);
        }
      }

      camera_fb_t * tfb = esp_camera_fb_get();
      if (!tfb) {
        Serial.println("Camera Capture Failed");
      } else {


        currentByte = 0;
        fb_length = tfb->len;
        fb_buffer = tfb->buf;

        Serial.println("\n>>>>> Sending timed photo, bytes=  " + String(fb_length));

        String sent = bot.sendMultipartFormDataToTelegramWithCaption("sendPhoto", "photo", "img.jpg",
                      "image/jpeg", "Timed Photo", chat_id, fb_length,
                      isMoreDataAvailable, getNextByte, nullptr, nullptr);

        Serial.println("done!");
      }
      esp_camera_fb_return(tfb);
      delay(10);
    }
    TimePhoto_lasttime = millis();
  }
}