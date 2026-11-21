// SPDX-License-Identifier: AGPL-3.0-or-later
// app_controller.hpp - the WeDo 2.0 controller application.
//
// Keys (arrows are the keys with the arrow print: ; , . /):
//   up / down      run every attached motor forward / reverse while held
//   f / b          keep running forward / backward; same key again stops
//   left / right   power -10 / +10 (10..100)
//   space          stop motors and buzzer
//   1..9, 0        hub LED colour preset, 0 = off
//   q w e r t y u i   notes C5..C6 on the Cardputer speaker while held
//   - / =          speaker volume down / up
//
// Two tasks, both for core 1 (the BLE host and controller run on core 0):
//   keyboard_task  samples the key matrix every 10 ms, posts key events
//   control_task   owns the hub session and the screen (screen.hpp)

#pragma once

namespace app {

/** Display, keyboard, event queue, BLE stack. Call once before the tasks start. */
bool init();

void keyboard_task(void *arg);
void control_task(void *arg);

}  // namespace app
