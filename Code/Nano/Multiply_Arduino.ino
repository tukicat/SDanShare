//#include <BlockDriver.h>
//#include <FreeStack.h>
//#include <MinimumSerial.h>
#include <SdFat.h>
//#include <SdFatConfig.h>
//#include <sdios.h>
//#include <SysCall.h>

const uint8_t MultiplyVersion[8] = { 'M','U','L','T','v','1','.','2' };  // Version 1.2 fixed NO SD CARD

//#define PinPower_A7           // A7 for Dan V3, Comment #define for D9 for Multiply+Dan2.x
#define pinPower A7

#include "incCmdsFileTypes.h"
#include "incDelayDefs.h"

#define DEFAULT_TIMEOUT 1000
#define CS_PIN 10
#define BUFRSIZE 512
#define SCR_BLKSIZE 432

#define HW16K 1
#define HW48K 2
#define HW128K 3

SdFat SD;
File myFile;
File CurDir;
uint8_t myBuf[BUFRSIZE];
bool SdCardOk = false;
bool FileOk = false;
uint8_t HWZX = HW128K;

//-------------------------------------------------------------------------------------------------
void setup() {

  #ifdef PinPower_A7
    pinMode(pinPower, INPUT);
    #if FASTADC
      sbi(ADCSRA, ADPS2);
      cbi(ADCSRA, ADPS1);
      cbi(ADCSRA, ADPS0);
    #endif
  #endif

  DDRB &= 0xFD;  // PortB bit 1 as input D9
  PORTB &= 0xFD; // no pull‑up
  Serial.begin(57600, SERIAL_8N2);
  UCSR0B &= 0xF7; // deactivate TX

  // --- LED test setup ---
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  InitJoyPassT();

  // ⭐ SLOW SD CLOCK FOR 245 LEVEL SHIFTER ⭐
  if (SD.begin(CS_PIN, SD_SCK_MHZ(4))) SdCardOk = true;

  Init4bit();
  CurDir = SD.open(ROOTDIR, O_READ);
}

//-------------------------------------------------------------------------------------------------
void loop() {
  #define cmdBufSize 8
  uint8_t cmdBuf[cmdBufSize];
  uint16_t *index;
  #ifdef PinPower_A7
    bool test = false;
  #endif

  // --- D9 detect test ---
  if ((PINB & 0x02) == 0) {  // D9 LOW → disconnected
    digitalWrite(LED_BUILTIN, LOW);
    ShutdownPower4bit();
    Init4bit();
  } else {
    digitalWrite(LED_BUILTIN, HIGH); // D9 HIGH → connected
  }

  while (!Serial.available()) {
    JoyThrough();
    #ifdef PinPower_A7
      test = (analogRead(pinPower) < 900);
    #endif
  }

  #ifdef PinPower_A7
    if (test) {
  #else
    if ((PINB & 0x02) == 0) {  // Avoid power to pins if Spectrum is disconnected
  #endif
      ShutdownPower4bit();
      Init4bit();
    }

  cmdBuf[0] = Serial.read();
  switch (cmdBuf[0]) {
    case CMD_ZX2INO_REQ_ID:
      delay(DlyROMSETms);
      for (int i = 0; i < 8; i++) Send4bit(MultiplyVersion[i], DlyANSWERus);
      break;
    case CMD_ZX2SD_SETZXTYPE:
      if (getBuffer_N(myBuf, 1)) HWZX = *myBuf;
      break;
    case CMD_ZX2SD_OFREAD:
      if (getBuffer_0(myBuf)) FileOk = (*OpenFile(&myFile, myBuf) == 0x01);
      break;
    case CMD_ZX2SD_OFREAD_IX:
      if (getBuffer_N(myBuf, 2)) {
        index = (uint16_t *)(myBuf);
        FileOk = OpenFileIX(&CurDir, &myFile, *index);
      }
      break;
    case CMD_ZX2SD_CD_ROOT:
      OpenDir(&CurDir, (uint8_t *)ROOTDIR);
      SD.chdir(true);
      break;
    case CMD_ZX2SD_CD:
      if (getBuffer_0(myBuf)) {
        if (OpenDirTmp(myBuf)) {
          OpenDir(&CurDir, myBuf);
          SD.chdir((char *)myBuf, true);
        }
      }
      break;
    case CMD_ZX2SD_CD_IX:
      if (getBuffer_N(myBuf, 2)) {
        FileOk = OpenDirIX(&CurDir, &myFile, myBuf);
        if (FileOk) SD.chdir((char *)myBuf, true);
      }
      break;
    case CMD_ZX2SD_GETDIR:
      GetDir(&CurDir, myBuf);
      break;
    case CMD_ZX2SD_LS_RELATIVE:
      if (getBuffer_N(myBuf, 2)) {
        index = (uint16_t *)(myBuf);
        ListDir(&CurDir, SdCardOk, myBuf, index);
      }
      break;
    case CMD_ZX2SD_LS_ABSOLUTE:
      if (getBuffer_0(myBuf)) ListDirTmp(myBuf);
      break;
    case CMD_ZX2SD_ROMSETBLK4B:
      if (getBuffer_N(cmdBuf + 1, 1) && FileOk && SdCardOk)
        SendRomsetBlock4b(cmdBuf[1] - 1);
      break;
    case CMD_ZX2SD_SNA_128K:
    case CMD_ZX2SD_SNA_48K:
      if (getBuffer_N(cmdBuf + 1, 1)) {
        SendSNA(cmdBuf[0], cmdBuf[1], myBuf);
        if (cmdBuf[1] == 10) myFile.close();
      }
      break;
    case CMD_ZX2SD_Z80_128K:
    case CMD_ZX2SD_Z80_48K:
    case CMD_ZX2SD_Z80_16K:
      if (getBuffer_N(cmdBuf + 1, 1)) {
        SendZ80(cmdBuf[0], cmdBuf[1]);
        if (cmdBuf[1] == 10) myFile.close();
      }
      break;
    case CMD_ZX2SD_SCRTAP:
      if (getBuffer_N(cmdBuf + 1, 1)) SendSCRTAP(&myFile, myBuf, *(cmdBuf + 1));
      break;
    case CMD_ZX2SD_SCR:
      if (getBuffer_N(cmdBuf + 1, 1)) SendSCR(&myFile, myBuf, *(cmdBuf + 1));
      break;
    case CMD_ZX2SD_SCR_FROM_SNA:
      if (getBuffer_N(cmdBuf + 1, 1)) SendSCRSNA(&myFile, myBuf, *(cmdBuf + 1));
      break;
    case CMD_ZX2SD_SCR_FROM_Z80:
      if (getBuffer_N(cmdBuf + 1, 1)) SendSCRZ80(&myFile, myBuf, *(cmdBuf + 1));
      break;
    case CMD_ZX2SD_GETINFO:
      if (getBuffer_N(myBuf + 256, 2)) GetMoreInfoIX(&CurDir, &myFile, myBuf + 256);
      break;
    case CMD_ZX2SD_TAP:
      if (getBuffer_N(cmdBuf + 1, 5)) {
        delay(DlyTAPms);
        SendTAP(&myFile, cmdBuf + 1);
      }
      break;
    case CMD_PC2AR_ROMSET_TUNNEL:
    case CMD_PC2AR_ROMSET_TUN2:
      RomsetSerialTunnel(cmdBuf[0], true);
      break;
    case CMD_PC2AR_BIN_TUNNEL:
      RomsetSerialTunnel(cmdBuf[0], false);
      break;
    case 0x00:
      setup();
      break;
    default:
      break;
  }
}
