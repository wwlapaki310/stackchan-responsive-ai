// K151/CoreS3 on-device face detection (pico, MIT) on camera frames. No servo, no network.
// Frame is downsampled 2x to 160x120 grayscale. Capture time is millis() when the driver
// returned the frame, taken before detection (the core expects capture time, not inference end).
// The camera returns RGB565 with swapped bytes relative to a native uint16_t read
// (verified by dumping both decodings to a PNG on 2026-10-27); toGray() swaps them.
#include <Arduino.h>
#include <M5StackChan.h>
#include <M5CoreS3.h>
#include "esp_camera.h"
extern "C" {
#include "picornt.h"
}
#include "facefinder.h"

static constexpr int kW = 160, kH = 120;
static constexpr float kQualityMin = 5.0F;
static uint8_t gray[kW * kH];
static bool cameraOk = false;
static uint32_t frames = 0, windowStart = 0, detUsSum = 0, detUsMax = 0, faceFrames = 0;
static float lastX = 0, lastQ = 0, maxQ = 0;

static void toGray(const uint16_t* src, int srcW) {
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      const uint16_t raw = src[(y * 2) * srcW + x * 2];
      const uint16_t p = (uint16_t)((raw << 8) | (raw >> 8));
      const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      gray[y * kW + x] = (uint8_t)((r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8);
    }
  }
}

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  cameraOk = CoreS3.Camera.begin();
  Serial.printf("camera %s free_heap=%u free_psram=%u\n", cameraOk ? "OK" : "FAIL",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  windowStart = millis();
}

void loop() {
  if (!cameraOk || !CoreS3.Camera.get()) { delay(5); return; }
  auto* fb = CoreS3.Camera.fb;
  M5StackChan.Display().pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
  toGray((const uint16_t*)fb->buf, fb->width);
  CoreS3.Camera.free();

  float rcsq[4 * 16];
  const uint32_t t0 = micros();
  int n = find_objects(rcsq, 16, (void*)facefinder_cascade, 0.0F, gray, kH, kW, kW, 1.2F, 0.15F, 24.0F, 120.0F);
  n = cluster_detections(rcsq, n);
  const uint32_t us = micros() - t0;
  detUsSum += us;
  if (us > detUsMax) detUsMax = us;

  int best = -1;
  for (int i = 0; i < n; ++i) {
    if (rcsq[4 * i + 3] > maxQ) maxQ = rcsq[4 * i + 3];
    if (rcsq[4 * i + 3] >= kQualityMin && (best < 0 || rcsq[4 * i + 3] > rcsq[4 * best + 3])) best = i;
  }
  if (best >= 0) {
    const float row = rcsq[4 * best], col = rcsq[4 * best + 1], s = rcsq[4 * best + 2];
    lastX = col / kW * 2.0F - 1.0F;
    lastQ = rcsq[4 * best + 3];
    ++faceFrames;
    M5StackChan.Display().drawRect((int)((col - s / 2) * 2), (int)((row - s / 2) * 2), (int)s * 2, (int)s * 2, TFT_GREEN);
  }
  ++frames;

  const uint32_t now = millis();
  if (now - windowStart >= 5000) {
    Serial.printf("device_monotonic_ms=%lu fps=%.1f detect_us_avg=%lu detect_us_max=%lu face_frames=%lu/%lu last_x=%.2f last_q=%.1f maxq=%.1f free_heap=%u\n",
                  (unsigned long)now, frames * 1000.0F / (now - windowStart),
                  (unsigned long)(detUsSum / (frames ? frames : 1)), (unsigned long)detUsMax,
                  (unsigned long)faceFrames, (unsigned long)frames, lastX, lastQ, maxQ, (unsigned)ESP.getFreeHeap());
    frames = faceFrames = detUsSum = detUsMax = 0;
    maxQ = 0;
    windowStart = now;
  }
}
