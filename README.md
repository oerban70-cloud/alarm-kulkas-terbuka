# Alarm Kulkas Terbuka — ESP32 + Reed Switch + Buzzer + LCD

Pintu kulkas lupa ditutup? Alat kecil ini yang mengingatkanmu sebelum makanan
rusak dan tagihan listrik membengkak. Saklar magnet (reed switch) di pintu
kulkas mendeteksi pintu terbuka; kalau terbuka lebih dari 30 detik, buzzer
berbunyi, LED merah menyala, LCD menampilkan sudah berapa lama pintunya
terbuka, dan notifikasi Telegram terkirim ke HP.

Video Short (60 detik): link menyusul setelah upload.

## Cara kerja

1. Magnet kecil ditempel di daun pintu kulkas, saklar reed di kusennya.
   Pintu tertutup, magnet menempel, saklar menutup (pin LOW).
2. Pintu terbuka, magnet menjauh, saklar membuka (pin HIGH, pull-up internal).
   ESP32 mulai menghitung sejak pintu terbuka.
3. Terbuka 30 detik, alarm aktif: buzzer berbunyi putus-putus (200 ms bunyi,
   300 ms diam), LED merah menyala, LCD menampilkan durasi terbuka,
   Telegram terkirim sekali per kejadian.
4. Tekan tombol bisu, alarm diam 5 menit (misalnya sedang memasukkan
   belanjaan). Setelah 5 menit, kalau pintu masih terbuka, alarm berbunyi lagi.
5. Pintu tertutup, alarm berhenti sendiri, LED hijau menyala (kulkas aman),
   Telegram mengabarkan pintu sudah tertutup beserta lamanya terbuka.

## Wiring

| Komponen | Pin modul | ESP32 |
| --- | --- | --- |
| Reed switch kaki A | A | GPIO4 |
| Reed switch kaki B | B | GND |
| Buzzer aktif HIGH (+) | + | GPIO25 |
| Buzzer (-) | - | GND |
| LED merah (seri 220 Ω) | Anoda | GPIO2 |
| LED hijau (seri 220 Ω) | Anoda | GPIO15 |
| Tombol bisu | Kaki 1 | GPIO14 |
| Tombol bisu | Kaki 2 | GND |
| LCD 16x2 I2C (0x27) SDA | SDA | GPIO21 |
| LCD 16x2 I2C (0x27) SCL | SCL | GPIO22 |
| LCD 16x2 I2C (0x27) VCC | VCC | VIN 5V |
| LCD 16x2 I2C (0x27) GND | GND | GND |
| Adaptor USB 5V 2A | 5V / GND | VIN / GND (ESP32 dicatu via USB) |

Skema lengkap (gaya Fritzing): lihat `schematic.png`.

## Konfigurasi

Di `AlarmKulkas/AlarmKulkas.ino`:

- `BATAS_ALARM_DETIK` (30) — pintu terbuka selama ini, alarm berbunyi.
- `BISU_MENIT` (5) — tombol bisu mematikan alarm selama 5 menit.
- `BEEP_NYALA_MS` (200) / `BEEP_MATI_MS` (300) — pola bip-bip buzzer.
- `BACA_INTERVAL_MS` (100) / `DEBOUNCE_MS` (50) — baca saklar + debounce.
- `USE_TELEGRAM` (1) — 1 = kirim notifikasi Telegram, 0 = tanpa Telegram.
  Isi `WIFI_SSID`, `WIFI_PASS`, `BOT_TOKEN`, dan `CHAT_ID` bila Telegram aktif.

## Komponen

- ESP32 DevKit (WiFi + Bluetooth)
- Reed switch + magnet kecil (pasangan saklar magnet pintu)
- Buzzer aktif HIGH (5V)
- LED merah + LED hijau + resistor 220 Ω (2 buah)
- Tombol tekan (push button)
- LCD 16x2 I2C alamat 0x27
- Adaptor USB 5V 2A + kabel USB

## Compile (terverifikasi)

```
arduino-cli compile --fqbn esp32:esp32:esp32 AlarmKulkas
Sketch uses 1089765 bytes (83%) of program storage space. Maximum is 1310720 bytes.
Global variables use 51936 bytes (15%) of dynamic memory, leaving 275744 bytes
for local variables. Maximum is 327680 bytes.
```

Terverifikasi 21 November 2026, ESP32 core 4.0.0, arduino-cli 1.6.0.

## Library

Folder `libraries/` berisi ZIP library yang dipakai (pasang lewat
Sketch > Include Library > Add .ZIP Library bila belum ada):

- LiquidCrystal_I2C 1.1.2
- UniversalTelegramBot 1.3.0 (hanya bila `USE_TELEGRAM` 1)
- ArduinoJson 6.21.5 (hanya bila `USE_TELEGRAM` 1)

## Lisensi

MIT — lihat `LICENSE`.
