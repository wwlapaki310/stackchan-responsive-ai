// Minimal K151/CoreS3 bring-up: screen + touch only. No servo, camera or network.
#include <M5Unified.h>

static uint32_t touchCount = 0;

static void draw(const char* line2) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("stackchan bring-up");
  M5.Display.printf("touches: %lu\n", (unsigned long)touchCount);
  M5.Display.println(line2);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.printf("boot ms=%lu w=%d h=%d\n", (unsigned long)millis(), (int)M5.Display.width(), (int)M5.Display.height());
  draw("touch the screen");
}

void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    ++touchCount;
    char buf[48];
    snprintf(buf, sizeof(buf), "x=%d y=%d", (int)t.x, (int)t.y);
    draw(buf);
    Serial.printf("touch ms=%lu n=%lu x=%d y=%d\n", (unsigned long)millis(), (unsigned long)touchCount, (int)t.x, (int)t.y);
  }
  delay(10);
}
