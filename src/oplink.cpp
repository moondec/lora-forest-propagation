#include "oplink.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <LittleFS.h>
#include <string.h>

#include "packet.h"
#include "storage.h"

namespace oplink {

// Nordic UART Service, understood by generic phone terminals (e.g. Serial Bluetooth Terminal).
static const char* kNusService = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static const char* kNusRx = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  // phone writes
static const char* kNusTx = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  // board notifies
// With 0 the core's HWCDC retry counter wraps and a write blocks while a host is enumerated
// but not reading; a few ms makes the core drop output instead.
constexpr uint32_t kTxTimeoutMs = 5, kDumpTimeoutMs = 1000;

static BLECharacteristic* txChar = nullptr;
static volatile bool connected = false;
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
// Small queue of complete BLE lines, so "stop X\nh 0.3\n" in one write loses nothing.
constexpr int kQueue = 4;
static char bleQueue[kQueue][96];
static int qHead = 0, qCount = 0;  // guarded by mux
static char bleLine[96];
static size_t bleLen = 0;
static bool bleOverflow = false;
static char usbLine[96];
static size_t usbLen = 0;

static File dumpFile;
static bool dumpActive = false;
static uint32_t dumpLeft = 0, dumpCrc = 0;

class ServerCb : public BLEServerCallbacks {
  void onConnect(BLEServer*) override { connected = true; }
  void onDisconnect(BLEServer* s) override {
    connected = false;
    s->getAdvertising()->start();  // stay discoverable for the next phone
  }
};

class RxCb : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < v.length(); ++i) {
      const char ch = v[i];
      if (ch == '\n' || ch == '\r') {
        if (bleLen > 0) {
          if (qCount < kQueue) {
            bleLine[bleLen] = '\0';
            memcpy(bleQueue[(qHead + qCount) % kQueue], bleLine, bleLen + 1);
            ++qCount;
          } else {
            bleOverflow = true;
          }
        }
        bleLen = 0;
      } else if (bleLen < sizeof bleLine - 1) {
        bleLine[bleLen++] = ch;
      }
    }
    portEXIT_CRITICAL(&mux);
  }
};

void begin(const char* bleName) {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(kTxTimeoutMs);
  BLEDevice::init(bleName);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCb());
  BLEService* svc = server->createService(kNusService);
  txChar = svc->createCharacteristic(kNusTx, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());
  BLECharacteristic* rx =
      svc->createCharacteristic(kNusRx, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCb());
  svc->start();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(kNusService);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

bool readLine(char* buf, size_t n) {
  while (Serial.available()) {
    const int c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (usbLen > 0) {
        usbLine[usbLen] = '\0';
        snprintf(buf, n, "%s", usbLine);
        usbLen = 0;
        return true;
      }
    } else if (usbLen < sizeof usbLine - 1) {
      usbLine[usbLen++] = static_cast<char>(c);
    }
  }
  bool got = false, overflow = false;
  portENTER_CRITICAL(&mux);
  if (qCount > 0) {
    snprintf(buf, n, "%s", bleQueue[qHead]);
    qHead = (qHead + 1) % kQueue;
    --qCount;
    got = true;
  }
  overflow = bleOverflow;
  bleOverflow = false;
  portEXIT_CRITICAL(&mux);
  if (overflow) println("ERR busy: a command was dropped, send it again");
  return got;
}

static void bleSend(const char* s) {
  if (!connected || !txChar) return;
  // 20-byte notifications work without MTU negotiation on every phone.
  const size_t n = strlen(s);
  for (size_t i = 0; i < n; i += 20) {
    txChar->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(s + i)), n - i < 20 ? n - i : 20);
    txChar->notify();
  }
  txChar->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>("\n")), 1);
  txChar->notify();
}

void println(const char* s) {
  if (!dumpActive) Serial.println(s);
  bleSend(s);
}

bool startDump(const char* name) {
  if (dumpActive) return false;
  storage::flush();
  char p[32];
  snprintf(p, sizeof p, "/log/%s", name);
  dumpFile = LittleFS.open(p, FILE_READ);
  if (!dumpFile) return false;
  // The size is fixed here; records appended to an open file during the dump are not sent.
  dumpLeft = dumpFile.size();
  dumpCrc = 0;
  Serial.setTxTimeoutMs(kDumpTimeoutMs);  // a dump needs every byte delivered
  Serial.printf("BEGIN %s %lu\n", name, static_cast<unsigned long>(dumpLeft));
  dumpActive = true;
  return true;
}

bool dumping() { return dumpActive; }

void service() {
  if (!dumpActive) return;
  uint8_t chunk[2048];
  const size_t want = dumpLeft < sizeof chunk ? dumpLeft : sizeof chunk;
  const size_t n = want ? dumpFile.read(chunk, want) : 0;
  if (n > 0) {
    dumpCrc = core::crc32Ieee(dumpCrc, chunk, n);  // computed while sending: no long stall at the start
    Serial.write(chunk, n);
    dumpLeft -= n;
  }
  if (dumpLeft == 0 || n == 0) {
    dumpFile.close();
    // A short read leaves the host waiting for missing bytes: it times out and reports the file as failed.
    Serial.printf("\nEND %08lx\n", static_cast<unsigned long>(dumpCrc));
    Serial.flush();
    Serial.setTxTimeoutMs(kTxTimeoutMs);
    dumpActive = false;
  }
}

bool bleConnected() { return connected; }

}  // namespace oplink
