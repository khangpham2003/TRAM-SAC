#define BLYNK_TEMPLATE_ID "TMPL6-Se28ZcM"
#define BLYNK_TEMPLATE_NAME "Tram Sac"
#define BLYNK_AUTH_TOKEN "seIx3VazMb9ZVTDpCU2J27lYf8K_wnf-"
// Đảm bảo in thông tin debug của Blynk ra Serial Monitor
#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>

// ================= THÔNG TIN WIFI =================
char ssid[] = "Phamdinhkhang";
char pass[] = "khang2003";

// ================= KHAI BÁO CHÂN (PINOUT) =================
const int RELAY_PIN   = 26; // Điều khiển Relay 30A (Active HIGH)
const int BUZZER_PIN  = 32; // Điều khiển còi báo động (Active HIGH)
const int MQ2_PIN     = 34; // Đọc ADC cảm biến khói
const int RESET_PIN   = 14; // Nút nhấn Reset vật lý (Active LOW)
const int CAM_TRIG_PIN = 4; // Kích hoạt ESP32-CAM chụp ảnh

// ================= NGƯỠNG BẢO VỆ AN TOÀN =================
const float MAX_CURRENT = 15.0; // Ampe
const float MAX_TEMP    = 75.0; // Độ C
const int   MAX_SMOKE   = 500; // ADC value

// ================= KHỞI TẠO ĐỐI TƯỢNG =================
PZEM004Tv30 pzem(Serial2, 16, 17); // UART2 (RX=16, TX=17)
Adafruit_MLX90614 mlx = Adafruit_MLX90614();
LiquidCrystal_I2C lcd(0x27, 20, 4); // Địa chỉ I2C màn hình (thường là 0x27 hoặc 0x3F)
BlynkTimer timer;

// ================= BIẾN TOÀN CỤC =================
boolean isAlarmActive = false; // Cờ chốt trạng thái (Latching Flag)
float currentV = 0, currentI = 0, currentP = 0, currentTemp = 0;
int currentSmoke = 0;

// ================= HÀM XỬ LÝ NÚT RESET TRÊN APP BLYNK (V0) =================
BLYNK_WRITE(V0) {
  int virtualResetBtn = param.asInt();
  if (virtualResetBtn == 1 && isAlarmActive == true) {
    resetSystem();
  }
}

// ================= HÀM ĐỌC DỮ LIỆU CẢM BIẾN =================
void readSensors() {
  currentV = pzem.voltage();
  currentI = pzem.current();
  currentP = pzem.power();
  if (isnan(currentV)) { currentV = 0; currentI = 0; currentP = 0; }

  currentTemp = mlx.readObjectTempC();
  currentSmoke = analogRead(MQ2_PIN);

  // Hiển thị Hàng 1: Điện áp & Dòng điện
  lcd.setCursor(0, 0);
  lcd.print("DIEN: "); lcd.print(currentV, 0); lcd.print("V | "); 
  lcd.print(currentI, 1); lcd.print("A   ");

  // Hiển thị Hàng 2: Công suất
  lcd.setCursor(0, 1);
  lcd.print("  _CONG SUAT: "); lcd.print(currentP, 0); lcd.print("W    ");
  
  // Hiển thị Hàng 3: Nhiệt độ & Khói
  lcd.setCursor(0, 2);
  lcd.print("NHIET:"); lcd.print(currentTemp, 1); lcd.print("C");
  lcd.print("|KHOI:"); lcd.print(currentSmoke); lcd.print("   ");

  // Hiển thị Hàng 4: Trạng thái
  lcd.setCursor(0, 3);
  if (isAlarmActive) {
    lcd.print("!!! NGUY HIEM !!!   ");
  } else {
    lcd.print("TRANG THAI: AN TOAN ");
  }
}

// ================= HÀM GỬI DỮ LIỆU LÊN BLYNK =================
void sendToBlynk() {
  Blynk.virtualWrite(V2, currentV);
  Blynk.virtualWrite(V1, currentI);
  Blynk.virtualWrite(V3, currentP);
  Blynk.virtualWrite(V4, currentTemp);
  Blynk.virtualWrite(V5, currentSmoke);
}

// ================= HÀM KHÔI PHỤC HỆ THỐNG (RESET) =================
void resetSystem() {
  Serial.println("[HỆ THỐNG] Nguoi quan ly xac nhan an toan. Khoi phuc sạc.");
  isAlarmActive = false;
  digitalWrite(RELAY_PIN, HIGH); // Đóng điện lại
  digitalWrite(BUZZER_PIN, LOW); // Tắt còi
  Blynk.virtualWrite(V0, 0);     // Reset nút trên app về 0
  digitalWrite(CAM_TRIG_PIN, LOW);

  lcd.clear();
  lcd.print("  KHOI PHUC...  ");
  delay(1000);
  lcd.clear();
}

// ================= LÕI LOGIC AN TOÀN (LATCHING LOGIC) =================
void safetyCheckLoop() {
  readSensors();

  // 1. Nếu hệ thống đang bình thường -> Kiểm tra điều kiện vi phạm
  if (!isAlarmActive) {
    if (currentI > MAX_CURRENT || currentTemp > MAX_TEMP || currentSmoke > MAX_SMOKE) {
      Serial.println("!!! PHAT HIEN NGUY HIEM - KICH HOAT CACH LY !!!");
      
      // Khóa trạng thái báo động
      isAlarmActive = true; 
      
      // Thực thi cách ly phần cứng
      digitalWrite(RELAY_PIN, LOW); // Ngắt điện lập tức
      digitalWrite(BUZZER_PIN, HIGH); // Hú còi
      
      // Gửi xung Trigger kích hoạt ESP32-CAM chụp ảnh
      digitalWrite(CAM_TRIG_PIN, HIGH);


      // Đẩy cảnh báo lên Blynk
      Blynk.logEvent("fire_alarm", "Cảnh báo: Phát hiện sự cố tại trạm sạc! Đã ngắt điện an toàn.");
    }
  } 
  // 2. Nếu hệ thống ĐANG BỊ KHÓA (Báo động) -> Chờ người dùng nhấn nút Reset vật lý
  else {
    // Quét nút nhấn PBS-11B (Nhấn = LOW)
    if (digitalRead(RESET_PIN) == LOW) {
      // Chỉ cho phép khôi phục nếu các chỉ số đã nguội/hạ xuống mức an toàn
      if (currentTemp < 60.0 && currentSmoke < 1000) {
         resetSystem();
      } else {
         Serial.println("[CẢNH BÁO] Nhiet do hoac khoi van chua tan, khong the Reset!");
         lcd.clear();
         lcd.print(" CHUA AN TOAN! ");
         delay(1000);
         lcd.clear();
      }
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  // Khởi tạo chân I/O
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(CAM_TRIG_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  
  // Trạng thái phần cứng khởi động: Đóng tải, Tắt còi, Tắt CAM
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(CAM_TRIG_PIN, LOW);

  // Khởi tạo ngoại vi
  Wire.begin();
  mlx.begin();
  lcd.init();
  lcd.backlight();
  lcd.print("Khoi dong he");
  lcd.setCursor(0, 1);
  lcd.print("thong an toan...");

  // Kết nối mạng
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);
  lcd.clear();

  // Cấu hình đa nhiệm Timer:
  // - Quét an toàn và đọc cảm biến mỗi 0.5 giây (Tốc độ phản ứng khẩn cấp)
  timer.setInterval(500L, safetyCheckLoop);
  // - Đẩy dữ liệu lên Blynk mỗi 3 giây (Đảm bảo không bị tràn mạng)
  timer.setInterval(3000L, sendToBlynk);
}

// ================= VÒNG LẶP CHÍNH =================
void loop() {
  Blynk.run();
  timer.run();
}
