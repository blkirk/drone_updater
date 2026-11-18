#include "led.h"

#include <Arduino.h>

#include "status.h"

namespace led {

namespace {

struct Rgb { uint8_t r, g, b; };
constexpr Rgb kOff    = {0, 0, 0};
constexpr Rgb kBlue   = {0, 0, 40};
constexpr Rgb kGreen  = {0, 40, 0};
constexpr Rgb kRed    = {50, 0, 0};
constexpr Rgb kYellow = {40, 30, 0};
constexpr Rgb kCyan   = {0, 30, 30};

Rgb s_last = {1, 1, 1};  // force first write

void show(bool on, Rgb color) {
  Rgb c = on ? color : kOff;
  if (c.r == s_last.r && c.g == s_last.g && c.b == s_last.b) return;
  s_last = c;
#if defined(RGB_LED_PIN)
  neopixelWrite(RGB_LED_PIN, c.r, c.g, c.b);
#elif defined(LED_PIN)
  bool lit = c.r || c.g || c.b;
  #if LED_ACTIVE_LOW
  digitalWrite(LED_PIN, lit ? LOW : HIGH);
  #else
  digitalWrite(LED_PIN, lit ? HIGH : LOW);
  #endif
#endif
}

// `period` ms cycle, lit for the first `on_ms`.
bool blink(uint32_t now, uint32_t period, uint32_t on_ms) { return (now % period) < on_ms; }

void render(uint32_t now) {
  using status::Phase;
  switch (status::phase) {
    case Phase::kIdleNoFw:
      show(blink(now, 1000, 500), kBlue);
      break;
    case Phase::kIdleReady:
      show(blink(now, 2000, 80), kBlue);
      break;
    case Phase::kCountdown: {
#if defined(RGB_LED_PIN)
      show(blink(now, 1000, 500), kYellow);
#else
      uint32_t t = now % 1000;  // two 80 ms flashes
      show(t < 80 || (t >= 200 && t < 280), kYellow);
#endif
      break;
    }
    case Phase::kScanning:
      show(blink(now, 400, 200), kBlue);
      break;
    case Phase::kConnecting:
      show(true, kCyan);
      break;
    case Phase::kStreaming: {
      uint8_t p = status::pct;
      if (p >= 100) { show(true, kGreen); break; }
      uint32_t half = 600 - ((uint32_t)p * (600 - 30)) / 100;  // 600..30 ms
      show(blink(now, half * 2, half), kGreen);
      break;
    }
    case Phase::kDoneOk:
      show(true, kGreen);
      break;
    case Phase::kDoneFail:
#if defined(RGB_LED_PIN)
      show(true, kRed);
#else
      {
        uint32_t t = now % 1500;  // three 100 ms flashes then a pause
        show(t < 600 && (t % 200) < 100, kRed);
      }
#endif
      break;
  }
}

void task(void*) {
  for (;;) {
    render(millis());
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

void begin() {
#if defined(LED_PIN) && !defined(RGB_LED_PIN)
  pinMode(LED_PIN, OUTPUT);
#endif
  show(false, kOff);
  xTaskCreate(task, "led", 2048, nullptr, 1, nullptr);
}

}  // namespace led
