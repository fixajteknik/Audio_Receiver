# 📻 End-to-End RF Audio Streaming System (ESP32 & LoRa E22)

![ESP32](https://img.shields.io/badge/ESP32-S3-blue?style=flat&logo=espressif)
![LoRa](https://img.shields.io/badge/LoRa-E22--900T22D-orange?style=flat)
![Python](https://img.shields.io/badge/Python-3.8+-yellow?style=flat&logo=python)
![Architecture](https://img.shields.io/badge/Architecture-State_Machine-success?style=flat)

Bu proje, bir INMP441 MEMS mikrofon üzerinden alınan **16kHz, 16-bit** ham PCM ses verisini, dar bantlı (narrow-band) bir RF modülü olan **LoRa E22** üzerinden iletmek ve alıcı tarafta yeniden birleştirerek (Reassembly) bilgisayar ortamında `.wav` formatına dönüştürmek amacıyla geliştirilmiş endüstriyel bir Gömülü Sistem mimarisidir.

Sistem; eşzamanlı donanım yönetimi, veri parçalama (fragmentation), paket kayıp oranı (PLR) hesaplaması ve asenkron zaman aşımı (non-blocking timeout) gibi ileri seviye mühendislik konseptlerini barındırır.

---

## 🛠️ Sistem Mimarisi ve Mühendislik Kararları (Trade-offs)

Gerçek zamanlı (real-time) 256 kbps veri akışı gerektiren bir sesi, doğası gereği düşük bant genişliğine sahip LoRa üzerinden iletmek fiziksel bir darboğazdır (bottleneck). Bu darboğazı aşmak için sistem **"Half-Duplex Walkie-Talkie"** mimarisiyle tasarlanmış ve aşağıdaki mühendislik çözümleri uygulanmıştır:

### 1. I2S DMA ve Donanımsal Veri Kırpma (Hardware Bit-Shifting)
CPU'yu I/O işlemleriyle yormamak için Arduino'nun standart `analogRead()` fonksiyonları yerine **ESP-IDF I2S DMA** sürücüleri kullanılmıştır. INMP441 fiziksel olarak 32-bit veri üretir, ancak RF bant genişliğini korumak için donanımsal `slot_bit_width` ayarlarıyla veri doğrudan donanım seviyesinde 16-bit'e (MSB tutulup LSB atılarak) sıkıştırılmış ve RAM'e kopyalanmıştır.

### 2. Fragmentation (Parçalama) ve FIFO Optimizasyonu
32ms'lik tek bir ses çerçevesi (Frame) 512 örnekten, yani **1024 Byte**'tan oluşur. LoRa E22 modülünün donanımsal UART FIFO sınırı **240 Byte**'tır. 
Tampon taşmalarını (Overflow) önlemek için;
* **19 Byte** başlık (Header: Çerçeve No, Paket No, ID, Şifre)
* **210 Byte** ses yükü (Payload)
olmak üzere özel bir `#pragma pack(1)` Struct tasarlandı. Böylece 1024 Byte'lık dev blok, donanımı boğmadan **5 RF Paketine** bölünerek havaya basılır.

### 3. Reassembly ve Dinamik Offset Mantığı
Alıcı (RX) modül, paketlerin havadan hangi sırayla geldiğini önemsemez. Gelen paketin başlığındaki `paket_no` bilgisi okunarak, 1024 byte'lık RAM tamponundaki tam konumu `(paket_no * 210)` formülüyle hesaplanır ve veri `memcpy` ile doğrudan ilgili adrese kopyalanır.

### 4. Sequence Tracking (Sıra Takibi) ve Otonom PLR Hesabı
Paket kayıplarını (Packet Loss) ölçmek için geleneksel zamanlayıcılar (Timer) yerine **Sıra Takibi** kullanılmıştır. Alıcı, havadan yeni bir `cerceve_no` yakaladığında, önceki çerçevenin kapandığını anlar, eksik paketleri sayar ve **Packet Loss Ratio (PLR)** değerini anında % olarak hesaplayıp raporlar.

### 5. Asenkron Zaman Aşımı (Non-Blocking Timeout)
Sistemde paket veya çerçeve kayıplarının (Drop) yazılımı kilitlemesini (Deadlock) engellemek için `millis()` tabanlı 5 saniyelik asenkron bir Watchdog / Timeout mekanizması kurulmuştur. Süre dolduğunda sistem beklemeyi bırakır, kurtarabildiği (N/A içermeyen) verileri raporlar ve yeni çerçeveyi dinlemeye geçer.

---

## 🚀 Kurulum ve Çalıştırma

### 1. Donanım Bağlantıları (ESP32-S3 İçin)
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

### 2. Yazılımın Yüklenmesi
1. `gizli.h` dosyasındaki `Adres`, `Kanal` ve `GonderilecekAdres` parametrelerini cihazlarınıza göre ayarlayın.
2. `1_TX_Sender_Node` kodunu Gönderici cihaza yükleyin.
3. `2_RX_Receiver_Node` kodunu Alıcı cihaza yükleyin.

### 3. PC Üzerinden Ses Verisinin Dinlenmesi (.WAV)
Alıcı cihaz bilgisayara bağlandığında Arduino IDE Seri Monitörünü **kapatın**. Ardından Python scriptini çalıştırarak gelen ham PCM verisini `.wav` dosyasına dönüştürün:

```bash
# Gerekli kütüphaneyi kurun
pip install pyserial

# Dinleme betiğini başlatın (COM portunu kendi bilgisayarınıza göre düzenleyin)
python 3_PC_Python_Script/ses_alici.py

📂 Dizin Yapısı

📦 OLBS_RF_Audio_Project
 ┣ 📂 1_TX_Sender_Node
 ┃ ┣ 📜 TX_Audio_Sender.ino  # I2S DMA Kayıt ve RF Fragmentation
 ┃ ┗ 📜 gizli.h              # LoRa Pin ve Adres Tanımlamaları
 ┣ 📂 2_RX_Receiver_Node
 ┃ ┣ 📜 RX_Audio_Receiver.ino # RF Reassembly, PLR Hesabı ve Timeout Yönetimi
 ┃ ┗ 📜 gizli.h
 ┣ 📂 3_PC_Python_Script
 ┃ ┗ 📜 ses_alici.py         # Seri Port okuma ve PCM to WAV dönüşümü
 ┗ 📜 README.md
 
 
 👨‍💻 Geliştirici
Mehmet Yıldız | Embedded Systems Architect