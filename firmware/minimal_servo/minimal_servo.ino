// Minimal K151 head-servo bring-up. Moves only after the user touches the top sensor.
// Small, slow yaw sweep (+-15 deg at speed 200); pitch is not driven here.
// Angle unit: 10 = 1 deg. Yaw -1280..1280, pitch 0..900 (M5 advises keeping pitch within 5..85 deg).
#include <Arduino.h>
#include <M5StackChan.h>

static constexpr int kYawSwing = 150;
static constexpr int kSpeed = 200;
static uint32_t runs = 0;

static void say(const char* msg) {
  M5StackChan.Display().printf("> %s\n", msg);
  Serial.printf("ms=%lu %s yaw=%d pitch=%d\n", (unsigned long)millis(), msg,
                M5StackChan.Motion.getCurrentYawAngle(), M5StackChan.Motion.getCurrentPitchAngle());
}

void setup() {
  M5StackChan.begin();
  Serial.begin(115200);
  M5StackChan.Display().setTextSize(2);
  M5StackChan.Display().setTextScroll(true);
  M5StackChan.Display().setTextColor(TFT_YELLOW);
  M5StackChan.Display().printf("> Touch the top to sweep\n");
  M5StackChan.Display().setTextColor(TFT_GREENYELLOW);
  say("boot");
}

void loop() {
  M5StackChan.update();
  if (M5StackChan.TouchSensor.wasPressed()) {
    delay(200);
    ++runs;
    M5StackChan.Motion.goHome(kSpeed);
    say("home");
    delay(2500);
    M5StackChan.Motion.moveX(kYawSwing, kSpeed);
    say("yaw +15");
    delay(2500);
    M5StackChan.Motion.moveX(-kYawSwing, kSpeed);
    say("yaw -15");
    delay(2500);
    M5StackChan.Motion.goHome(kSpeed);
    say("home");
    delay(2500);
  }
  delay(100);
}
