// K151 demo: Stack-chan notices a person, greets, asks the cloud LLM something by itself,
// keeps tracking and reacting while it waits, then speaks and shows the answer.
//
// Ownership: loop() is the only caller of the portable Interaction core and the Director
// policy. The API task only performs HTTPS (LLM, then optional text-to-speech) and hands
// results back through a queue; it never touches the core, the display, the speaker or the
// servos. A cancelled or superseded request cannot be aborted mid-flight; its late result
// is rejected by the core (request id check).
//
// Build: tools/gen_secrets.ps1 first (writes the git-ignored secrets.h), optionally
// tools/gen_voice.py (fixed phrases, Open JTalk), then see HANDOFF.md.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <M5StackChan.h>
#include <M5CoreS3.h>
#include "esp_camera.h"
#include "mbedtls/base64.h"
extern "C" {
#include "picornt.h"
}
#include "facefinder.h"
#include "director.hpp"
#include "interaction.hpp"
#include "root_ca.h"
#include "secrets.h"
#if __has_include("voice_clips.h")
#include "voice_clips.h"
#else
#define VOICE_CLIPS_AVAILABLE 0
#endif

using stackchan::Act;
using stackchan::Face;
using stackchan::State;

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

// ---- configuration ---------------------------------------------------------------
static const char* kApiUrl = "https://api.sarashina.ai/v1/chat/completions";
static const char* kModel = "sarashina3-nano";            // 60 RPM; mini is 6 RPM
static const char* kTtsUrl = "https://api.openai.com/v1/audio/speech";
static const char* kTtsModel = "gpt-4o-mini-tts";          // OpenAI marks it deprecated, shutdown 2027-01-06
static const char* kTtsVoice = "coral";
static const char* kTtsStyle = "明るく親しみやすい、少し高めの声の日本語で、はっきり話してください。";
// Optional vision: one camera frame (JPEG) goes to a multimodal cloud model, only when the
// operator enables it on the web page and only for an explicit "look" or an autonomous ask.
static const char* kLookUrl = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent";   // 2.5 models answered 404 for this key (2026-10-30)
static const char* kLookPrompt = "添付の画像は、あなたの目(カメラ)に今映っているものです。見えているものに触れて、ロボットとして一言で話しかけてください。";
static constexpr uint32_t kHttpTimeoutMs = 12000;          // < core timeout (15000)
static constexpr uint32_t kTtsTimeoutMs = 9000;
static constexpr size_t kTtsMaxBytes = 24000 * 2 * 8;      // 8 s of 24 kHz 16-bit mono
static constexpr size_t kMaxAnswerBytes = 180;
static constexpr uint32_t kAnswerShowMs = 6000;            // minimum caption time; longer while speaking
static constexpr uint32_t kLongPressMs = 900;
static constexpr int kYawMax = 450;                        // 10 = 1 deg -> +-45 deg
static constexpr int kYawSign = 1;                         // verified 2026-10-27: face moves toward the centre
static constexpr int kServoSpeed = 600;
static constexpr float kYawGain = 80.0F;                   // yaw units added per detection at |x| = 1
static constexpr float kDeadband = 0.08F;                  // |x| below this is "centred"
static constexpr uint32_t kServoIntervalMs = 100;
static constexpr int kW = 160, kH = 120;                   // detector input
static constexpr float kQualityMin = 5.0F;
static constexpr int kWanderYaw = 250;                     // +-25 deg glances while nobody is seen

struct Question { const char* label; const char* prompt; };
static const Question kQuestions[3] = {
    {"豆知識", "ロボットに関する豆知識を一つ教えてください。"},
    {"応援", "元気が出る短い一言をください。"},
    {"休憩", "短い休憩のおすすめの過ごし方を教えてください。"},
};
// What the robot asks by itself when it has been looking at someone for a while.
static const char* const kAutoPrompts[] = {
    "ロボットに関する豆知識を一つ教えてください。",
    "目の前の人に、元気が出る短い一言をください。",
    "目の前の人に、答えやすい軽い質問を一つしてください。",
    "あなた自身の自己紹介を一言でお願いします。",
};
static const char* kSystemPrompt = "あなたは小さな卓上ロボットです。日本語で、40文字以内の一文で答えてください。【状況】があれば、それに合わせて答えてください。";

// ---- speaker ------------------------------------------------------------------------
static bool muted = false;
// The CoreS3 microphone and speaker share the audio port and cannot run together: the mic
// listens while the robot is silent, and is stopped before anything is played.
static bool micOn = false, micAllowed = true, micFailed = false;
static uint32_t micStartMs = 0;                            // sounds in the first 700 ms after (re)start are start-up noise
static constexpr size_t kMicN = 256;                       // 16 ms at 16 kHz
static int16_t micBuf[2][kMicN];
static volatile uint32_t micLevel = 0, micWindowMax = 0;   // mean absolute deviation of the last buffer
static void micReleased(void*, void* data, size_t length) {
  const int16_t* p = (const int16_t*)data;
  int32_t sum = 0;
  for (size_t i = 0; i < length; ++i) sum += p[i];
  const int32_t mean = sum / (int32_t)length;
  uint32_t dev = 0;
  for (size_t i = 0; i < length; ++i) dev += (uint32_t)abs(p[i] - mean);
  micLevel = dev / length;
  if (micLevel > micWindowMax) micWindowMax = micLevel;
  M5.Mic.record((int16_t*)data, length, 16000);            // keep capturing (allowed from this callback)
}
static void startMic() {
  M5.Speaker.end();
  M5.Mic.setBufferReleaseCallback(nullptr, micReleased);
  if (M5.Mic.begin()) {
    micLevel = 0;
    micWindowMax = 0;
    for (int i = 0; i < 2; ++i) M5.Mic.record(micBuf[i], kMicN, 16000);
    micStartMs = millis();
    micOn = true;
  } else {
    micFailed = true;
  }
}
static void useSpeaker() {
  if (micOn) { M5.Mic.end(); micOn = false; micLevel = 0; }
  if (!M5.Speaker.isRunning()) M5.Speaker.begin();
}
#if VOICE_CLIPS_AVAILABLE
static void sayClip(int id) {
  if (!muted) useSpeaker();
  if (!muted) M5.Speaker.playRaw(kVoiceClips[id].data, kVoiceClips[id].samples, VOICE_SAMPLE_RATE, false, 1, -1, false);
}
#define SAY(name) sayClip(VOICE_##name)
#else
#define SAY(name) ((void)0)
#endif

// ---- API task plumbing -----------------------------------------------------------
struct Req { uint64_t id; char prompt[256]; uint8_t* jpg; uint32_t jpgLen; };   // jpg: malloc'd, owned and freed by apiTask
static volatile bool visionEnabled = false;                // opt-in from the web page: sends camera frames to the cloud
struct Res { uint64_t id; bool ok; int http; uint32_t ms; uint32_t pcmBytes; int ttsHttp; uint32_t ttsMs; char text[kMaxAnswerBytes + 1]; };
static QueueHandle_t reqQ, resQ;
static uint8_t* ttsBuf = nullptr;                          // PSRAM; 24 kHz s16le from the TTS service
static volatile bool ttsHeld = false;                      // true while ttsBuf holds audio not yet played out
static volatile bool ttsEnabled = false;                   // opt-in from the web page: needs an OpenAI key with credit
static volatile int faultMode = 0;                         // test only (web /opt?n=fault&v=): 1 bad key, 2 unreachable host, 3 hang 16 s
static volatile uint64_t wantedId = 0;                     // id loop() still wants; lets the task skip useless TTS

static bool validAnswer(const char* s, size_t n) {
  if (n == 0 || n > kMaxAnswerBytes) return false;
  for (size_t i = 0; i < n; ++i) if ((uint8_t)s[i] < 0x20 && s[i] != '\n') return false;
  return true;
}

// Fetches speech for `text` into ttsBuf. Returns the byte count (0 = no audio) and sets `code`.
static size_t fetchTts(const char* text, int& code) {
  code = 0;
  if (!ttsBuf || !ttsEnabled || !strlen(OPENAI_API_KEY)) return 0;
  for (int i = 0; i < 80 && ttsHeld; ++i) vTaskDelay(pdMS_TO_TICKS(100));   // previous clip still playing
  if (ttsHeld) { code = -300; return 0; }
  WiFiClientSecure cli;
  cli.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start);
  HTTPClient http;
  http.useHTTP10(true);                                    // no chunked framing: the body is raw PCM until close
  http.setTimeout(kTtsTimeoutMs);
  if (!http.begin(cli, kTtsUrl)) { code = -101; return 0; }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
  JsonDocument req;
  req["model"] = kTtsModel;
  req["voice"] = kTtsVoice;
  req["input"] = text;
  req["instructions"] = kTtsStyle;
  req["response_format"] = "pcm";
  String body; serializeJson(req, body);
  code = http.POST(body);
  size_t n = 0;
  if (code == 200) {
    WiFiClient* s = http.getStreamPtr();
    const uint32_t t0 = millis();
    while (n < kTtsMaxBytes && millis() - t0 < kTtsTimeoutMs && (s->connected() || s->available())) {
      const int a = s->available();
      if (a > 0) n += s->readBytes(ttsBuf + n, min((size_t)a, kTtsMaxBytes - n));
      else vTaskDelay(pdMS_TO_TICKS(5));
    }
    n &= ~(size_t)1;
    if (n < 2400) { code = -301; n = 0; }                  // under 50 ms: not usable audio
  }
  http.end();
  if (n) ttsHeld = true;
  return n;
}

// One image + prompt to the multimodal model. Returns the HTTP code (200 = text filled).
static int askGemini(const char* prompt, const uint8_t* jpg, size_t jpgLen, char* text, size_t cap) {
  JsonDocument d;
  d["systemInstruction"]["parts"][0]["text"] = kSystemPrompt;
  JsonObject p0 = d["contents"][0]["parts"][0].to<JsonObject>();
  p0["inline_data"]["mime_type"] = "image/jpeg";
  p0["inline_data"]["data"] = "@@";
  d["contents"][0]["parts"][1]["text"] = prompt;
  d["generationConfig"]["maxOutputTokens"] = 300;           // headroom in case the model spends tokens on thinking
  String s; serializeJson(d, s);
  const int at = s.indexOf("@@");
  size_t b64 = 0;
  mbedtls_base64_encode(nullptr, 0, &b64, jpg, jpgLen);
  const size_t total = s.length() - 2 + b64;
  char* body = (char*)ps_malloc(total + 1);
  if (at < 0 || !body) { free(body); return -102; }
  memcpy(body, s.c_str(), at);
  size_t olen = 0;
  mbedtls_base64_encode((uint8_t*)body + at, b64 + 1, &olen, jpg, jpgLen);
  memcpy(body + at + olen, s.c_str() + at + 2, s.length() - at - 2);
  const size_t len = at + olen + s.length() - at - 2;
  WiFiClientSecure cli;
  cli.setCACertBundle(rootca_crt_bundle_start, rootca_crt_bundle_end - rootca_crt_bundle_start);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(kHttpTimeoutMs);
  int code = -101;
  if (http.begin(cli, kLookUrl)) {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-goog-api-key", GEMINI_API_KEY);
    code = http.POST((uint8_t*)body, len);
    if (code == 200) {
      JsonDocument filter; filter["candidates"][0]["content"]["parts"][0]["text"] = true;
      JsonDocument doc;
      if (!deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter))) {
        const char* c = doc["candidates"][0]["content"]["parts"][0]["text"] | "";
        size_t n = strlen(c);
        if (n > cap - 1) { n = cap - 1; while (n > 0 && ((uint8_t)c[n] & 0xC0) == 0x80) --n; }   // cut on a character boundary
        if (n == 0) code = -200;
        else { memcpy(text, c, n); text[n] = 0; }
      } else code = -201;
    }
    http.end();
  }
  free(body);
  return code;
}

static void apiTask(void*) {
  Req r;
  for (;;) {
    if (xQueueReceive(reqQ, &r, portMAX_DELAY) != pdTRUE) continue;
    Res res = {r.id, false, 0, 0, 0, 0, 0, {0}};
    const uint32_t t0 = millis();
    if (WiFi.status() != WL_CONNECTED || !strlen(SARASHINA_API_KEY)) {
      res.http = -100;
    } else if (r.jpg) {
      res.http = strlen(GEMINI_API_KEY) ? askGemini(r.prompt, r.jpg, r.jpgLen, res.text, sizeof(res.text)) : -103;
      res.ok = res.http == 200;
    } else {
      WiFiClientSecure cli;
      cli.setCACert(kRootCa);
      HTTPClient http;
      http.setTimeout(kHttpTimeoutMs);
      if (faultMode == 3) vTaskDelay(pdMS_TO_TICKS(16000));    // simulated hang: the core times out first
      if (http.begin(cli, faultMode == 2 ? "https://10.255.255.1/v1/chat/completions" : kApiUrl)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Authorization", String("Bearer ") + (faultMode == 1 ? "invalid" : SARASHINA_API_KEY));
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
    if (r.jpg) free(r.jpg);
    if (res.ok && wantedId == r.id) {                  // speech only for an answer somebody still wants
      const uint32_t t1 = millis();
      res.pcmBytes = (uint32_t)fetchTts(res.text, res.ttsHttp);
      res.ttsMs = millis() - t1;
    }
    xQueueSend(resQ, &res, 0);
  }
}

// ---- state owned by loop() --------------------------------------------------------
static stackchan::Interaction core;
static stackchan::Director director;
static M5Canvas canvas(&M5.Display);   // off-screen frame: compose camera + overlay, then push once
static uint8_t gray[kW * kH];
static bool cameraOk = false, wifiOk = false;
static bool debugView = false;   // false: Stack-chan's face; true: camera preview with detection box and status bar
static bool streamOn = false;    // camera snapshots over the LAN; off at every boot
static bool autoOn = true;       // autonomous greeting / asking
static int boxX = 0, boxY = 0, boxS = 0;   // last detection in 320x240 camera pixels
static uint32_t boxMs = 0;
static uint64_t activeId = 0;
static char answer[kMaxAnswerBytes + 1] = "";
static char status[48] = "";
static uint32_t answerStartMs = 0, lastServoMs = 0, lastFaceLogMs = 0, faceFrames = 0, frames = 0;
static uint32_t acceptedN = 0, rejectedN = 0;
static float lastX = 0, bestQ = 0, windowMaxQ = 0;
static int lastYaw = 0, yawTarget = 0;
static bool prevTrack = false, smoothValid = false;
static uint32_t trackStartMs = 0, lastSoundSeenMs = 0, soundN = 0;   // inputs of the one-line scene description sent to the cloud
static float smoothX = 0;
static uint32_t lastTrackMs = 0;
static constexpr uint32_t kHoldMs = 3000;                  // keep the head still this long after the face is lost
static uint32_t trackFrames = 0, servoCmds = 0;
static uint32_t happyUntilMs = 0, nextWanderMs = 0, lastTouchVoiceMs = 0;
static uint32_t pressMs = 0, ttsPlayMs = 0;
static bool pressing = false, longDone = false, ttsPlaying = false, timeoutSaid = false;
static unsigned autoIndex = 0;
static int trackMode = 0;            // 0 face detection (pico), 1 motion (frame difference), 2 face, motion as fallback
static uint32_t lastMoveMs = 0;      // last servo command: the camera moves with the head
static uint8_t prevGray[kW * kH];
static bool prevValid = false;
static float micFloor = 150;                               // adaptive background level
static uint32_t lastAudioMs = 0, lastSoundMs = 0, lastListenMs = 0;

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
struct Ev { uint32_t ms; const char* name; long a; };      // names are string literals
static Ev evBuf[32];
static uint32_t evSeq = 0;
static uint32_t lastApiMs = 0;
static float lastFps = 0;
static void logEvent(const char* ev, uint32_t now, long a = 0, long b = 0) {
  if (strcmp(ev, "servo_yaw") != 0) { evBuf[evSeq % 32] = {now, ev, a}; ++evSeq; }
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

// Simple alternative to the face detector: where does the image change most? Frame difference
// on 4x4-pixel cells; the changed cells' centroid is the target. The camera sits on the head,
// so frames taken while the head moves are ignored (the whole image shifts). Returns false
// when nothing is moving, when too much of the image changes (light flicker, head motion), or
// right after a head move. Always refreshes the reference frame.
static bool detectMotion(float& x, int& bx, int& by, int& bs, float& quality, uint32_t now) {
  constexpr int cw = kW / 4, ch = kH / 4;
  const bool valid = prevValid && now - lastMoveMs > 350;
  bool found = false;
  if (valid) {
    int active = 0;
    float sx = 0, sy = 0, sxx = 0, syy = 0;
    for (int cy = 0; cy < ch; ++cy)
      for (int cx = 0; cx < cw; ++cx) {
        int changed = 0;
        for (int j = 0; j < 4; ++j) {
          const int o = (cy * 4 + j) * kW + cx * 4;
          for (int i = 0; i < 4; ++i) changed += abs((int)gray[o + i] - (int)prevGray[o + i]) >= 24;
        }
        if (changed >= 6) { ++active; sx += cx; sy += cy; sxx += cx * cx; syy += cy * cy; }
      }
    if (active >= 4 && active <= cw * ch * 35 / 100) {
      const float mx = sx / active, my = sy / active;
      const float vx = max(0.0F, sxx / active - mx * mx), vy = max(0.0F, syy / active - my * my);
      x = (mx + 0.5F) / cw * 2.0F - 1.0F;
      bs = (int)constrain(sqrtf(max(vx, vy)) * 3.0F + 2.0F, 3.0F, 30.0F) * 8;   // 320x240 pixels
      bx = (int)((mx + 0.5F) * 8) - bs / 2;
      by = (int)((my + 0.5F) * 8) - bs / 2;
      quality = active / 10.0F;
      found = true;
    }
  }
  memcpy(prevGray, gray, sizeof(prevGray));
  prevValid = true;
  return found;
}

// What the robot perceived, as one line of text for the cloud model. Only these derived facts
// (tracking time, a loudness flag, head direction) leave the board: never the image or the sound.
static void sceneText(char* out, size_t n, uint32_t now) {
  const int secs = prevTrack ? (int)((now - trackStartMs) / 1000) : 0;
  const char* side = lastYaw > 150 ? "右" : (lastYaw < -150 ? "左" : "正面");
  const bool loud = soundN > 0 && now - lastSoundSeenMs < 20000;
  snprintf(out, n, "【状況】%s。顔が見えている時間は約%d秒。顔の向きは%s。%s",
           prevTrack ? "目の前に人がいる" : "人は見えない", secs, side,
           loud ? "直前に大きな音がした。" : "周りは静か。");
}

static bool startPrompt(const char* prompt, int tag, uint32_t now, uint8_t* jpg = nullptr, size_t jpgLen = 0) {
  const uint64_t id = core.start(now);
  if (id == 0) { free(jpg); return false; }
  activeId = id;
  wantedId = id;
  answer[0] = 0;
  status[0] = 0;
  timeoutSaid = false;
  Req r = {id, {0}};
  strncpy(r.prompt, prompt, sizeof(r.prompt) - 1);
  r.jpg = jpg;
  r.jpgLen = (uint32_t)jpgLen;
  if (xQueueSend(reqQ, &r, 0) != pdTRUE) free(jpg);
  logEvent("start", now, (long)id, tag);            // the prompt text itself is never logged
  return true;
}

// Take one camera frame and ask the multimodal model about it. The JPEG lives only in RAM until
// the request is done; it is never stored or logged.
static bool startLook(uint32_t now) {
  if (!visionEnabled || !strlen(GEMINI_API_KEY) || !cameraOk || !CoreS3.Camera.get()) return false;
  uint8_t* jpg = nullptr;
  size_t len = 0;
  const bool ok = frame2jpg(CoreS3.Camera.fb, 50, &jpg, &len);
  CoreS3.Camera.free();
  if (!ok || !jpg) return false;
  char scene[112];
  sceneText(scene, sizeof(scene), now);
  char full[sizeof(Req::prompt)];
  snprintf(full, sizeof(full), "%s%s", scene, kLookPrompt);
  return startPrompt(full, 30, now, jpg, len);
}

// Cancel whatever is going on: a pending request, an answer being shown, speech.
static bool doCancel(uint32_t now) {
  bool ok = core.cancel(now);
  if (!ok && core.output(now).state == State::answering) { core.finish(now); ok = true; }
  wantedId = 0;
  M5.Speaker.stop();
  if (ttsPlaying) { ttsPlaying = false; ttsHeld = false; }
  if (ok) { answer[0] = 0; SAY(CANCEL); }
  logEvent(ok ? "cancel_ok" : "cancel_ignored", now);
  return ok;
}

// ---- remote control over the LAN (plain HTTP + access token) ------------------------
// An operator view for the demo and for tests; the robot itself needs no buttons.
// Handlers run inside web.handleClient(), which loop() calls, so the core keeps a single
// owner. Control endpoints need ?t=<WEB_TOKEN>. There is no TLS: use a trusted LAN only.
static WebServer web(80);
static uint32_t lastWebAskMs = 0;
static const char kPage[] PROGMEM = R"HTML(<!doctype html><html lang="ja"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Stack-chan</title>
<style>body{font-family:sans-serif;max-width:46em;margin:1em auto;padding:0 1em}
button{font-size:1.05em;padding:.5em .9em;margin:.2em}input{width:100%;font-size:1.1em;padding:.5em;box-sizing:border-box}
#st{background:#eee;padding:.6em;border-radius:6px;white-space:pre-wrap}.on{background:#cfe8ff}
#nums span{display:inline-block;min-width:9em;margin-right:.6em}canvas{max-width:100%}</style>
<h2>Stack-chan</h2><div id="st">...</div>
<div style="display:flex;flex-wrap:wrap;gap:.8em;align-items:flex-start">
<div style="position:relative;width:320px;height:240px;background:#222;color:#aaa;flex:none">
<img id="cam" width="320" height="240" style="display:none;position:absolute;left:0;top:0">
<canvas id="ov" width="320" height="240" style="position:absolute;left:0;top:0"></canvas>
<div id="off" style="position:absolute;left:8px;top:8px">カメラ配信: オフ</div></div>
<div id="nums"><span id="n_fps"></span><span id="n_api"></span><span id="n_mic"></span><span id="n_rssi"></span></div></div>
<canvas id="dash" width="720" height="210" style="background:#fafafa;border:1px solid #ccc;margin:.5em 0"></canvas>
<p><button onclick="ask(0)">豆知識</button><button onclick="ask(1)">応援</button><button onclick="ask(2)">休憩</button></p>
<p><input id="q" maxlength="40" placeholder="自由に質問(40文字まで)"><button onclick="askText()">送信</button></p>
<p><button onclick="post('/cancel')">中止</button><button onclick="post('/touch')">タッチ反応</button><button onclick="post('/view')">本体の表示切替</button></p>
<p><button id="b_stream" onclick="post('/stream')">カメラ配信</button><button id="b_auto" onclick="opt('auto')">自律</button><button id="b_mute" onclick="opt('mute')">消音</button><button id="b_tts" onclick="opt('tts')">読み上げ</button><button id="b_mic" onclick="opt('mic')">マイク</button><button id="b_mode" onclick="opt('mode')">追従方式</button><button id="b_vision" onclick="opt('vision')">視覚(画像をクラウドへ送る)</button><button onclick="post('/look')">見て</button></p>
<script>
const t=new URLSearchParams(location.search).get('t')||'';
async function post(p,b){const r=await fetch(p+'?t='+encodeURIComponent(t),{method:'POST',body:b});if(!r.ok)document.getElementById('st').textContent='エラー '+r.status;}
function opt(n){post('/opt',new URLSearchParams({n:n}));}
function ask(i){post('/ask',new URLSearchParams({i:i}));}
function askText(){const q=document.getElementById('q').value.trim();if(q)post('/ask',new URLSearchParams({q:q}));}
let streaming=false,looping=false,box=null,n=0;
function drawBox(){const c=document.getElementById('ov').getContext('2d');c.clearRect(0,0,320,240);
if(streaming&&box&&box.age<600){c.strokeStyle='#0f0';c.lineWidth=2;c.strokeRect(box.x,box.y,box.s,box.s);}}
function loop(){const im=document.getElementById('cam');
if(!streaming){looping=false;im.style.display='none';document.getElementById('off').style.display='block';drawBox();return;}
looping=true;im.style.display='block';document.getElementById('off').style.display='none';
im.onload=()=>{drawBox();setTimeout(loop,250)};im.onerror=()=>setTimeout(loop,1000);
im.src='/snap?t='+encodeURIComponent(t)+'&n='+(n++);}
function mark(id,on){document.getElementById(id).className=on?'on':'';}
// dashboard: head angle and the request timeline, last WIN ms
const WIN=40000,samples=[],events=[];let lastSeq=-1,off=0;
const EV={start:['質問','#1f77b4'],reply_accepted:['回答','#2ca02c'],request_failed:['失敗','#d62728'],track_on:['顔あり','#9467bd'],
track_off:['顔なし','#999'],touch:['タッチ','#ff7f0e'],sound:['音','#e377c2'],auto_greet:['挨拶','#17becf'],auto_filler:['えーっと','#17becf'],
cancel_ok:['中止','#555'],auto_lonely:['だれか','#17becf']};
function draw(){const c=document.getElementById('dash'),g=c.getContext('2d'),W=c.width,H=c.height,L=44,B=60,now=Date.now(),x=ms=>L+(W-L-6)*(1-(now-ms)/WIN);
g.clearRect(0,0,W,H);const top=8,ph=H-B-top,y=a=>top+ph/2-(a/45)*(ph/2);
for(let i=1;i<samples.length;i++){if(samples[i-1].w){g.fillStyle='rgba(31,119,180,.14)';g.fillRect(x(samples[i-1].t),top,x(samples[i].t)-x(samples[i-1].t),ph);}}
g.strokeStyle='#ddd';g.fillStyle='#666';g.font='11px sans-serif';for(const a of[-45,0,45]){g.beginPath();g.moveTo(L,y(a));g.lineTo(W-6,y(a));g.stroke();g.fillText(a+'°',4,y(a)+4);}
function line(k,col,dash){g.beginPath();g.setLineDash(dash);g.strokeStyle=col;g.lineWidth=2;let f=true;for(const s of samples){if(now-s.t>WIN)continue;const px=x(s.t),py=y(s[k]);if(f){g.moveTo(px,py);f=false}else g.lineTo(px,py);}g.stroke();g.setLineDash([]);}
line('target','#999',[5,4]);line('yaw','#d62728',[]);
g.fillStyle='#d62728';g.fillText('首の角度',L+4,top+10);g.fillStyle='#999';g.fillText('目標',L+64,top+10);g.fillStyle='rgba(31,119,180,.5)';g.fillText('■ 回答待ち',L+100,top+10);
g.strokeStyle='#bbb';g.beginPath();g.moveTo(L,H-B+4);g.lineTo(W-6,H-B+4);g.stroke();
let k=0;for(const e of events){const d=EV[e.n];if(!d||now-e.t>WIN)continue;const px=x(e.t);g.strokeStyle=d[1];g.beginPath();g.moveTo(px,H-B+4);g.lineTo(px,H-B+10+(k%4)*11);g.stroke();g.fillStyle=d[1];g.fillText(d[0],px+2,H-B+20+(k%4)*11);k++;}
g.fillStyle='#666';g.fillText('-40秒',L,H-4);g.fillText('現在',W-30,H-4);}
async function poll(){try{const r=await fetch('/status?t='+encodeURIComponent(t));const j=await r.json();
const dn=Date.now();off=dn-j.now;samples.push({t:dn,yaw:j.yaw,target:j.target,w:j.state==='waiting'});while(samples.length&&dn-samples[0].t>WIN+2000)samples.shift();
for(const e of j.ev){if(e.s>lastSeq){lastSeq=e.s;events.push({t:e.ms+off,n:e.n,a:e.a});}}while(events.length&&dn-events[0].t>WIN+2000)events.shift();
document.getElementById('st').textContent='状態: '+j.state+' / 顔追従: '+(j.track?'あり':'なし')+'\n'+(j.answer||j.status||'');
document.getElementById('n_fps').textContent='検出 '+j.fps.toFixed(1)+' fps';document.getElementById('n_api').textContent='API '+j.api_ms+' ms';
document.getElementById('n_mic').textContent='音 '+j.mic+(j.mic_on?'':' (停止中)');document.getElementById('n_rssi').textContent='Wi-Fi '+j.rssi+' dBm';
mark('b_stream',j.stream);mark('b_auto',j.auto);mark('b_mute',j.mute);mark('b_tts',j.tts);mark('b_mic',j.mic_allowed);mark('b_vision',j.vision);document.getElementById('b_mode').textContent='追従方式: '+['顔検出','動き','顔→動き'][j.mode];
streaming=j.stream;box=j.box;if(streaming&&!looping)loop();if(!streaming)loop();}catch(e){}draw();}
setInterval(poll,500);poll();
</script></html>
)HTML";

static bool validPrompt(const String& s) {
  if (s.length() < 1 || s.length() > 120) return false;
  for (size_t i = 0; i < s.length(); ++i) if ((uint8_t)s[i] < 0x20) return false;
  return true;
}
static bool authorized() {
  if (!strlen(WEB_TOKEN) || web.arg("t") != WEB_TOKEN) { web.send(403, "text/plain", "forbidden"); return false; }
  return true;
}
static const char* stateEn(State s) {
  switch (s) {
    case State::idle: return "idle";
    case State::waiting: return "waiting";
    case State::answering: return "answering";
    case State::cancelled: return "cancelled";
    case State::error: return "error";
  }
  return "";
}
static void setupWeb() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html; charset=utf-8", kPage); });
  web.on("/status", HTTP_GET, [] {
    if (!authorized()) return;
    const auto o = core.output(millis());
    JsonDocument j;
    j["state"] = stateEn(o.state);
    j["track"] = o.track_head;
    j["x"] = o.normalized_x;
    j["answer"] = answer;
    j["status"] = status;
    j["ok"] = acceptedN;
    j["rejected"] = rejectedN;
    j["rssi"] = WiFi.RSSI();
    j["stream"] = streamOn;
    j["auto"] = autoOn;
    j["mute"] = muted;
    j["tts"] = ttsEnabled;
    j["vision"] = visionEnabled;
    j["vision_key"] = strlen(GEMINI_API_KEY) > 0;
    j["fault"] = (int)faultMode;
    j["now"] = millis();
    j["mode"] = trackMode;
    j["yaw"] = lastYaw / 10.0F;
    j["target"] = yawTarget / 10.0F;
    j["mic"] = (uint32_t)micLevel;
    j["mic_on"] = micOn;
    j["mic_allowed"] = micAllowed;
    j["fps"] = lastFps;
    j["api_ms"] = lastApiMs;
    JsonArray ev = j["ev"].to<JsonArray>();
    for (uint32_t k = evSeq > 32 ? evSeq - 32 : 0; k < evSeq; ++k) {
      const Ev& e = evBuf[k % 32];
      JsonObject o = ev.add<JsonObject>();
      o["s"] = k; o["ms"] = e.ms; o["n"] = e.name; o["a"] = e.a;
    }
    j["voice_clips"] = (bool)VOICE_CLIPS_AVAILABLE;
    JsonObject b = j["box"].to<JsonObject>();
    b["x"] = boxX; b["y"] = boxY; b["s"] = boxS;
    b["age"] = boxMs ? (long)(millis() - boxMs) : 999999L;
    String body; serializeJson(j, body);
    web.send(200, "application/json; charset=utf-8", body);
  });
  web.on("/ask", HTTP_POST, [] {
    if (!authorized()) return;
    const uint32_t now = millis();
    if (now - lastWebAskMs < 2000) { web.send(429, "text/plain", "slow down"); return; }
    bool ok = false;
    if (web.hasArg("q")) {
      const String q = web.arg("q");
      if (!validPrompt(q)) { web.send(400, "text/plain", "bad prompt"); return; }
      ok = startPrompt(q.c_str(), 9, now);
    } else {
      const int i = web.arg("i").toInt();
      if (i < 0 || i > 2) { web.send(400, "text/plain", "bad index"); return; }
      ok = startPrompt(kQuestions[i].prompt, i, now);
    }
    if (ok) { lastWebAskMs = now; SAY(ASK); }
    web.send(ok ? 200 : 500, "text/plain", ok ? "started" : "failed");
  });
  web.on("/look", HTTP_POST, [] {
    if (!authorized()) return;
    const uint32_t now = millis();
    if (!visionEnabled) { web.send(409, "text/plain", "vision off"); return; }
    if (now - lastWebAskMs < 2000) { web.send(429, "text/plain", "slow down"); return; }
    const bool ok = startLook(now);
    if (ok) { lastWebAskMs = now; SAY(ASK); }
    web.send(ok ? 200 : 500, "text/plain", ok ? "started" : "failed");
  });
  web.on("/cancel", HTTP_POST, [] {
    if (!authorized()) return;
    const bool ok = doCancel(millis());
    web.send(200, "text/plain", ok ? "cancelled" : "nothing to cancel");
  });
  // Toggle a runtime option: auto (autonomous behaviour), mute (speaker), tts (spoken answers).
  web.on("/opt", HTTP_POST, [] {
    if (!authorized()) return;
    const String n = web.arg("n");
    bool v;
    if (n == "auto") v = autoOn = !autoOn;
    else if (n == "mute") { v = muted = !muted; if (muted) M5.Speaker.stop(); }
    else if (n == "tts") v = ttsEnabled = !ttsEnabled;
    else if (n == "mic") v = micAllowed = !micAllowed;
    else if (n == "vision") v = visionEnabled = !visionEnabled;
    else if (n == "mode") { trackMode = (trackMode + 1) % 3; prevValid = false; v = trackMode != 0; }
    else if (n == "fault") { const int m = web.arg("v").toInt(); if (m < 0 || m > 3) { web.send(400, "text/plain", "bad fault"); return; } faultMode = m; v = m != 0; }
    else { web.send(400, "text/plain", "bad option"); return; }
    logEvent("opt", millis(), v ? 1 : 0);
    web.send(200, "text/plain", v ? "on" : "off");
  });
  web.on("/stream", HTTP_POST, [] {
    if (!authorized()) return;
    streamOn = !streamOn;
    logEvent(streamOn ? "stream_on" : "stream_off", millis());
    web.send(200, "text/plain", streamOn ? "on" : "off");
  });
  // One JPEG of the current camera frame. Taken inside loop() context via handleClient(), so
  // it does not race the detector; the frame is encoded, sent and never stored.
  web.on("/snap", HTTP_GET, [] {
    if (!authorized()) return;
    if (!streamOn) { web.send(409, "text/plain", "stream off"); return; }
    if (!cameraOk || !CoreS3.Camera.get()) { web.send(503, "text/plain", "no frame"); return; }
    uint8_t* jpg = nullptr;
    size_t len = 0;
    const bool ok = frame2jpg(CoreS3.Camera.fb, 60, &jpg, &len);
    CoreS3.Camera.free();
    if (!ok || !jpg) { web.send(500, "text/plain", "encode failed"); return; }
    web.sendHeader("Cache-Control", "no-store");
    web.setContentLength(len);
    web.send(200, "image/jpeg", "");
    web.client().write(jpg, len);
    free(jpg);
  });
  web.on("/view", HTTP_POST, [] {
    if (!authorized()) return;
    debugView = !debugView;
    web.send(200, "text/plain", debugView ? "camera" : "face");
  });
  web.on("/touch", HTTP_POST, [] {
    if (!authorized()) return;
    core.touch(millis());
    logEvent("touch", millis(), 1);
    web.send(200, "text/plain", "ok");
  });
  web.begin();
}

// Stack-chan's face. The expression comes from the core (out.face); the eyes follow the
// tracked face (out.normalized_x) and wander slowly when nobody is seen. The mouth moves
// while the robot is speaking.
static float gaze = 0;
static uint32_t nextBlinkMs = 3000, blinkUntilMs = 0;
static void drawFace(const stackchan::Output& out, uint32_t now, bool speaking) {
  auto& d = canvas;
  const int w = d.width(), cx = w / 2, ey = 74, er = 30, my = 132;
  const int ex[2] = {cx - 66, cx + 66};
  Face face = out.face;
  if (face == Face::neutral && now < happyUntilMs) face = Face::happy;
  const float target = out.track_head ? out.normalized_x : 0.25F * sinf(now / 2500.0F);
  gaze += (target - gaze) * 0.25F;
  if (now >= nextBlinkMs) { blinkUntilMs = now + 140; nextBlinkMs = now + 2500 + (now * 7) % 2500; }
  const bool blink = now < blinkUntilMs;
  int px = (int)(gaze * 13), py = 0;
  if (face == Face::thinking) { px = 11; py = -12; }
  for (int e = 0; e < 2; ++e) {
    const int x = ex[e];
    switch (face) {
      case Face::happy: d.fillArc(x, ey + 12, er - 9, er, 200, 340, TFT_WHITE); break;                 // ^ ^
      case Face::cancelled: d.fillRect(x - er, ey - 3, er * 2, 6, TFT_WHITE); break;
      case Face::error:
        for (int o = -1; o <= 1; ++o) {
          d.drawLine(x - er / 2 + o, ey - er / 2, x + er / 2 + o, ey + er / 2, TFT_RED);
          d.drawLine(x - er / 2 + o, ey + er / 2, x + er / 2 + o, ey - er / 2, TFT_RED);
        }
        break;
      default:
        if (blink) d.fillRect(x - er, ey - 3, er * 2, 6, TFT_WHITE);
        else { d.fillCircle(x, ey, er, TFT_WHITE); d.fillCircle(x + px, ey + py, er / 2, TFT_BLACK); }
    }
  }
  const bool talk = speaking || face == Face::answer;
  if (talk && face != Face::cancelled && face != Face::error) {
    d.fillEllipse(cx, my + 2, 20, 6 + (int)(5 + 5 * sinf(now / 110.0F)), TFT_WHITE);
    if (face == Face::happy) {
      d.fillCircle(ex[0] - 14, ey + 44, 9, 0xF810);
      d.fillCircle(ex[1] + 14, ey + 44, 9, 0xF810);
    }
    return;
  }
  switch (face) {
    case Face::happy:
      d.fillArc(cx, my - 16, 18, 30, 20, 160, TFT_WHITE);
      d.fillCircle(ex[0] - 14, ey + 44, 9, 0xF810);
      d.fillCircle(ex[1] + 14, ey + 44, 9, 0xF810);
      break;
    case Face::thinking:
      d.fillRect(cx - 14, my, 28, 5, TFT_WHITE);
      for (int i = 0; i < 3; ++i) if ((int)(now / 350) % 4 > i) d.fillCircle(cx + 64 + i * 16, 38, 5, TFT_CYAN);
      break;
    case Face::cancelled: d.fillRect(cx - 16, my, 32, 5, TFT_ORANGE); break;
    case Face::error: d.fillArc(cx, my + 26, 18, 28, 200, 340, TFT_RED); break;
    default: d.fillArc(cx, my - 14, 16, 22, 30, 150, TFT_WHITE);
  }
}

// Caption of what the robot says (or why it failed). The only text on the face screen.
static void drawCaption() {
  if (!answer[0] && !status[0]) return;
  auto& d = canvas;
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.fillRoundRect(6, 172, d.width() - 12, 62, 10, 0x2104);
  d.setTextColor(answer[0] ? TFT_WHITE : TFT_ORANGE, 0x2104);
  d.setCursor(14, 178);
  d.print(answer[0] ? answer : status);
}

// Operator status bar, shown only in the camera/debug view.
static void drawStatusBar(const stackchan::Output& out) {
  auto& d = canvas;
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.fillRect(0, 0, d.width(), 22, TFT_BLACK);
  d.fillCircle(11, 11, 8, faceColor(out.face));
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.setCursor(26, 3);
  d.printf("%s %s%s%s%s", stateJa(out.state), out.track_head ? "顔追従" : "顔なし", wifiOk ? "" : " 圏外",
           streamOn ? " 配信中" : "", autoOn ? " 自律" : "");
}

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);   // never block the control loop on log output when nobody reads the USB port
  M5.Speaker.setVolume(200);
  auto& d = M5StackChan.Display();
  d.setFont(&fonts::lgfxJapanGothic_16);
  d.setTextColor(TFT_WHITE, TFT_BLACK);
  d.fillScreen(TFT_BLACK);
  canvas.setPsram(true);
  canvas.createSprite(d.width(), d.height());
  canvas.fillScreen(TFT_BLACK);
  ttsBuf = (uint8_t*)ps_malloc(kTtsMaxBytes);
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
  Serial.printf("setup wifi=%d key_set=%d tts_key_set=%d tts_buf=%d voice_clips=%d free_heap=%u free_psram=%u\n", (int)wifiOk,
                (int)(strlen(SARASHINA_API_KEY) > 0), (int)(strlen(OPENAI_API_KEY) > 0), (int)(ttsBuf != nullptr),
                (int)VOICE_CLIPS_AVAILABLE, (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
  if (wifiOk) {
    MDNS.begin("stackchan");
    MDNS.addService("http", "tcp", 80);
    setupWeb();
    Serial.printf("web http://%s/?t=%s\n", WiFi.localIP().toString().c_str(), WEB_TOKEN);
  }
  cameraOk = CoreS3.Camera.begin();
  Serial.printf("camera %s\n", cameraOk ? "OK" : "FAIL");
  if (!wifiOk) snprintf(status, sizeof(status), "ネットワークなし");
  SAY(BOOT);
  happyUntilMs = millis() + 2500;
}

static uint32_t tLoopMax = 0, tWebMax = 0, tCamMax = 0, tServoMax = 0, tDrawMax = 0;   // per-window worst case, ms
static inline void trackMax(uint32_t& m, uint32_t t0) { const uint32_t d = millis() - t0; if (d > m) m = d; }

void loop() {
  const uint32_t loopStart = millis();
  M5StackChan.update();
  if (wifiOk) { const uint32_t t0 = millis(); web.handleClient(); trackMax(tWebMax, t0); }
  uint32_t now = millis();
  const uint32_t camStart = now;

  // Camera -> display -> detector. Capture time is taken when the driver returns the frame.
  bool present = false;
  float x = 0;
  if (cameraOk && CoreS3.Camera.get()) {
    const uint32_t captured = millis();
    auto* fb = CoreS3.Camera.fb;
    if (debugView) canvas.pushImage(0, 0, fb->width, fb->height, (uint16_t*)fb->buf);
    toGray((const uint16_t*)fb->buf, fb->width);
    CoreS3.Camera.free();
    float rcsq[4 * 16];
    int n = 0;
    if (trackMode != 1) {
      n = find_objects(rcsq, 16, (void*)facefinder_cascade, 0.0F, gray, kH, kW, kW, 1.2F, 0.15F, 24.0F, 120.0F);
      n = cluster_detections(rcsq, n);
    }
    float mxv = 0, mq = 0;
    int mbx = 0, mby = 0, mbs = 0;
    const bool moved = trackMode != 0 && detectMotion(mxv, mbx, mby, mbs, mq, millis());
    int best = -1;
    for (int i = 0; i < n; ++i)
      if (rcsq[4 * i + 3] >= kQualityMin && (best < 0 || rcsq[4 * i + 3] > rcsq[4 * best + 3])) best = i;
    if (best >= 0) {
      present = true;
      bestQ = rcsq[4 * best + 3];
      if (bestQ > windowMaxQ) windowMaxQ = bestQ;
      x = rcsq[4 * best + 1] / kW * 2.0F - 1.0F;
      ++faceFrames;
      const float row = rcsq[4 * best], col = rcsq[4 * best + 1], s = rcsq[4 * best + 2];
      boxX = (int)((col - s / 2) * 2); boxY = (int)((row - s / 2) * 2); boxS = (int)s * 2; boxMs = millis();
      if (debugView) canvas.drawRect((int)((col - s / 2) * 2), (int)((row - s / 2) * 2), (int)s * 2, (int)s * 2, TFT_GREEN);
    }
    if (!present && moved) {
      present = true;
      bestQ = mq;
      if (bestQ > windowMaxQ) windowMaxQ = bestQ;
      x = mxv;
      ++faceFrames;
      boxX = mbx; boxY = mby; boxS = mbs; boxMs = millis();
      if (debugView) canvas.drawRect(mbx, mby, mbs, mbs, TFT_YELLOW);
    }
    ++frames;
    now = millis();
    // Only detections are reported. A missed frame is not evidence that the face left: the
    // core's observation TTL decides when tracking stops.
    if (present) {
      smoothX = smoothValid ? 0.5F * smoothX + 0.5F * x : x;
      smoothValid = true;
      core.observe(smoothX, true, captured, now);
      lastX = smoothX;
      // Integral control: turn a little toward the face on every new detection, so the
      // face ends up centred. Applying it per detection (not per servo tick) avoids
      // integrating one stale observation several times.
      if (fabsf(smoothX) > kDeadband)
        yawTarget = constrain(yawTarget + kYawSign * (int)(smoothX * kYawGain), -kYawMax, kYawMax);
    }
  }

  trackMax(tCamMax, camStart);

  // Touch: a tap tickles the robot; holding for kLongPressMs cancels / silences it.
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) { pressing = true; longDone = false; pressMs = now; }
  if (pressing && !longDone && t.isPressed() && now - pressMs >= kLongPressMs) { longDone = true; doCancel(now); }
  if (pressing && t.wasReleased()) {
    pressing = false;
    if (!longDone) {
      core.touch(now);
      logEvent("touch", now);
      if (now - lastTouchVoiceMs > 4000 && !M5.Speaker.isPlaying()) { SAY(TOUCH); lastTouchVoiceMs = now; }
    }
  }

  // API results. The core decides whether a result is still wanted.
  Res res;
  while (xQueueReceive(resQ, &res, 0) == pdTRUE) {
    now = millis();
    lastApiMs = res.ms + res.ttsMs;
    logEvent("api_done", now, res.http, (long)res.ms);
    bool played = false;
    if (res.ok) {
      if (core.reply(res.id, now)) {
        strncpy(answer, res.text, sizeof(answer) - 1);
        status[0] = 0;
        answerStartMs = now;
        ++acceptedN;
        logEvent("reply_accepted", now, (long)res.id, (long)strlen(answer));
        SAY(ANSWER);
        if (res.pcmBytes && !muted) {
          useSpeaker();
          M5.Speaker.playRaw((const int16_t*)ttsBuf, res.pcmBytes / 2, 24000, false, 1, -1, false);
          ttsPlaying = true;
          ttsPlayMs = now;
          played = true;
        }
        logEvent("tts", now, res.ttsHttp, (long)res.pcmBytes);
      } else { ++rejectedN; logEvent("reply_rejected_stale", now, (long)res.id); }
    } else if (core.fail(res.id, now)) {
      snprintf(status, sizeof(status), "通信エラー(%d)", res.http);
      logEvent("request_failed", now, (long)res.id, res.http);
      SAY(ERROR);
    } else { ++rejectedN; logEvent("failure_rejected_stale", now, (long)res.id); }
    if (res.pcmBytes && !played) ttsHeld = false;     // audio that will not be played: free the buffer
  }
  if (ttsPlaying && now - ttsPlayMs > 300 && !M5.Speaker.isPlaying()) { ttsPlaying = false; ttsHeld = false; }

  now = millis();
  auto out = core.output(now);
  const bool speaking = M5.Speaker.isPlaying();
  if (out.state == State::answering && now - answerStartMs > kAnswerShowMs && !speaking && !ttsHeld) {
    core.finish(now);
    out = core.output(now);
  }
  if (out.state == State::error && !status[0]) { snprintf(status, sizeof(status), "時間切れ"); }
  if (out.state == State::error && !timeoutSaid && strcmp(status, "時間切れ") == 0) { timeoutSaid = true; SAY(TIMEOUT); }
  if (out.state == State::idle && answer[0] && !speaking) answer[0] = 0;
  if (out.state == State::idle && status[0] && now - answerStartMs > kAnswerShowMs + 3000) status[0] = 0;

  // Microphone: listen while silent; a sudden loud sound makes the robot react (face, voice),
  // also while it waits for the cloud. Nothing is recorded, stored or sent: only a level.
  if (speaking || ttsPlaying) lastAudioMs = now;
  if (!micOn && micAllowed && !micFailed && !speaking && !ttsPlaying && now - lastAudioMs > 600) {
    startMic();
    logEvent(micOn ? "mic_on" : "mic_failed", now);
  }
  if (micOn && !micAllowed) { M5.Mic.end(); micOn = false; }
  // The servo motor is loud: sounds in the 1.2 s after a head command are ignored (observed 2026-10-30).
  if (micOn && now - micStartMs > 700 && now - lastMoveMs > 1200) {
    const uint32_t lvl = micLevel;
    if (lvl < micFloor * 2 + 40) micFloor += ((float)lvl - micFloor) * 0.01F;
    const float thr = micFloor * 3.0F + 250.0F;
    if (lvl > thr && now - lastSoundMs > 1500) {
      lastSoundMs = now;
      core.touch(now);
      logEvent("sound", now, (long)lvl, (long)thr);
      ++soundN;
      lastSoundSeenMs = now;
      if (out.state != State::waiting && now - lastListenMs > 6000) {
        lastListenMs = now;
        if (lvl > micFloor * 8.0F + 1200.0F) { SAY(LOUD); } else { SAY(LISTEN); }
      }
    }
  }

  // Autonomous behaviour: the Director decides, this loop performs.
  const bool netUp = wifiOk && WiFi.status() == WL_CONNECTED;
  switch (director.step(now, out.track_head, out.state, autoOn && netUp)) {
    case Act::greet:
      happyUntilMs = now + 1800;
      SAY(FOUND);
      logEvent("auto_greet", now);
      break;
    case Act::ask: {
      if (visionEnabled && strlen(GEMINI_API_KEY)) {
        if (startLook(now)) { SAY(ASK); logEvent("auto_look", now); }
        break;
      }
      const char* p = kAutoPrompts[autoIndex++ % (sizeof(kAutoPrompts) / sizeof(kAutoPrompts[0]))];
      char full[sizeof(Req::prompt)];
      char scene[112];
      sceneText(scene, sizeof(scene), now);
      snprintf(full, sizeof(full), "%s%s", scene, p);
      if (startPrompt(full, 20 + (int)((autoIndex - 1) % 10), now)) { SAY(ASK); logEvent("auto_ask", now); }
      break;
    }
    case Act::filler:
      if (director.fillers_said() == 1) { SAY(FILLER1); } else { SAY(FILLER2); }
      logEvent("auto_filler", now, director.fillers_said());
      break;
    case Act::lonely:
      SAY(LONELY);
      logEvent("auto_lonely", now);
      break;
    case Act::none:
      break;
  }

  // Head: follow a fresh face observation; hold briefly after losing it; then glance around
  // (autonomous mode) or return to centre.
  if (now - lastServoMs >= kServoIntervalMs) {
    lastServoMs = now;
    if (out.track_head != prevTrack) {
      logEvent(out.track_head ? "track_on" : "track_off", now, (long)(out.normalized_x * 100), (long)(bestQ * 10));
      prevTrack = out.track_head;
      if (out.track_head) trackStartMs = now;
      if (!out.track_head) smoothValid = false;
    }
    if (out.track_head) { ++trackFrames; lastTrackMs = now; }
    if (!out.track_head && now - lastTrackMs > kHoldMs) {
      if (autoOn && out.state == State::idle) {
        if (now >= nextWanderMs) {
          yawTarget = (int)random(-kWanderYaw, kWanderYaw + 1);
          nextWanderMs = now + (uint32_t)random(3500, 7000);
        }
      } else {
        yawTarget = 0;
      }
    }
    const int yaw = yawTarget;
    if (abs(yaw - lastYaw) >= 5) {
      M5StackChan.Motion.moveX(yaw, kServoSpeed);
      lastMoveMs = now;
      logEvent("servo_yaw", now, yaw, (long)(out.normalized_x * 100));
      lastYaw = yaw;
      ++servoCmds;
    }
  }

  const uint32_t drawStart = millis();
  if (!debugView) canvas.fillScreen(TFT_BLACK);
  if (!debugView) drawFace(out, now, speaking);
  else drawStatusBar(out);
  drawCaption();
  canvas.pushSprite(0, 0);
  trackMax(tDrawMax, drawStart);
  trackMax(tLoopMax, loopStart);

  if (now - lastFaceLogMs >= 5000) {
    Serial.printf("device_monotonic_ms=%lu fps=%.1f face_frames=%lu/%lu track_ticks=%lu servo_cmds=%lu maxq=%.1f x=%.2f ok=%lu rej=%lu free_heap=%u max_ms loop=%lu web=%lu cam=%lu draw=%lu\n",
                  (unsigned long)now, frames * 1000.0F / (now - lastFaceLogMs), (unsigned long)faceFrames,
                  (unsigned long)frames, (unsigned long)trackFrames, (unsigned long)servoCmds, windowMaxQ, lastX,
                  (unsigned long)acceptedN, (unsigned long)rejectedN, (unsigned)ESP.getFreeHeap(),
                  (unsigned long)tLoopMax, (unsigned long)tWebMax, (unsigned long)tCamMax, (unsigned long)tDrawMax);
    lastFps = frames * 1000.0F / (now - lastFaceLogMs);
    Serial.printf("device_monotonic_ms=%lu mic on=%d level=%lu window_max=%lu floor=%.0f\n", (unsigned long)now, (int)micOn,
                  (unsigned long)micLevel, (unsigned long)micWindowMax, micFloor);
    micWindowMax = 0;
    frames = faceFrames = trackFrames = servoCmds = 0;
    windowMaxQ = 0;
    tLoopMax = tWebMax = tCamMax = tDrawMax = 0;
    lastFaceLogMs = now;
  }
}
