// Alarm Kulkas Terbuka — ESP32 + Reed Switch + Buzzer + LCD 16x2 I2C
//
// Cara kerja:
//  - Saklar reed dipasang di kusen kulkas, magnet kecil di daun pintu.
//    Pintu tertutup = magnet menempel = saklar menutup (pin LOW).
//    Pintu terbuka = magnet menjauh = saklar membuka (pin HIGH).
//  - ESP32 membaca saklar tiap 100 ms dengan debounce 50 ms.
//  - Pintu terbuka lebih dari 30 detik = alarm: buzzer berbunyi
//    putus-putus, LED merah menyala, LCD menampilkan sudah berapa lama
//    pintu terbuka. Telegram terkirim sekali tiap kejadian alarm.
//  - Tekan tombol = alarm dibisukan 5 menit (mis. sedang memasukkan
//    belanjaan). Alarm berhenti sendiri begitu pintu tertutup.
//  - LED hijau menyala selama pintu tertutup (kulkas aman).
//
// Wiring:
//  - Reed switch : satu kaki -> GPIO4, kaki lain -> GND
//    (INPUT_PULLUP, aktif LOW saat pintu tertutup)
//  - Buzzer aktif HIGH : + -> GPIO25, - -> GND
//  - LED merah : GPIO2 (seri resistor 220 ohm ke GND)
//  - LED hijau : GPIO15 (seri resistor 220 ohm ke GND)
//  - Tombol bisu : GPIO14 -> GND (INPUT_PULLUP, aktif LOW)
//  - LCD 16x2 I2C (0x27) : SDA -> GPIO21, SCL -> GPIO22,
//    VCC -> VIN 5V, GND -> GND
//
// Library yang dibutuhkan (semua tersedia di Library Manager):
//  - LiquidCrystal_I2C 1.1.2,
//    UniversalTelegramBot 1.3.0 + ArduinoJson 6.21.5
//    (hanya bila USE_TELEGRAM 1).
// Compile: arduino-cli compile --fqbn esp32:esp32:esp32 AlarmKulkas
// (terverifikasi 21 Nov 2026: lihat README.md untuk angka pastinya,
//  ESP32 core 4.0.0, COMPILE_RC=0)

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------- KONFIGURASI ----------
#define USE_TELEGRAM 1  // 1 = kirim notifikasi Telegram, 0 = tanpa Telegram

#define PIN_REED 4       // saklar reed pintu kulkas (INPUT_PULLUP)
#define PIN_BUZZER 25    // buzzer aktif HIGH
#define PIN_LED_MERAH 2  // LED alarm
#define PIN_LED_HIJAU 15 // LED pintu tertutup (aman)
#define PIN_TOMBOL 14    // tombol bisu (INPUT_PULLUP, aktif LOW)

#define BATAS_ALARM_DETIK 30   // pintu terbuka selama ini -> alarm bunyi
#define BACA_INTERVAL_MS 100   // baca saklar tiap 100 ms
#define DEBOUNCE_MS 50         // saklar harus stabil selama ini
#define BEEP_NYALA_MS 200      // buzzer bunyi 200 ms ...
#define BEEP_MATI_MS 300       // ... lalu diam 300 ms (pola bip-bip)
#define BISU_MENIT 5           // tombol bisu mematikan alarm selama 5 menit

const char *WIFI_SSID = "NAMA_WIFI";
const char *WIFI_PASS = "PASSWORD_WIFI";
const char *BOT_TOKEN = "TOKEN_BOT_TELEGRAM";
const char *CHAT_ID = "CHAT_ID_TELEGRAM";

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Status pintu (sudah debounce) dan waktu kejadiannya.
bool pintuTerbuka = false;
unsigned long terbukaSejak = 0;     // millis() saat pintu mulai terbuka
bool alarmAktif = false;            // alarm sedang berbunyi
bool telegramAlarmTerkirim = false; // cegah Telegram ganda per kejadian
unsigned long bisuSampai = 0;       // millis() akhir masa bisu (0 = tidak bisu)
unsigned long bacaTerakhir = 0;     // millis() baca saklar terakhir
unsigned long lcdTerakhir = 0;      // millis() update LCD terakhir
unsigned long stabilSejak = 0;      // millis() bacaan mentah terakhir berubah
bool bacaanMentah = HIGH;           // bacaan pin terakhir (belum debounce)
bool tombolDulu = HIGH;             // status tombol loop sebelumnya

#if USE_TELEGRAM
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
WiFiClientSecure telegramClient;
UniversalTelegramBot bot(BOT_TOKEN, telegramClient);

void kirimTelegram(const String &pesan) {
  if (WiFi.status() != WL_CONNECTED) return;
  bot.sendMessage(CHAT_ID, pesan, "");
}

void sambungWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) {
    delay(250);
  }
}
#else
void kirimTelegram(const String &) {}
void sambungWiFi() {}
#endif

// Format detik menjadi "MM:SS" untuk LCD dan Telegram.
void formatDurasi(unsigned long detik, char *buf, size_t len) {
  snprintf(buf, len, "%02lu:%02lu", detik / 60, detik % 60);
}

// Tampilkan status di LCD (16 kolom, diratakan supaya tidak ada sisa huruf).
void tampilLCD(const char *baris0, const char *baris1) {
  char r0[17], r1[17];
  snprintf(r0, sizeof(r0), "%-16s", baris0);
  snprintf(r1, sizeof(r1), "%-16s", baris1);
  lcd.setCursor(0, 0);
  lcd.print(r0);
  lcd.setCursor(0, 1);
  lcd.print(r1);
}

// Pintu baru saja terbuka: catat waktunya, matikan LED hijau.
void pintuMulaiTerbuka() {
  pintuTerbuka = true;
  terbukaSejak = millis();
  digitalWrite(PIN_LED_HIJAU, LOW);
  Serial.println("Pintu kulkas terbuka.");
}

// Pintu baru saja tertutup: matikan semua alarm, nyalakan LED hijau,
// laporkan durasi terbuka lewat Telegram bila sempat berbunyi alarm.
void pintuTertutup() {
  unsigned long lamaDetik = (millis() - terbukaSejak) / 1000;
  pintuTerbuka = false;
  alarmAktif = false;
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_LED_HIJAU, HIGH);
  tampilLCD("Pintu tertutup", "Kulkas aman");
  if (telegramAlarmTerkirim) {
    char dur[8];
    formatDurasi(lamaDetik, dur, sizeof(dur));
    kirimTelegram(String("Pintu kulkas tertutup setelah terbuka ") +
                  dur + " (mm:dd).");
    telegramAlarmTerkirim = false;
  }
  Serial.print("Pintu kulkas tertutup setelah ");
  Serial.print(lamaDetik);
  Serial.println(" detik.");
}

// Alarm baru saja aktif: nyalakan LED merah, beri tahu lewat Telegram.
void alarmMulai(unsigned long lamaDetik) {
  alarmAktif = true;
  digitalWrite(PIN_LED_MERAH, HIGH);
  char dur[8];
  formatDurasi(lamaDetik, dur, sizeof(dur));
  kirimTelegram(String("Pintu kulkas terbuka ") + dur +
                " (mm:dd) — tutup pintunya!");
  telegramAlarmTerkirim = true;
  Serial.println("ALARM: pintu terbuka terlalu lama!");
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_TOMBOL, INPUT_PULLUP);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);

  lcd.init();
  lcd.backlight();
  tampilLCD("Alarm Kulkas", "Siap mengawasi");

  // Status awal: pintu tertutup (magnet menempel) atau terbuka?
  bacaanMentah = digitalRead(PIN_REED);
  pintuTerbuka = (bacaanMentah == HIGH);
  if (pintuTerbuka) {
    terbukaSejak = millis();
  } else {
    digitalWrite(PIN_LED_HIJAU, HIGH);
  }
  stabilSejak = millis();

#if USE_TELEGRAM
  sambungWiFi();
  telegramClient.setInsecure();
#endif

  Serial.println("Alarm Kulkas Terbuka siap.");
}

void loop() {
  unsigned long sekarang = millis();

  // --- Baca saklar tiap 100 ms, debounce 50 ms ---
  if (sekarang - bacaTerakhir >= BACA_INTERVAL_MS) {
    bacaTerakhir = sekarang;
    bool baca = digitalRead(PIN_REED);
    if (baca != bacaanMentah) {
      bacaanMentah = baca;
      stabilSejak = sekarang;
    } else if (sekarang - stabilSejak >= DEBOUNCE_MS) {
      // Bacaan stabil: pintu terbuka = HIGH (saklar lepas), tertutup = LOW.
      bool terbuka = (bacaanMentah == HIGH);
      if (terbuka && !pintuTerbuka) pintuMulaiTerbuka();
      if (!terbuka && pintuTerbuka) pintuTertutup();
    }
  }

  // --- Tombol bisu ditekan (aktif LOW, tepi turun) ---
  bool tombol = digitalRead(PIN_TOMBOL);
  if (tombol == LOW && tombolDulu == HIGH) {
    delay(50); // debounce tombol
    if (digitalRead(PIN_TOMBOL) == LOW) {
      bisuSampai = millis() + BISU_MENIT * 60UL * 1000UL;
      digitalWrite(PIN_BUZZER, LOW);
      digitalWrite(PIN_LED_MERAH, LOW);
      tampilLCD("Alarm dibisukan", "5 menit");
      Serial.println("Alarm dibisukan 5 menit.");
    }
  }
  tombolDulu = tombol;

  // --- Pintu terbuka: hitung durasi, tentukan alarm & LCD ---
  if (pintuTerbuka) {
    unsigned long lamaDetik = (sekarang - terbukaSejak) / 1000;
    bool dibisukan = (bisuSampai > 0 && sekarang < bisuSampai);

    if (lamaDetik >= BATAS_ALARM_DETIK && !dibisukan && !alarmAktif) {
      alarmMulai(lamaDetik);
    }

    // Pola bip-bip: buzzer bunyi 200 ms, diam 300 ms, selama alarm aktif.
    if (alarmAktif && !dibisukan) {
      digitalWrite(PIN_LED_MERAH, HIGH);
      unsigned long fase = (sekarang - terbukaSejak) % (BEEP_NYALA_MS + BEEP_MATI_MS);
      digitalWrite(PIN_BUZZER, fase < BEEP_NYALA_MS ? HIGH : LOW);
    } else {
      digitalWrite(PIN_BUZZER, LOW);
    }

    // LCD diupdate sekali per detik (durasi berubah tiap detik).
    if (sekarang - lcdTerakhir < 1000) return;
    lcdTerakhir = sekarang;
    // LCD: baris atas status, baris bawah durasi terbuka.
    char dur[8];
    formatDurasi(lamaDetik, dur, sizeof(dur));
    char baris1[17];
    if (alarmAktif && !dibisukan) {
      snprintf(baris1, sizeof(baris1), "Terbuka %s !", dur);
      tampilLCD("PINTU TERBUKA!", baris1);
    } else if (dibisukan) {
      snprintf(baris1, sizeof(baris1), "Terbuka %s", dur);
      tampilLCD("Alarm dibisukan", baris1);
    } else {
      snprintf(baris1, sizeof(baris1), "Terbuka %s", dur);
      tampilLCD("Pintu terbuka", baris1);
    }
  }
}
