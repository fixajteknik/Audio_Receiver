/*********************************************************************************************************
** END-TO-END RF AUDIO STREAMING PROJECT
**
**--------------File Info-------------------------------------------------------------------------------
** File Name:            TX_Audio_Sender.ino
** Author:               Mehmet Yildiz - Embedded Systems Architect www.fixaj.com
** Mail:                 destek@fixaj.com
** Date:                 April 2026
** Version:              v1.0
** Description:          ESP32-S3 tabanli, INMP441 mikrofondan I2S DMA donanimi ile 32ms (16kHz, 16-bit) 
** ham PCM ses kaydi alan ve bu veriyi LoRa E22 modulunun 240 byte FIFO donanim 
** sinirina uygun olarak (Fragmentation) parcalayarak RF uzerinden ileten yazilim.
** Walkie-Talkie mimarisine yonelik State-Machine (Durum Makinesi) kullanilmistir.
**
** Dependency:           LoRa_E22.h / LoRa_E32.h, driver/i2s_std.h
*********************************************************************************************************/

#include "gizli.h"
#include "driver/i2s_std.h"

/*======================================================================================================
** CONSTANTS & MACROS
========================================================================================================*/
// --- I2S PIN YAPILANDIRMASI (ESP32-S3 GUVENLI PINLERI) ---
#define I2S_WS 48   // Word Select (LRCK)
#define I2S_SCK 2  // Serial Clock (BCLK)
#define I2S_SD 38   // Serial Data (DIN)

// --- SES KAYIT PARAMETRELERI ---
#define SAMPLE_RATE 16000                                         // 16 kHz Ornekleme hizi
#define FRAME_TIME_MS 32                                          // 1 Cerceve uzunlugu (Milisaniye)
#define SAMPLES_PER_FRAME ((SAMPLE_RATE * FRAME_TIME_MS) / 1000)  // 32ms icin 512 ornek
#define BYTES_PER_FRAME (SAMPLES_PER_FRAME * sizeof(int16_t))     // Toplam 1024 Byte ham veri

// --- RF PAKETLEME (FRAGMENTATION) PARAMETRELERI ---
// LoRa E22 240 byte donanim FIFO sinirini asmamak icin payload 210 byte ile sinirlandirildi.
#define MAX_AUDIO_PAYLOAD_SIZE 210

/*======================================================================================================
** DATA STRUCTURES & STATE MACHINE
========================================================================================================*/
/*********************************************************************************************************
** Enum:      app_state_t
** Brief:     Sistem durum makinesi (State Machine) asamalari. Spagetti kodu onler ve akisi yonetir.
*********************************************************************************************************/
typedef enum {
  STATE_IDLE,         // Bekleme durumu, donanim musait
  STATE_RECORDING,    // I2S donanimi DMA uzerinden kayit yapiyor
  STATE_FRAME_READY,  // DMA kesmesi (ISR) tetiklendi, tampon dolu
  STATE_PROCESSING,   // Verinin parcalanmasi (Parse) ve RF iletimi
  STATE_ERROR         // Donanim hatasi veya Tampon tasmasi (Overflow)
} app_state_t;

/*********************************************************************************************************
** Struct:    AudioPacket
** Brief:     RF uzerinden iletilecek ses paketinin mimarisi. 
** Note:      #pragma pack(1) kullanilarak derleyicinin struct icine bosluk (padding) atamasi engellenmis,
** veri butunlugu (229 Byte) garanti altina alinmistir.
*********************************************************************************************************/
#pragma pack(push, 1)
struct AudioPacket {
  uint32_t cerceve_no;    // 4 Byte: Ait oldugu 32ms'lik blogun kimligi
  uint16_t kaynak_id;     // 2 Byte: Gonderici cihazin ID'si
  uint8_t veri_uzunlugu;  // 1 Byte: Bu paketteki gecerli ses byte'i uzunlugu
  uint8_t paket_no;       // 1 Byte: Cerceve icindeki sira numarasi (0, 1, 2...)
  uint8_t toplam_paket;   // 1 Byte: Cercevenin bolundugu toplam paket sayisi
  char sifre[10];         // 10 Byte: Uygulama katmani guvenlik sifresi

  // 105 adet 16-bit PCM ornek = 210 Byte. Toplam Struct = 229 Byte
  int16_t ses_verisi[MAX_AUDIO_PAYLOAD_SIZE / 2];
};
#pragma pack(pop)

/*======================================================================================================
** GLOBAL VARIABLES
========================================================================================================*/
static volatile app_state_t app_state = STATE_IDLE;
static int16_t pcm_buffer[SAMPLES_PER_FRAME];  // 1024 Byte'lik ana ses tamponu
static i2s_chan_handle_t rx_chan = NULL;       // I2S donanim kanali tutucusu

/*======================================================================================================
** HARDWARE INTERRUPT SERVICE ROUTINES (ISR)
========================================================================================================*/
/*********************************************************************************************************
** Function Name: on_recv_done
** Description:   I2S DMA tamponu tamamen doldugunda donanim tarafindan tetiklenen kesme fonksiyonu.
** IRAM_ATTR etiketi ile hizli erisim icin RAM uzerinde calisir.
** Input Params:  handle: I2S kanal referansi | event: Olay verisi | user_ctx: Kullanici baglami
** Return Value:  bool: Yuksek oncelikli gorev uyandirma gereksinimi (false)
*********************************************************************************************************/
static bool IRAM_ATTR on_recv_done(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
  if (app_state == STATE_RECORDING) {
    app_state = STATE_FRAME_READY;  // Ana donguye verinin hazir oldugunu bildir
  }
  return false;
}

/*********************************************************************************************************
** Function Name: on_recv_overflow
** Description:   Islemci RF iletimi yaparken DMA tamponlari yetersiz kalip tasarsa tetiklenen hata kesmesi.
** Input Params:  handle: I2S kanal referansi | event: Olay verisi | user_ctx: Kullanici baglami
** Return Value:  bool: Yuksek oncelikli gorev uyandirma gereksinimi (false)
*********************************************************************************************************/
static bool IRAM_ATTR on_recv_overflow(i2s_chan_handle_t handle, i2s_event_data_t *event, void *user_ctx) {
  if (app_state == STATE_RECORDING) {
    app_state = STATE_ERROR;  // Veri guvenilirligi kayboldu, sistemi durdur
  }
  return false;
}

/*======================================================================================================
** INITIALIZATION FUNCTIONS
========================================================================================================*/
/*********************************************************************************************************
** Function Name: i2s_init
** Description:   ESP32-S3 I2S donanimini Master ve Standart RX modunda baslatir. Donanimsal 32-bit'ten 
** 16-bit'e kirpma (Hardware Bit-Shifting) filtresini aktif ederek CPU yukunu sifirlar.
** Return Value:  esp_err_t: Basarili ise ESP_OK, aksi halde hata kodu.
*********************************************************************************************************/
static esp_err_t i2s_init() {
  // 1. Kanal ve DMA Yapilandirmasi
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 2;                   // Sadece 1 frame icin 2 descriptor yeterli
  chan_cfg.dma_frame_num = SAMPLES_PER_FRAME;  // 512 ornek kapasitesi
  chan_cfg.auto_clear = false;

  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_chan));

  // 2. Standart Mod ve Donanim Filtresi
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK,
      .ws = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)I2S_SD,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };

  // Sinyal hatti 32-bit, DMA'ya yansiyan veri 16-bit
  std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_chan, &std_cfg));

  // 3. Olay (Callback) Yoneticilerini Kaydet
  i2s_event_callbacks_t cbs = {
    .on_recv = on_recv_done,
    .on_recv_q_ovf = on_recv_overflow,
    .on_sent = NULL,
    .on_send_q_ovf = NULL,
  };
  ESP_ERROR_CHECK(i2s_channel_register_event_callback(rx_chan, &cbs, NULL));

  return ESP_OK;
}

/*======================================================================================================
** MAIN SYSTEM SETUP
========================================================================================================*/
void setup() {
  pinMode(M0, OUTPUT);
  pinMode(M1, OUTPUT);
  Serial.begin(115200);
  delay(2000);

  Serial.println("[SETUP] I2S Donanimi Baslatiliyor...");
  if (i2s_init() != ESP_OK) {
    Serial.println("[SETUP] HATA: I2S baslatilamadi!");
    app_state = STATE_ERROR;
    return;
  }

  // DMA dinlemeyi aktif et
  ESP_ERROR_CHECK(i2s_channel_enable(rx_chan));
  Serial.printf("[SETUP] Sistem Hazir. %d Hz | %d ms | %d ornek | %d byte\n",
                SAMPLE_RATE, FRAME_TIME_MS, SAMPLES_PER_FRAME, BYTES_PER_FRAME);

  // RF (LoRa) Seri Port Baslatma
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

  app_state = STATE_IDLE;  // Durum makinesini baslat
}

/*======================================================================================================
** MAIN SYSTEM LOOP (STATE MACHINE)
========================================================================================================*/
void loop() {
  switch (app_state) {

    // ── IDLE DURUMU: KAYDI BASLAT ──────────────────────────────────
    case STATE_IDLE:
      {
        Serial.println("[REC] Kayit asamasina geciliyor...");
        app_state = STATE_RECORDING;

        size_t bytes_read = 0;

        // DMA okumasini tetikle ve donanimin veriyi RAM'e tasiyabilmesi icin zaman ver
        esp_err_t res = i2s_channel_read(
          rx_chan,
          pcm_buffer,
          BYTES_PER_FRAME,
          &bytes_read,
          pdMS_TO_TICKS(FRAME_TIME_MS + 20)  // Gecikme toleransi
        );

        if (res == ESP_ERR_TIMEOUT) {
          Serial.println("[REC] HATA: Zaman asimi (I2S Saat Sinyali Yok)");
          app_state = STATE_ERROR;
          break;
        }
        if (res != ESP_OK) {
          Serial.printf("[REC] HATA: %s\n", esp_err_to_name(res));
          app_state = STATE_ERROR;
          break;
        }
        if (bytes_read != BYTES_PER_FRAME) {
          Serial.printf("[REC] HATA: Eksik veri okumasi gerceklesti (%u/%u byte)\n", (unsigned)bytes_read, (unsigned)BYTES_PER_FRAME);
          app_state = STATE_ERROR;
          break;
        }

        // Kesme tarafindan hata basilmadiysa islemi bir sonraki asama olan hazirliga aktar
        if (app_state != STATE_ERROR) {
          app_state = STATE_FRAME_READY;
        }
        break;
      }

    // ── FRAME READY DURUMU: TAMPON DOLDU ───────────────────────────
    case STATE_FRAME_READY:
      {
        // sürekli gönderim varsa bura böyle kalır.
        // Bir daha kayıt yapmayacağız, kanalı kapat
        // i2s_channel_disable(rx_chan);
        // i2s_del_channel(rx_chan);
        // rx_chan = NULL;

        Serial.printf("[REC] Ses blogu (%d ornek) RAM'e kopyalandi.\n", SAMPLES_PER_FRAME);
        Serial.printf("[REC] Ozet Ilk 8 Ornek: ");
        for (int i = 0; i < 8; i++) {
          Serial.printf("%d ", pcm_buffer[i]);
        }
        Serial.println();

        app_state = STATE_PROCESSING;  // Parcalama asamasina gec
        break;
      }

    // ── PROCESSING DURUMU: RF PARCALAMA (FRAGMENTATION) ────────────
    case STATE_PROCESSING:
      {
        static uint32_t genel_cerceve_sayaci = 0;
        genel_cerceve_sayaci++;

        int gonderilen_toplam_byte = 0;
        uint8_t toplam_paket_sayisi = (BYTES_PER_FRAME + MAX_AUDIO_PAYLOAD_SIZE - 1) / MAX_AUDIO_PAYLOAD_SIZE;
        uint8_t mevcut_paket_indeksi = 0;

        Serial.printf("[TX] %d Numarali Cerceve, %d bagimsiz pakete bolunerek iletiliyor...\n", genel_cerceve_sayaci, toplam_paket_sayisi);

        // Tum 1024 byte tukenene kadar yigma dizisini dondur
        while (gonderilen_toplam_byte < BYTES_PER_FRAME) {

          AudioPacket tx_paket;
          memset(&tx_paket, 0, sizeof(AudioPacket));

          // 1. Baslik (Header) Verilerinin Doldurulmasi
          tx_paket.cerceve_no = genel_cerceve_sayaci;
          tx_paket.kaynak_id = 1453;
          tx_paket.paket_no = mevcut_paket_indeksi;
          tx_paket.toplam_paket = toplam_paket_sayisi;

          // Iki katmanli guvenlik icin String Kopyalama (Buffer tasma korumali)
          strncpy(tx_paket.sifre, "Fixaj.com", sizeof(tx_paket.sifre) - 1);
          tx_paket.sifre[sizeof(tx_paket.sifre) - 1] = '\0';

          // 2. Fragmentasyon Yuk Hesaplamasi
          int kalan_byte = BYTES_PER_FRAME - gonderilen_toplam_byte;
          int bu_pakete_sigacak_byte = (kalan_byte > MAX_AUDIO_PAYLOAD_SIZE) ? MAX_AUDIO_PAYLOAD_SIZE : kalan_byte;
          tx_paket.veri_uzunlugu = bu_pakete_sigacak_byte;

          // 3. Dinamik Pointer Aritmetigi ile Veri Tasimasi
          memcpy(tx_paket.ses_verisi, (uint8_t *)pcm_buffer + gonderilen_toplam_byte, bu_pakete_sigacak_byte);

          // 4. LoRa Modulu Uzerinden RF Iletimi
          ResponseStatus rs = FixajSerial.sendFixedMessage(
            highByte(GonderilecekAdres),
            lowByte(GonderilecekAdres),
            Kanal,
            &tx_paket,
            sizeof(AudioPacket));

          // Iletim Sonucu Degerlendirmesi (E22_SUCCESS = 1)
          if (rs.code != 1) {
            Serial.printf("[TX HATA] Paket %d UART hattina yazilamadi! Hata Kodu: %d\n", mevcut_paket_indeksi, rs.code);
          } else {
            Serial.printf("[TX OK] Paket %d islendi. (Yuk: %d byte)\n", mevcut_paket_indeksi, bu_pakete_sigacak_byte);
          }

          gonderilen_toplam_byte += bu_pakete_sigacak_byte;
          mevcut_paket_indeksi++;

          // LoRa Modulunun isinmasini onlemek ve RF cakismalarindan kacinarak Air-Data-Rate'e uyum saglamak icin
          delay(2000);
        }

        Serial.println("[TX] Cerceve iletim dongusu basariyla tamamlandi.");
        delay(5000);
        // Surekli dongu modu icin STATE_IDLE'a dondur, tek atimlik test icin yoruma al
        app_state = STATE_IDLE;

        // Tek seferlik gonderim (Single-Shot Debug) icin alttaki blogu aktif et:
        // while (true) { delay(1000); }
        break;
      }

    // ── ERROR DURUMU: SISTEM DURDURMA ──────────────────────────────
    case STATE_ERROR:
      {
        Serial.println("[ERR] Kritik sistem hatasi! Mimari guvenlice kilitleniyor.");
        if (rx_chan != NULL) {
          i2s_channel_disable(rx_chan);
          i2s_del_channel(rx_chan);
          rx_chan = NULL;
        }
        while (true) { delay(1000); }
        break;
      }

    default:
      break;
  }
}

/*======================================================================================================
** LORA MODULE CONFIGURATION FUNCTIONS
========================================================================================================*/
#ifdef e22900t22d
/*********************************************************************************************************
** Function Name: LoraE22Ayarlar
** Description:   LoRa E22 serisi moduller icin dinamik konfigürasyon (Kanal, Adres, Hiz vb.) yurutur.
*********************************************************************************************************/
void LoraE22Ayarlar() {
  digitalWrite(M0, LOW);
  digitalWrite(M1, HIGH);

  ResponseStructContainer c;
  c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration *)c.data;

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
** Description:   LoRa E32 serisi moduller icin dinamik konfigürasyon yurutur.
*********************************************************************************************************/
void LoraE32Ayarlar() {
  digitalWrite(M0, HIGH);
  digitalWrite(M1, HIGH);

  ResponseStructContainer c;
  c = FixajSerial.getConfiguration();
  Configuration configuration = *(Configuration *)c.data;

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