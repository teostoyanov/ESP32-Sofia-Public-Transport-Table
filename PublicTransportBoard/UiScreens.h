#pragma once

// Builds the full-screen route carousel UI and starts its own periodic
// refresh/cycling timer. Call once from setup(), after Lvgl_Init().
void Ui_Init();
