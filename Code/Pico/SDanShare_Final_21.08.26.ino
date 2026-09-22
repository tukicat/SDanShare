// -------------------------------------------------------------
//  SD + USB MSC Dual‑Host System (Optimised for Fast Enumeration)
//  Scott’s Pico/Nano SD Multiplexer – Full Updated Version
// -------------------------------------------------------------

// TinyUSB MSC performance tuning
#define CFG_TUD_MSC_BUFSIZE     512
#define CFG_TUD_MSC_EP_BUFSIZE  512

#include <SdFat.h>
#include <Adafruit_TinyUSB.h>

// -----------------------------
// Pin assignments (SPI0)
// -----------------------------
#define SD_CS_PIN   17   // GP17
#define SD_SCK_PIN  18   // GP18
#define SD_MOSI_PIN 19   // GP19
#define SD_MISO_PIN 16   // GP16

#define LED_PICO_PIN    12   // Pico-mode LED
#define LED_OTHER_PIN   13   // Nano-mode LED

// SN74LVC245 OE control (active LOW)
#define OE_PIN 21       // GPIO21 → OE̅

// USB VBUS sense (HIGH when USB cable plugged in)
#define VBUS_PIN 24     // GPIO24 → internal VBUS sense

// Manual mode toggle button
#define MODE_BUTTON_PIN 22

// -----------------------------
// SD + MSC objects
// -----------------------------
SdSpiCard card;

// ⭐ Faster SPI for quicker enumeration
SdSpiConfig sdConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(25));

Adafruit_USBD_MSC usb_msc;

const uint32_t block_size = 512;
uint32_t block_count = 0;

bool picoMode = false;
bool lastUsbPresent = false;

// -----------------------------
// SD activity tracking
// -----------------------------
volatile uint32_t lastActivity = 0;

// -----------------------------
// Tri‑state helpers
// -----------------------------
void enablePicoSPI()
{
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  pinMode(SD_MOSI_PIN, OUTPUT);
  pinMode(SD_SCK_PIN, OUTPUT);
  pinMode(SD_MISO_PIN, INPUT);

  SPI.setRX(SD_MISO_PIN);
  SPI.setTX(SD_MOSI_PIN);
  SPI.setSCK(SD_SCK_PIN);
  SPI.begin();
}

void triStatePicoSPI()
{
  pinMode(SD_CS_PIN, INPUT);
  pinMode(SD_MOSI_PIN, INPUT);
  pinMode(SD_SCK_PIN, INPUT);
  pinMode(SD_MISO_PIN, INPUT);
}

// -----------------------------
// MSC callbacks
// -----------------------------
int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize)
{
  lastActivity = millis();
  uint32_t blocks = bufsize / block_size;
  if (!card.readSectors(lba, (uint8_t*)buffer, blocks)) return -1;
  return bufsize;
}

int32_t msc_write_cb(uint32_t lba, uint8_t* buffer, uint32_t bufsize)
{
  lastActivity = millis();
  uint32_t blocks = bufsize / block_size;
  if (!card.writeSectors(lba, buffer, blocks)) return -1;

  // ⭐ Only sync on flush, not every write
  return bufsize;
}

void msc_flush_cb(void)
{
  card.syncDevice();
}

// -----------------------------
// Mode apply helpers
// -----------------------------
void setPicoMode()
{
  picoMode = true;

  digitalWrite(OE_PIN, HIGH);   // disconnect Nano
  digitalWrite(LED_PICO_PIN, HIGH);
  digitalWrite(LED_OTHER_PIN, LOW);

  enablePicoSPI();

  // ⭐ SD warm-up: ensures instant response to Windows READ(10)
  uint8_t temp[512];
  card.readSectors(0, temp, 1);

  usb_msc.setUnitReady(true);

  // ⭐ Faster detach/attach
  TinyUSBDevice.detach();
  delay(10);
  TinyUSBDevice.attach();
}

void setNanoMode()
{
  picoMode = false;

  usb_msc.setUnitReady(false);

  SPI.end();
  delay(5);

  triStatePicoSPI();

  digitalWrite(OE_PIN, LOW);    // reconnect Nano
  digitalWrite(LED_PICO_PIN, LOW);
  digitalWrite(LED_OTHER_PIN, HIGH);
}

// -----------------------------
// SETUP
// -----------------------------
void setup()
{
  pinMode(LED_PICO_PIN, OUTPUT);
  pinMode(LED_OTHER_PIN, OUTPUT);
  pinMode(OE_PIN, OUTPUT);
  pinMode(VBUS_PIN, INPUT);
  pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(OE_PIN, HIGH);
  enablePicoSPI();

  // ⭐ SD initialisation only ONCE
  if (!card.begin(sdConfig)) {
    while (1) {
      digitalWrite(LED_PICO_PIN, HIGH);
      delay(200);
      digitalWrite(LED_PICO_PIN, LOW);
      delay(200);
    }
  }

  block_count = card.sectorCount();

  usb_msc.setID("Raspberry Pi", "Pico SD Reader", "1.0");
  usb_msc.setReadWriteCallback(msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setCapacity(block_count - 1, block_size);
  usb_msc.begin();

  setNanoMode();
}

// -----------------------------
// LOOP
// -----------------------------
void loop()
{
  tud_task();

  bool usbPresent = digitalRead(VBUS_PIN);

  // ⭐ Only detach/attach when USB cable is plugged in
  if (usbPresent && !lastUsbPresent) {
    setPicoMode();
  } else if (!usbPresent && lastUsbPresent) {
    setNanoMode();
  }

  lastUsbPresent = usbPresent;

  // -----------------------------
  // Reliable toggle button
  // -----------------------------
  static bool buttonPressed = false;
  bool reading = digitalRead(MODE_BUTTON_PIN);

  if (!reading && !buttonPressed && usbPresent) {
    buttonPressed = true;
    if (picoMode) setNanoMode();
    else setPicoMode();
  }

  if (reading && buttonPressed) {
    buttonPressed = false;
  }

  // -----------------------------
  // ⭐ Pulsing LED logic
  // -----------------------------
  bool busy = (millis() - lastActivity < 150);

  static int brightness = 0;
  static int direction = 5;
  static unsigned long lastUpdate = 0;

  int activeLed = picoMode ? LED_PICO_PIN : LED_OTHER_PIN;
  int inactiveLed = picoMode ? LED_OTHER_PIN : LED_PICO_PIN;

  analogWrite(inactiveLed, 0);

  if (busy) {
    if (millis() - lastUpdate > 10) {
      brightness += direction;
      if (brightness >= 255 || brightness <= 0)
        direction = -direction;
      analogWrite(activeLed, brightness);
      lastUpdate = millis();
    }
  } else {
    analogWrite(activeLed, 255);
  }
}
