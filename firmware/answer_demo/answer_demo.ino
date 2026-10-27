// K151 MVP integration: question on screen -> direct HTTPS request to the Sarashina API ->
// face tracking and touch feedback while waiting -> validated answer on screen.
//
// Ownership: loop() is the only caller of the portable Interaction core. The API task only
// performs HTTPS and hands results back through a queue; it never touches the core, the
// display or the servos. A cancelled or superseded request cannot be aborted mid-flight;
// its late result is rejected by the core (request id check).
//
// Build: tools/gen_secrets.ps1 first (writes the git-ignored secrets.h), then see HANDOFF.md.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <M5StackChan.h>
#include <M5CoreS3.h>
#include "esp_camera.h"
extern "C" {
#include "picornt.h"
}
#include "facefinder.h"
#include "interaction.hpp"
#include "root_ca.h"
#include "secrets.h"

using stackchan::Face;
using stackchan::State;

// ---- configuration ---------------------------------------------------------------
static const char* kApiUrl = "https://api.sarashina.ai/v1/chat/completions";
static const char* kModel = "sarashina3-nano";            // 60 RPM; mini is 6 RPM
static constexpr uint32_t kHttpTimeoutMs = 12000;          // < core timeout (15000)
static constexpr size_t kMaxAnswerBytes = 180;
static constexpr uint32_t kAnswerShowMs = 8000;
static constexpr int kYawMax = 150;                        // 10 = 1 deg -> +-15 deg
static constexpr int kYawSign = 1;                         // flip if the head turns away from the face
static constexpr int kServoSpeed = 300;
static constexpr uint32_t kServoIntervalMs = 100;
static constexpr int kW = 160, kH = 120;                   // detector input
static constexpr float kQualityMin = 5.0F;

struct Question { const char* label; const char* prompt; };
static const Question kQuestions[3] = {
    {"豆知識", "ロボットに関する豆知識を一つ教えてください。"},
    {"応援", "元気が出る短い一言をください。"},
    {"休憩", "短い休憩のおすすめの過ごし方を教えてください。"},
};
static const char* kSystemPrompt = "あなたは小さな卓上ロボットです。日本語で、40文字以内の一文で答えてください。";

// ---- API task plumbing -----------------------------------------------------------
struct Req { uint64_t id; char prompt[128]; };
struct Res { uint64_t id; bool ok; int http; uint32_t ms; char text[kMaxAnswerBytes + 1]; };
static QueueHandle_t reqQ, resQ;

static bool validAnswer(const char* s, size_t n) {
  if (n == 0 || n > kMaxAnswerBytes) return false;
  for (size_t i = 0; i < n; ++i) if ((uint8_t)s[i] < 0x20 && s[i] != '\n') return false;
  return true;
}

static void apiTask(void*) {
  Req r;
  for (;;) {
    if (xQueueReceive(reqQ, &r, portMAX_DELAY) != pdTRUE) continue;
    Res res = {r.id, false, 0, 0, {0}};
    const uint32_t t0 = millis();
    if (WiFi.status() != WL_CONNECTED || !strlen(SARASHINA_API_KEY)) {
      res.http = -100;
    } else {
      WiFiClientSecure cli;
      cli.setCACert(kRootCa);
      HTTPClient http;
      http.setTimeout(kHttpTimeoutMs);
      if (http.begin(cli, kApiUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + SARASHINA_API_KEY);
        JsonDocument req;
        req["model"] = kModel;
        req["max_completion_tokens"] = 80;
        JsonArray m = req["messages"].to<JsonArray>();
        JsonObject s = m.add<JsonObject>(); s["role"] = "system"; s["content"] = kSystemPrompt;
        JsonObject u = m.add<JsonObject>(); u["role"] = "user"; u["content"] = r.prompt;
        String body; serializeJson(req, body);
        res.http = http.POST(body);
        if (res.http == 200) {
          JsonDocument filter; filter["choices"][0]["message"]["content"] = true;
          JsonDocument doc;
          if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
            const char* c = doc["choices"][0]["message"]["content"] | "";
            const size_t n = strlen(c);
            if (validAnswer(c, n)) { memcpy(res.text, c, n); res.ok = true; }
            else res.http = -200;                      // invalid answer shape or length
          } else res.http = -201;                      // unparsable JSON
        }
        http.end();
      } else res.http = -101;
    }
    res.ms = millis() - t0;
    xQueueSend(resQ, &res, 0);
  }
}

// ---- state owned by loop() --------------------------------------------------------
static stackchan::Interaction core;
static uint8_t gray[kW * kH];
static bool cameraOk = false, wifiOk = false;
static uint64_t activeId = 0;
static char answer[kMaxAnswerBytes + 1] = "";
static char status[48] = "";
static uint32_t answerStartMs = 0, lastServoMs = 0, lastFaceLogMs = 0, faceFrames = 0, frames = 0;
static uint32_t acceptedN = 0, rejectedN = 0;
static float lastX = 0;
static int lastYaw = 0;

static const char* stateJa(State s) {
  switch (s) {
    case State::idle: return "待機";
    case State::waiting: return "考え中";
    case State::answering: return "回答";
    case State::cancelled: return "中止";
    case State::error: return "エラー";
  }
  return "";
}
static uint16_t faceColor(Face f) {
  switch (f) {
    case Face::thinking: return TFT_CYAN;
    case Face::happy: return TFT_YELLOW;
    case Face::answer: return TFT_GREEN;
    case Face::cancelled: return TFT_ORANGE;
    case Face::error: return TFT_RED;
    default: return TFT_WHITE;
  }
}
static void logEvent(const char* ev, uint32_t now, long a = 0, long b = 0) {
  Serial.printf("device_monotonic_ms=%lu event=%s a=%ld b=%ld\n", (unsigned long)now, ev, a, b);
}

static void toGray(const uint16_t* src, int srcW) {          // camera RGB565 needs a byte swap
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const uint16_t raw = src[(y * 2) * srcW + x * 2];
      const uint16_t p = (uint16_t)((raw << 8) | (raw >> 8));
      const int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
      gray[y * kW + x] = (uint8_t)((r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8);
    }
}

static void startQuestion(int i, uint32_t now) {
  const uint64_t id = core.start(now);
  if (id == 0) return;
  activeId = id;
  answer[0] = 0;
  Req r = {id, {0}};
  strncpy(r.prompt, kQuestions[i].prompt, sizeof(r.prompt) - 1);
  xQueueSend(reqQ, &r, 0);
  logEvent("start", now, (long)id, i);
}

static void drawOverlay(const stackchan::Output& out) {
  auto& d = M5StackChan.Display();
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.fillRect(0, 0, d.width(), 22, TFT_BLACK);
  d.fillCircle(11, 11, 8, faceColor(out.face));
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(26, 3);
  d.printf("%s %s%s", stateJa(out.state), out.track_head ? "顔追従" : "顔なし", wifiOk ? "" : " 圏外");
  d.fillRect(d.width() - 36, 0, 36, 22, 0x8000);
  d.setCursor(d.width() - 28, 3);
  d.print("中止");
  if (answer[0] || status[0]) {
    d.fillRect(0, 150, d.width(), 44, TFT_BLACK);
    d.setCursor(4, 152);
    d.setTextColor(answer[0] ? TFT_GREENYELLOW : TFT_ORANGE, TFT_BLACK);
    d.print(answer[0] ? answer : status);
  }
  for (int i = 0; i < 3; ++i) {
    const int x = i * (d.width() / 3);
    d.fillRect(x + 2, 200, d.width() / 3 - 4, 38, 0x2104);
    d.drawRect(x + 2, 200, d.width() / 3 - 4, 38, TFT_WHITE);
    d.setTextColor(TFT_WHITE, 0x2104);
    d.setCursor(x + 14, 211);
    d.print(kQuestions[i].label);
  }
}

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  auto& d = M5StackChan.Display();
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.fillScreen(TFT_BLACK);
  reqQ = xQueueCreate(1, sizeof(Req));
  resQ = xQueueCreate(2, sizeof(Res));
  xTaskCreatePinnedToCore(apiTask, "api", 16384, nullptr, 1, nullptr, 0);

  if (strlen(WIFI_SSID)) {
    d.println("Wi-Fi接続中...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) delay(500);
    wifiOk = WiFi.status() == WL_CONNECTED;
    if (wifiOk) {
      configTime(9 * 3600, 0, "pool.ntp.org", "time.google.com");   // TLS needs a valid clock
      for (int i = 0; i < 20 && time(nullptr) < 1700000000; ++i) delay(500);
      wifiOk = time(nullptr) >= 1700000000;
    }
  }
  Serial.printf("setup wifi=%d key_set=%d free_heap=%u free_psram=%u\n", (int)wifiOk,
                (int)(strlen(SARASHINA_API_KEY) > 0), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  cameraOk = CoreS3.Camera.begin();
  Serial.printf("camera %s\n", cameraOk ? "OK" : "FAIL");
  if (!wifiOk) snprintf(status, sizeof(status), "ネットワークなし");
}

void loop() {
  M5StackChan.update();
  uint32_t now = millis();

  // Camera -> display -> detector. Capture time is taken when the driver returns the frame.
  bool present = false;
  float x = 0;
  if (cameraOk && CoreS3.Camera.get()) {
    const uint32_t captured = millis();
    auto* fb = CoreS3.Camera.fb;
    M5StackChan.Display().pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    toGray((const uint16_t*)fb->buf, fb->width);
    CoreS3.Camera.free();
    float rcsq[4 * 16];
    int n = find_objects(rcsq, 16, (void*)facefinder_cascade, 0.0F, gray, kH, kW, kW, 1.2F, 0.15F, 24.0F, 120.0F);
    n = cluster_detections(rcsq, n);
    int best = -1;
    for (int i = 0; i < n; ++i)
      if (rcsq[4 * i + 3] >= kQualityMin && (best < 0 || rcsq[4 * i + 3] > rcsq[4 * best + 3])) best = i;
    if (best >= 0) {
      present = true;
      x = rcsq[4 * best + 1] / kW * 2.0F - 1.0F;
      ++faceFrames;
      const float row = rcsq[4 * best], col = rcsq[4 * best + 1], s = rcsq[4 * best + 2];
      M5StackChan.Display().drawRect((int)((col - s / 2) * 2), (int)((row - s / 2) * 2), (int)s * 2, (int)s * 2, TFT_GREEN);
    }
    ++frames;
    now = millis();
    core.observe(x, present, captured, now);
    lastX = x;
  }

  // Touch: bottom buttons start a question, top-right cancels, anything else is feedback.
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    auto& d = M5StackChan.Display();
    if (t.y >= 200) startQuestion(constrain(t.x * 3 / d.width(), 0, 2), now);
    else if (t.y < 22 && t.x >= d.width() - 36) { logEvent(core.cancel(now) ? "cancel_ok" : "cancel_ignored", now); }
    else { core.touch(now); logEvent("touch", now); }
  }

  // API results. The core decides whether a result is still wanted.
  Res res;
  while (xQueueReceive(resQ, &res, 0) == pdTRUE) {
    now = millis();
    logEvent("api_done", now, res.http, (long)res.ms);
    if (res.ok) {
      if (core.reply(res.id, now)) {
        strncpy(answer, res.text, sizeof(answer) - 1);
        status[0] = 0;
        answerStartMs = now;
        ++acceptedN;
        logEvent("reply_accepted", now, (long)res.id, (long)strlen(answer));
      } else { ++rejectedN; logEvent("reply_rejected_stale", now, (long)res.id); }
    } else if (core.fail(res.id, now)) {
      snprintf(status, sizeof(status), "通信エラー(%d)", res.http);
      logEvent("request_failed", now, (long)res.id, res.http);
    } else { ++rejectedN; logEvent("failure_rejected_stale", now, (long)res.id); }
  }

  now = millis();
  const auto out = core.output(now);
  if (out.state == State::answering && now - answerStartMs > kAnswerShowMs) core.finish(now);
  if (out.state == State::error && !status[0]) snprintf(status, sizeof(status), "時間切れ");

  // Head: follow a fresh face observation; otherwise drift back to centre.
  if (now - lastServoMs >= kServoIntervalMs) {
    lastServoMs = now;
    const int yaw = out.track_head ? kYawSign * (int)(out.normalized_x * kYawMax) : 0;
    if (abs(yaw - lastYaw) >= 10) { M5StackChan.Motion.moveX(yaw, kServoSpeed); lastYaw = yaw; }
  }

  drawOverlay(out);

  if (now - lastFaceLogMs >= 5000) {
    Serial.printf("device_monotonic_ms=%lu fps=%.1f face_frames=%lu/%lu x=%.2f ok=%lu rej=%lu free_heap=%u\n",
                  (unsigned long)now, frames * 1000.0F / (now - lastFaceLogMs), (unsigned long)faceFrames,
                  (unsigned long)frames, lastX, (unsigned long)acceptedN, (unsigned long)rejectedN,
                  (unsigned)ESP.getFreeHeap());
    frames = faceFrames = 0;
    lastFaceLogMs = now;
  }
}
