#define e22900t22d    //e22 modülünü kullanıyorsanız burayı aktif edin

#ifdef e22900t22d
#include "LoRa_E22.h"
#define kutuphane LoRa_E22
#define Netid 63  //0--65000 arası bir değer girebilirsiniz. Diğer Modüllerle AYNI olmalı.
#else
#include "LoRa_E32.h"
#define kutuphane LoRa_E32
#endif

////////////////////////////////     KART TANIMLAMALARI    /////////////////////////////////////////////////////////////
#if CONFIG_IDF_TARGET_ESP32  // ESP 32
#include <HardwareSerial.h>
#define ESP32AYAR
#define M0 32  //3in1 PCB mizde pin 7
#define M1 33  //3in1 PCB mizde pin 6
#define RX 27  //3in1 PCB mizde pin RX
#define TX 35  //3in1 PCB mizde pin TX

HardwareSerial fixSerial(1);  //Serial biri seçiyoruz.
kutuphane FixajSerial(TX, RX, &fixSerial, UART_BPS_RATE_9600);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#elif CONFIG_IDF_TARGET_ESP32S3  // S3
#include <HardwareSerial.h>
#define M0 4                    //  3in1 PCB mizde pin 7
#define M1 6                    //  3in1 PCB mizde pin 6
#define RX 18                   //  esp32 s3 de Lora RX e bağlı
#define TX 17                   //  esp32 s3 de Lora TX e bağlı

HardwareSerial fixSerial(1);
kutuphane FixajSerial(TX, RX, &fixSerial, UART_BPS_RATE_9600);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#elif defined ARDUINO_AVR_NANO  // NANO
#include <SoftwareSerial.h>
#define M0 7
#define M1 6
#define RX 3
#define TX 4
SoftwareSerial fixSerial(RX, TX);  //PCB versiyon 4.3 den sonra bu şekilde olmalı
kutuphane FixajSerial(&fixSerial);
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//#error Hedef CONFIG_IDF_TARGET tanimlanmamis
#endif

#define Adres 1              //0--65000 arası bir değer girebilirsiniz. Diğer Modüllerden FARKLI olmalı
#define Kanal 20             //Frekans değeri Diğer Modüllerle AYNI olmalı.
#define GonderilecekAdres 2  //Mesajın gönderileceği LoRa nın adresi
