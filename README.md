# 📻 End-to-End RF Audio Streaming System (ESP32 S3 & LoRa E22)

![ESP32](https://img.shields.io/badge/ESP32-S3-blue?style=flat&logo=espressif)
![LoRa](https://img.shields.io/badge/LoRa-E22--900T22D-orange?style=flat)
![Python](https://img.shields.io/badge/Python-3.8+-yellow?style=flat&logo=python)
![Architecture](https://img.shields.io/badge/Architecture-State_Machine-success?style=flat)

Bu proje, bir INMP441 MEMS mikrofon üzerinden alınan **16kHz, 16-bit** ham PCM ses verisini, dar bantlı (narrow-band) bir RF modülü olan **LoRa E22** üzerinden iletmek ve alıcı tarafta yeniden birleştirerek (Reassembly) bilgisayar ortamında `.wav` formatına dönüştürmek amacıyla geliştirilmiş endüstriyel bir Gömülü Sistem mimarisidir.

Sistem; eşzamanlı donanım yönetimi, veri parçalama (fragmentation), paket kayıp oranı (PLR) hesaplaması ve asenkron zaman aşımı (non-blocking timeout) gibi ileri seviye mühendislik konseptlerini barındırır.

# 📡 ESP32 S3 I2S Audio over LoRa

> **32ms'lik kristal net ses — DMA ile kaydedilir, LoRa E22 ile iletilir.**

Bir INMP441 MEMS mikrofon, bir ESP32 S3 ve bir LoRa E22 modülü ile gerçek zamanlı ses verisi kablosuz aktarımı. Sıfır CPU yükü, sıfır gecikme tasarımı.

---

## 🏗️ Mimari Genel Bakış

```
[INMP441 Mikrofon]
      │  I2S (Philips Std)
      ▼
[ESP32 S3 — I2S DMA Donanımı]
  32-bit fiziksel hat → 16-bit DMA dönüşümü (donanımsal, CPU yok)
      │  512 örnek × 16-bit = 1024 byte
      ▼
[pcm_buffer — SRAM]
      │  Paketlere bölünür (5 × 210 byte)
      ▼
[LoRa E22-900T22D]
  Adresli gönderim → Karşı Alıcı
```

---

## 🎥 Proje Tanıtım Videosu

[![ESP32 I2S LoRa Audio](https://img.youtube.com/vi/YlLklpSxGew/maxresdefault.jpg)](https://www.youtube.com/watch?v=YlLklpSxGew)

> 🎬 Videoyu izlemek için görsele tıklayın
 
 

## 🎙️ Ses Kayıt Tasarımı

### Neden `driver/i2s_std.h`?

Arduino'nun standart `ESP_I2S.h` kütüphanesi yerine **Espressif'in doğrudan sürücüsü** tercih edildi.  
Bu sayede DMA descriptor sayısı, frame boyutu ve donanım dönüşümleri doğrudan kontrol edilebiliyor.

### 32-bit → 16-bit Donanımsal Dönüşüm

INMP441, sesi **24-bit çözünürlükte** üretir. I2S protokolü bu veriyi **32-bitlik slot** içinde taşır.  
`int24_t` diye bir C tipi olmadığından ve LoRa bandını verimli kullanmak gerektiğinden, donanım katmanında MSB kesme yöntemi kullanıldı:

```cpp
// DMA'ya: "Bana 16-bit ver"
.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, ...)

// Donanıma: "Fiziksel hattı 32-bit dinle"
std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
```

**Sonuç:** ESP32 S3 DMA birimi, gelen 32-bit veriyi alır → alt 16 biti atar → sesin karakterini taşıyan üst 16 biti (MSB) doğrudan RAM'e yazar. CPU hiç dahil olmaz.

### Çerçeve Boyutu: 32ms

```
16.000 Hz × 0.032 s = 512 örnek (tam sayı, kesir yok)
512 × 2 byte = 1024 byte / çerçeve
```

---

## ⚙️ DMA & Durum Makinesi

### Neden DMA?

- İşlemci hiç yorulmaz — kayıt arka planda gerçekleşir
- Buffer dolduğunda donanım otomatik olarak callback tetikler
- Callback içinde yalnızca **flag güncellenir** (WDT güvenli)
- Asıl işlem ana döngüde yapılır → çökme riski sıfıra yakın

### State Machine

```
STATE_IDLE
    │  i2s_channel_read() başlatılır
    ▼
STATE_RECORDING
    │  DMA arka planda 1024 byte doldurur
    │  [ISR] on_recv_done() → flag
    ▼
STATE_FRAME_READY
    │  I2S kanalı kapatılır
    ▼
STATE_PROCESSING
    │  Paketleme + LoRa gönderimi
    ▼
STATE_ERROR  (overflow veya donanım hatası)
```

`switch/case` yapısı Assembly katmanında **sıfır performans kaybıyla** durum geçişi yapar.

---

## 📦 LoRa Paket Yapısı

1024 byte ses verisi **5 pakete** bölünerek gönderilir:

```c
#pragma pack(push, 1)
struct AudioPacket {
    uint32_t cerceve_no;              // 4 byte  — senkronizasyon
    uint16_t kaynak_id;               // 2 byte  — cihaz kimliği
    uint8_t  veri_uzunlugu;           // 1 byte  — gerçek veri boyutu
    uint8_t  paket_no;                // 1 byte  — sıra numarası
    uint8_t  toplam_paket;            // 1 byte  — toplam paket sayısı
    char     sifre[10];               // 10 byte — erişim denetimi
    int16_t  ses_verisi[105];         // 210 byte — PCM verisi
};  // TOPLAM: 229 byte
#pragma pack(pop)
```

`#pragma pack(push, 1)` ile struct hizalaması garantilendi — platform bağımsız doğru boyut.

---

## 📡 LoRa E22 Dinamik Yapılandırma

> Bu kütüphane ve PCB tasarımı **[Fixaj Teknik](https://github.com/fixajteknik/YouTube_Tutorials)** tarafından geliştirilmiştir.

Standart LoRa modülleri parametre ayarları için harici bir programlayıcı gerektirir.  
Bu tasarımda modül **yazılım üzerinden dinamik olarak yapılandırılır**:

- ✅ Ekstra programlayıcı donanım gerekmez
- ✅ Modül değiştirildiğinde sistem kendini yeniden ayarlar
- ✅ Adres, frekans ve şifre bilgisi donanıma gömülü değil, kodda yönetilir
- ✅ Saha kullanıcısı için sıfır konfigürasyon yükü

Referans proje ve PCB şemaları:  
🔗 [Video 108 — #define Part 2 / RSSİ li E22-900T22](https://github.com/fixajteknik/YouTube_Tutorials/tree/main/Video%20108%20%23define%20Part%202/RSS%C4%B0%20li%20e22900t22)

---

## 🔌 Donanım Bağlantıları (ESP32-S3 İçin)
> **Not:** Klasik ESP32'de Input-Only pinlerine (34-39) WS/SCK bağlanamaz. S3 için en güvenli pinler 1, 2 ve 3'tür.

| INMP441 Mikrofon | ESP32-S3 (TX) Pin | Açıklama |
| :--- | :--- | :--- |
| VDD / GND | 3.3V / GND | Güç Beslemesi |
| L/R | GND | Sol Kanal Seçimi |
| WS | GPIO 1 | Word Select (LRCK) |
| SCK | GPIO 2 | Serial Clock (BCLK) |
| SD | GPIO 3 | Serial Data (DIN) |

| LoRa E22 Modülü | ESP32-S3 (TX/RX) Pin |
| :--- | :--- |
| M0 / M1 | GPIO 4 / GPIO 6 |
| TX / RX | GPIO 17 / GPIO 18 |

---



## 🛠️ Kurulum

```bash

# Arduino IDE → Library Manager
# "LoRa_E22" ara ve yükle
```

`gizli.h` dosyasında kendi adres ve kanal bilgilerinizi girin:

```cpp
#define Adres            1    // Bu cihazın adresi
#define GonderilecekAdres 2   // Hedef cihaz adresi
#define Kanal            20   // Ortak frekans kanalı
#define Netid            63   // Ortak ağ kimliği
```

---

## 📋 Gereksinimler

- **Donanım:** ESP32 S3 N16R8 PCB by Fixaj, INMP441 MEMS mikrofon, LoRa E22-900T22D
- **Framework:** Arduino + ESP-IDF (PlatformIO veya Arduino IDE)
- **Kütüphane:** `LoRa_E22` — [xreef](https://github.com/xreef/EByte_LoRa_E22_Series_Library)

---

##  📂 Dizin Yapısı

```bash

📦 Audio_Receiver
 ┣ 📂 1_TX_Sender_Node
 ┃ ┣ 📜 TX_Audio_Sender.ino  # I2S DMA Kayıt ve RF Fragmentation
 ┃ ┗ 📜 gizli.h              # LoRa Pin ve Adres Tanımlamaları
 ┣ 📂 2_RX_Receiver_Node
 ┃ ┗📜 RX_Audio_Receiver.ino # RF Reassembly, PLR Hesabı ve Timeout Yönetimi
 ┣ 📂 3_PC_Python_Script
 ┃ ┗ 📜 ses_alici.py         # Seri Port okuma ve PCM to WAV dönüşümü
 ┗ 📜 README.md
 
 ```
 
 👨‍💻 Geliştirici
 
Mehmet Yıldız | Embedded Systems Architect

[FIXAJ.com](https://fixaj.com/) 
---

## 📄 Lisans

Bu proje MIT lisansı ile dağıtılmaktadır.


