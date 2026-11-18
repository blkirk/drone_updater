#pragma once

// Status LED driven from its own FreeRTOS task, so it keeps animating while
// the main loop blocks inside scan / DFU. Reads status::phase and status::pct.
//
//   Phase        S3 Super Mini (WS2812)        C3 Super Mini (single blue LED)
//   idle no fw   blue slow blink               1 Hz blink
//   ready        blue short flash every 2 s    short flash every 2 s
//   countdown    yellow pulse 1 Hz             double-blink every 1 s
//   scanning     blue fast blink               2.5 Hz blink
//   connecting   cyan solid                    solid on
//   streaming    green, blink speeds up w/ %   blink speeds up w/ %
//   done ok      green solid                   solid on
//   done fail    red solid                     triple-flash, pause

namespace led {

void begin();

}  // namespace led
