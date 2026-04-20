/*********************************************************************************************************
** END-TO-END RF AUDIO STREAMING PROJECT
**
**--------------File Info-------------------------------------------------------------------------------
** File Name:            RX_Audio_Receiver.ino
** Author:               Mehmet Yildiz - Embedded Systems Architect www.fixaj.com
** Mail:                 destek@fixaj.com
** Date:                 April 2026
** Version:              v1.0
** Description:          ESP32 tabanli, LoRa E22 modulu uzerinden parcalanmis (fragmented) olarak gelen 
** 32ms'lik (16kHz, 16-bit) ham PCM ses paketlerini alan, sira numaralarina (Sequence Tracking) 
** gore dinamik offset ile birlestiren (Reassembly) ve Paket Kayip Oranini (PLR) asenkron 
** zaman asimi (Non-blocking Timeout) mantigiyla hesaplayan Alici (Receiver) yazilimi.
**
** Dependency:           LoRa_E22.h / LoRa_E32.h
*********************************************************************************************************/

/*
** MODUL SECIMI VE KUTUPHANE KONTROLU
** https://shop.fixaj.com/
*/
//#define e32433t20d        // E32 modulu icin aktif edin
#define e22900t22d          // E22 modulu icin aktif edin

#ifdef e22900t22d
#include "LoRa_E22.h"
#define kutuphane LoRa_E22
#define Netid 63            // Ag Kimligi (TX ile ayni olmali)
#else
#include "LoRa_E32.h"
#define kutuphane LoRa_E32
#endif

/*======================================================================================================
** HARDWARE PIN DEFINITIONS (CROSS-PLATFORM SUPPORT)
========================================================================================================*/
#if CONFIG_IDF_TARGET_ESP32      // --- ESP32 KLASIK ---
#include <HardwareSerial.h>
#define ESP32AYAR
#define M0 32  
#define M1 33  
#define RX 27  
#define TX 35  
HardwareSerial fixSerial(1);  
kutuphane FixajSerial(TX, RX, &fixSerial, UART_BPS_RATE_9600);

#elif CONFIG_IDF_TARGET_ESP32S3  // --- ESP32-S3 ---
#include <HardwareSerial.h>
#define M0 4                    
#define M1 6                    
#define RX 18                   
#define TX 17                   
HardwareSerial fixSerial(1);
kutuphane FixajSerial(TX, RX, &fixSerial, UART_BPS_RATE_9600);

#elif defined ARDUINO_AVR_NANO   // --- ARDUINO NANO ---
#include <SoftwareSerial.h>
#define M0 7
#define M1 6
#define RX 3
#define TX 4
SoftwareSerial fixSerial(RX, TX); 
kutuphane FixajSerial(&fixSerial);
#endif

/*======================================================================================================
** CONSTANTS & MACROS
========================================================================================================*/
// --- LORA ADRESLEME PARAMETRELERI ---
#define Adres 2              // Alicinin kendi adresi (TX'ten farkli)
#define Kanal 20             // RF Frekans Kanali (TX ile ayni)
#define GonderilecekAdres 1  // TX'in adresi (Cift yonlu iletisim ihtimaline karsi)

// --- SES VE PARCALAMA (FRAGMENTATION) PARAMETRELERI ---
#define SAMPLES_PER_FRAME 512   // 32ms icin 512 ornek (Sample)
#define MAX_AUDIO_PAYLOAD_SIZE 210 // Donanimsal FIFO tasmamasi icin maksimum ses yuku

/*======================================================================================================
** DATA STRUCTURES
========================================================================================================*/
/*********************************************************************************************************
** Struct:    AudioPacket
** Brief:     Havadan gelen ses parcalarini (Fragment) karsilayan veri mimarisi. 
** Note:      #pragma pack(1) kullanilarak bellek hizalamasi (memory alignment) engellenmistir.
*********************************************************************************************************/
#pragma pack(push, 1)
struct AudioPacket {
  uint32_t cerceve_no;      // 4 Byte: Ait oldugu 32ms'lik blogun kimligi
  uint16_t kaynak_id;       // 2 Byte: Gonderici cihazin ID'si
  uint8_t  veri_uzunlugu;   // 1 Byte: Bu paketteki gecerli ses byte'i uzunlugu
  uint8_t  paket_no;        // 1 Byte: Cerceve icindeki sira numarasi (0, 1, 2...)
  uint8_t  toplam_paket;    // 1 Byte: Cercevenin bolundugu toplam paket sayisi
  char     sifre[10];       // 10 Byte: Uygulama katmani guvenlik sifresi
  int16_t  ses_verisi[MAX_AUDIO_PAYLOAD_SIZE / 2]; // 210 Byte Ses Yuku
};
#pragma pack(pop)

/*======================================================================================================
** GLOBAL VARIABLES
========================================================================================================*/
// Reassembly (Yeniden Birlestirme) isleminde kullanilacak ana tampon
int16_t pcm_buffer[SAMPLES_PER_FRAME];

// Cerceve Takip (Sequence Tracking) ve PLR Hesaplama Degiskenleri
uint32_t aktif_cerceve_no = 0;
bool     paket_alindi_mi[10]; 
int      alinan_paket_sayisi = 0;
int      beklenen_paket_sayisi = 5;

/*======================================================================================================
** MAIN SYSTEM SETUP
========================================================================================================*/
void setup() {
  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);

  Serial.begin(115200);
  delay(2000);
  FixajSerial.begin();

#ifdef e22900t22d
  LoraE22Ayarlar();
  Serial.print("[SETUP] E22 900t22d ");
#else
  LoraE32Ayarlar();
  Serial.print("[SETUP] E32 433t20d ");
#endif

  digitalWrite(M0, LOW);
  digitalWrite(M1, LOW);

  delay(500);
  Serial.println("Sistemi Basariyla Kuruldu. Fixaj.com - Ses Alici (RX) Modu Aktif.");
}

/*======================================================================================================
** MAIN SYSTEM LOOP (REASSEMBLY & SEQUENCE TRACKING)
========================================================================================================*/
void loop() {
  // --- ZAMAN AŞIMI (TIMEOUT) İÇİN GEREKLİ STATİK DEĞİŞKENLER ---
  static unsigned long son_paket_zamani = 0;
  static bool cerceve_aktif = false;

  /*****************************************************************************************************
  ** Lambda Function: raporu_bas
  ** Brief:           Dongu icinde tekrari (DRY) onlemek amaciyla olusturulmus yerel fonksiyon.
  ** Eksik paketleri (PLR) hesaplar ve asil PCM tamponunu matris formatinda ekrana basar.
  *****************************************************************************************************/
  auto raporu_bas = [&]() {
    float plr = 100.0 * (beklenen_paket_sayisi - alinan_paket_sayisi) / (float)beklenen_paket_sayisi;

    Serial.println("\n===================================================");
    if (alinan_paket_sayisi == beklenen_paket_sayisi) {
      Serial.printf("[BASARILI] Cerceve %d Eksiksiz Tamamlandi!\n", aktif_cerceve_no);
    } else {
      Serial.printf("[TIMEOUT] Cerceve %d Icin 5 Saniye Beklendi, Eksikler Var!\n", aktif_cerceve_no);
    }

    Serial.printf("        Alinan Paket     : %d/%d\n", alinan_paket_sayisi, beklenen_paket_sayisi);
    Serial.printf("        PLR (Kayip Orani): %%%.2f\n", plr);

    // Kayip Paket Analizi
    if (plr > 0) {
      Serial.print("        Kayip Paket No   : ");
      for (int p = 0; p < beklenen_paket_sayisi; p++) {
        if (!paket_alindi_mi[p]) {
          Serial.print(p);
          Serial.print(" ");
        }
      }
      Serial.println("\n        [UYARI] Eksik paketler seste kesintiye yol acabilir!");
    }
    Serial.println("===================================================");

    // Birlestirilmis Ham Ses Verisinin (Reassembled PCM) Ekrana Basilmasi
    Serial.println("[SES VERISI] pcm_buffer (512 Ornek):");
    
    for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
      // Offset Mantigi: Ses orneginin hangi pakete ait oldugunu bul (Gorsellestirme Icin)
      int paket_index = (i * sizeof(int16_t)) / MAX_AUDIO_PAYLOAD_SIZE;

      // Eger bu ornek havada kaybolan bir pakete aitse 'N/A' yazdir
      if (paket_index < beklenen_paket_sayisi && !paket_alindi_mi[paket_index]) {
        Serial.print(" N/A");
      } else {
        Serial.printf("%4d", pcm_buffer[i]);
      }

      if (i < SAMPLES_PER_FRAME - 1) {
        Serial.print(", ");
      }

      // Matris gorunumu icin her 16 ornekte bir alt satira gec
      if ((i + 1) % 16 == 0) {
        Serial.println();
      }
    }
    Serial.println("\n===================================================\n");
  };

  // =========================================================================
  // 1. ASENKRON ZAMAN AŞIMI (NON-BLOCKING TIMEOUT) YONETIMI
  // Eger cerceve aciksa ve son paketin uzerinden 5 saniye gectiyse sistemi kurtar.
  // =========================================================================
  if (cerceve_aktif && (millis() - son_paket_zamani > 5000)) {
    raporu_bas();
    cerceve_aktif = false; 
  }

  // =========================================================================
  // 2. RF VERI ALIMI VE YENIDEN BIRLESTIRME (REASSEMBLY)
  // =========================================================================
  while (FixajSerial.available() > 1) {
    ResponseStructContainer rsc = FixajSerial.receiveMessageRSSI(sizeof(AudioPacket));
    struct AudioPacket gelen_paket = *(AudioPacket*)rsc.data;
    rsc.close();
    
    // Application Layer Security (Sifre Kontrolu)
    if (strcmp(gelen_paket.sifre, "Fixaj.com") == 0) {

      // --- A) YENI CERCEVE (FRAME) TESPITI ---
      if (gelen_paket.cerceve_no != aktif_cerceve_no) {

        // Eger onceki cerceve hala aciksa (timeout olmadan yeni frame geldiyse) raporu kapat
        if (cerceve_aktif && aktif_cerceve_no != 0) {
          raporu_bas();
        }

        // Yeni cerceve icin metadata'yi kaydet ve sistemi sifirla
        aktif_cerceve_no = gelen_paket.cerceve_no;
        beklenen_paket_sayisi = gelen_paket.toplam_paket;
        alinan_paket_sayisi = 0;
        memset(paket_alindi_mi, 0, sizeof(paket_alindi_mi));
        memset(pcm_buffer, 0, sizeof(pcm_buffer));

        cerceve_aktif = true;
        son_paket_zamani = millis(); 
      }

      // --- B) PAKET KABULU VE BELLEGE YERLESTIRME ---
      if (gelen_paket.cerceve_no == aktif_cerceve_no) {

        son_paket_zamani = millis(); // Timeout sayacini yenile
        cerceve_aktif = true;

        // Eger paket gecerliyse ve onceden islenmediyse (Duplicate Protection)
        if (gelen_paket.paket_no < beklenen_paket_sayisi && !paket_alindi_mi[gelen_paket.paket_no]) {
          
          paket_alindi_mi[gelen_paket.paket_no] = true;
          alinan_paket_sayisi++;

          // Dinamik Offset Hesaplamasi: Paketin 1024 byte'lik tamponda nereye yerlesecegini bul
          int offset_byte = gelen_paket.paket_no * MAX_AUDIO_PAYLOAD_SIZE;
          memcpy((uint8_t*)pcm_buffer + offset_byte, gelen_paket.ses_verisi, gelen_paket.veri_uzunlugu);

          Serial.printf("[RX] Cerceve:%d | Paket:%d/%d | Boyut:%d | RSSI: %d dBm\n",
                        gelen_paket.cerceve_no, gelen_paket.paket_no,
                        gelen_paket.toplam_paket - 1, gelen_paket.veri_uzunlugu, rsc.rssi);

          // EGER BEKLENEN TUM PAKETLER GELDİYSE TIMEOUT BEKLEMEDEN ANINDA RAPORLA
          if (alinan_paket_sayisi == beklenen_paket_sayisi) {
            delay(50); // Seri porta nefes payi
            raporu_bas();
            cerceve_aktif = false; // Islem bitti, timeout'u iptal et
          }
        }
      }
    } else {
      Serial.println("[RX HATA] Yanlis Sifre! Yabanci paket reddedildi.");
    }
  }
}

/*======================================================================================================
** LORA MODULE CONFIGURATION FUNCTIONS
========================================================================================================*/
#ifdef e22900t22d
/*********************************************************************************************************
** Function Name: LoraE22Ayarlar
** Description:   LoRa E22 serisi moduller icin dinamik konfigürasyon.
*********************************************************************************************************/
void LoraE22Ayarlar() {
  digitalWrite(M0, LOW);
  digitalWrite(M1, HIGH);

  ResponseStructContainer c;
  c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration*)c.data;

  // Temel Ag Ayarlari
  configuration.ADDL = lowByte(Adres);
  configuration.ADDH = highByte(Adres);
  configuration.NETID = Netid;
  configuration.CHAN = Kanal;

  // Veri ve Guc Ayarlari
  configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;  
  configuration.OPTION.subPacketSetting = SPS_240_00;     
  configuration.OPTION.transmissionPower = POWER_22;

  // Gelismis RF Kontrolleri
  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.uartParity = MODE_00_8N1;
  configuration.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;
  configuration.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
  configuration.TRANSMISSION_MODE.WORTransceiverControl = WOR_RECEIVER;
  configuration.TRANSMISSION_MODE.enableRSSI = RSSI_ENABLED;
  configuration.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;
  configuration.TRANSMISSION_MODE.enableRepeater = REPEATER_DISABLED;
  configuration.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;

  ResponseStatus rs = FixajSerial.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  Serial.println(rs.getResponseDescription());
  Serial.println(rs.code);
  c.close();
}

#else
/*********************************************************************************************************
** Function Name: LoraE32Ayarlar
** Description:   LoRa E32 serisi moduller icin dinamik konfigürasyon.
*********************************************************************************************************/
void LoraE32Ayarlar() {
  digitalWrite(M0, HIGH);
  digitalWrite(M1, HIGH);

  ResponseStructContainer c;
  c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration*)c.data;

  configuration.ADDL = lowByte(Adres);
  configuration.ADDH = highByte(Adres);
  configuration.CHAN = Kanal;

  configuration.SPED.airDataRate = AIR_DATA_RATE_010_24;
  configuration.OPTION.transmissionPower = POWER_20;

  configuration.SPED.uartBaudRate = UART_BPS_9600;
  configuration.SPED.uartParity = MODE_00_8N1;
  configuration.OPTION.fec = FEC_0_OFF;
  configuration.OPTION.fixedTransmission = FT_FIXED_TRANSMISSION;
  configuration.OPTION.wirelessWakeupTime = WAKE_UP_250;
  configuration.OPTION.ioDriveMode = IO_D_MODE_PUSH_PULLS_PULL_UPS;

  ResponseStatus rs = FixajSerial.setConfiguration(configuration, WRITE_CFG_PWR_DWN_SAVE);
  Serial.println(rs.getResponseDescription());
  Serial.println(rs.code);
  c.close();
}
#endif