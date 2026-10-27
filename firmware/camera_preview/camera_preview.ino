// K151/CoreS3 camera bring-up: grab frames, show them on the display, log fps and heap.
// No face detection, no network, no servo. Frame capture time uses the same boot-local
// millis() clock as the core; it is taken when the frame is returned by the driver.
#include <Arduino.h>
#include <M5StackChan.h>
#include <M5CoreS3.h>
#include "esp_camera.h"

static uint32_t frames = 0, windowStart = 0;
static bool cameraOk = false;

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  auto& d = M5StackChan.Display();
  d.setTextSize(2);
  cameraOk = CoreS3.Camera.begin();
  if (!cameraOk) {
    d.fillScreen(TFT_BLACK);
    d.setTextColor(TFT_RED);
    d.drawString("Camera init FAIL", 8, 8);
    Serial.println("camera init FAIL");
    return;
  }
  Serial.printf("camera init OK free_heap=%u free_psram=%u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());
  windowStart = millis();
}

void loop() {
  if (!cameraOk) { delay(1000); return; }
  if (CoreS3.Camera.get()) {
    const uint32_t captured = millis();
    auto* fb = CoreS3.Camera.fb;
    if (frames == 0) {
      Serial.printf("first frame %ux%u format=%d len=%u captured_ms=%lu\n", (unsigned)fb->width,
                    (unsigned)fb->height, (int)fb->format, (unsigned)fb->len, (unsigned long)captured);
    }
    M5StackChan.Display().pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    CoreS3.Camera.free();
    ++frames;
  }
  const uint32_t now = millis();
  if (now - windowStart >= 5000) {
    Serial.printf("device_monotonic_ms=%lu fps=%.1f free_heap=%u free_psram=%u\n", (unsigned long)now,
                  frames * 1000.0F / (now - windowStart), (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getFreePsram());
    frames = 0;
    windowStart = now;
  }
}
