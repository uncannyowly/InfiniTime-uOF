#include "displayapp/screens/Car.h"

#include <algorithm>
#include <cstdio>
#include "components/car/ObdData.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr lv_coord_t screenSize = 240;

  constexpr uint32_t colorAccent = 0x00d5ff;
  constexpr uint32_t colorSpeed = 0x33ff88;
  constexpr uint32_t colorBoost = 0xff9933;
  constexpr uint32_t colorVac = 0x00d5ff;
  constexpr uint32_t colorWarn = 0xff3344;
  constexpr uint32_t colorCaption = 0x808080;
  constexpr uint32_t colorPanel = 0x1c1c1c;

  int16_t MapPressureToBoost(const Pinetime::Controllers::ObdData& data, bool imperial, char unit[4]) {
    // Positive = boost, negative = vacuum, relative to barometric pressure.
    const int diffKpa = static_cast<int>(data.mapKpa) - static_cast<int>(data.barometerKpa);
    if (diffKpa >= 0) {
      snprintf(unit, 4, "PSI");
      return imperial ? static_cast<int16_t>(diffKpa * 145 / 1000) : static_cast<int16_t>(diffKpa);
    }
    snprintf(unit, 4, "HG");
    // vacuum shown in inHg (positive magnitude)
    return static_cast<int16_t>(-diffKpa * 2953 / 10000);
  }

  void StyleLabel(lv_obj_t* label, const lv_font_t* font, uint32_t color) {
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  }

  void MenuButtonHandler(lv_obj_t* obj, lv_event_t event) {
    if (event != LV_EVENT_CLICKED) {
      return;
    }
    auto* item = static_cast<Car::MenuItem*>(obj->user_data);
    if (item != nullptr) {
      item->self->OnMenuButton(item->view);
    }
  }
}

Car::Car() {
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  SwitchTo(View::Menu);
}

Car::~Car() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void Car::OnMenuButton(uint8_t view) {
  if (view == toggleDemo) {
    demoMode = !demoMode;
    viewDirty = true; // rebuild the menu so the chip updates
    return;
  }
  pendingView = static_cast<View>(view);
  viewDirty = true;
}

const Pinetime::Controllers::ObdData& Car::Data() const {
  return demoMode ? obd.Current() : disconnected;
}

void Car::SwitchTo(View view) {
  pendingView = view;
  viewDirty = true;
}

bool Car::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // Swipe down returns to the menu from any dial; on the menu, let the OS handle it (exit).
  if (event == TouchEvents::SwipeDown && currentView != View::Menu) {
    SwitchTo(View::Menu);
    return true;
  }
  return false;
}

lv_obj_t* Car::CreateStatTile(lv_coord_t x, lv_coord_t y, const char* caption, uint32_t color, lv_obj_t** valueLabel) {
  constexpr lv_coord_t w = 76;
  constexpr lv_coord_t h = 42;
  lv_obj_t* tile = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(tile, false);
  lv_obj_set_size(tile, w, h);
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_style_local_bg_color(tile, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPanel));
  lv_obj_set_style_local_border_width(tile, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_border_color(tile, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  lv_obj_set_style_local_radius(tile, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 3);

  lv_obj_t* captionLabel = lv_label_create(tile, nullptr);
  StyleLabel(captionLabel, &jetbrains_mono_bold_14, colorCaption);
  lv_label_set_text_static(captionLabel, caption);
  lv_obj_align(captionLabel, nullptr, LV_ALIGN_IN_TOP_LEFT, 4, 2);

  *valueLabel = lv_label_create(tile, nullptr);
  StyleLabel(*valueLabel, &jetbrains_mono_bold_20, color);
  lv_label_set_text_static(*valueLabel, "--");
  lv_obj_align(*valueLabel, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, 4, -2);
  return tile;
}

void Car::BuildMenu() {
  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(title, true);
  lv_label_set_text_fmt(title, "#00d5ff %s# CAR", Symbols::car);
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_LEFT, 8, 8);

  // adapter status line
  statusLabel = lv_label_create(lv_scr_act(), nullptr);
  if (demoMode) {
    StyleLabel(statusLabel, &jetbrains_mono_bold_14, colorBoost);
    lv_label_set_text_static(statusLabel, "DEMO DATA");
  } else {
    StyleLabel(statusLabel, &jetbrains_mono_bold_14, colorWarn);
    lv_label_set_text_static(statusLabel, "NO ADAPTER");
  }
  lv_obj_align(statusLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 200);

  // DEMO toggle chip, top-right
  menuItems[3] = {this, toggleDemo};
  lv_obj_t* chip = lv_btn_create(lv_scr_act(), nullptr);
  lv_obj_set_size(chip, 74, 26);
  lv_obj_align(chip, nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, 6);
  const uint32_t chipColor = demoMode ? colorBoost : colorCaption;
  lv_obj_set_style_local_bg_color(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(demoMode ? colorBoost : colorPanel));
  lv_obj_set_style_local_border_width(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_border_color(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(chipColor));
  lv_obj_set_style_local_radius(chip, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 13);
  chip->user_data = &menuItems[3];
  lv_obj_set_event_cb(chip, MenuButtonHandler);
  lv_obj_t* chipLabel = lv_label_create(chip, nullptr);
  StyleLabel(chipLabel, &jetbrains_mono_bold_14, demoMode ? 0x000000 : chipColor);
  lv_label_set_text_static(chipLabel, demoMode ? "DEMO ON" : "DEMO");

  const char* labels[3] = {"SPEED", "BOOST / VAC", "HUD"};
  const uint32_t colors[3] = {colorSpeed, colorBoost, colorAccent};
  for (uint8_t i = 0; i < 3; i++) {
    menuItems[i] = {this, static_cast<uint8_t>(static_cast<uint8_t>(View::Speed) + i)};
    lv_obj_t* btn = lv_btn_create(lv_scr_act(), nullptr);
    lv_obj_set_size(btn, screenSize - 40, 42);
    lv_obj_align(btn, nullptr, LV_ALIGN_IN_TOP_MID, 0, 44 + i * 50);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPanel));
    lv_obj_set_style_local_border_width(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_style_local_border_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colors[i]));
    lv_obj_set_style_local_radius(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, 6);
    btn->user_data = &menuItems[i];
    lv_obj_set_event_cb(btn, MenuButtonHandler);

    lv_obj_t* label = lv_label_create(btn, nullptr);
    StyleLabel(label, &jetbrains_mono_bold_20, colors[i]);
    lv_label_set_text_static(label, labels[i]);
  }
}

void Car::BuildSpeed() {
  static lv_color_t needleColor = LV_COLOR_WHITE;
  speedGauge = lv_gauge_create(lv_scr_act(), nullptr);
  lv_gauge_set_needle_count(speedGauge, 1, &needleColor);
  lv_gauge_set_range(speedGauge, 0, 220);
  lv_gauge_set_scale(speedGauge, 260, 23, 12);
  lv_gauge_set_critical_value(speedGauge, 160);
  lv_obj_set_size(speedGauge, 200, 200);
  lv_obj_align(speedGauge, nullptr, LV_ALIGN_IN_TOP_MID, 0, 6);
  lv_obj_set_style_local_scale_grad_color(speedGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorSpeed));
  lv_obj_set_style_local_scale_end_color(speedGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorWarn));
  lv_obj_set_style_local_text_color(speedGauge, LV_GAUGE_PART_MAJOR, LV_STATE_DEFAULT, LV_COLOR_WHITE);

  speedValue = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(speedValue, &jetbrains_mono_42, colorSpeed);
  lv_label_set_text_static(speedValue, "0");

  speedUnit = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(speedUnit, &jetbrains_mono_bold_14, colorCaption);
  lv_label_set_text_static(speedUnit, "KM/H");

  rpmValue = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(rpmValue, &jetbrains_mono_bold_14, colorAccent);
  lv_label_set_text_static(rpmValue, "0 RPM");
  lv_obj_align(rpmValue, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -14);

  rpmBar = lv_bar_create(lv_scr_act(), nullptr);
  lv_obj_set_size(rpmBar, screenSize - 40, 6);
  lv_obj_align(rpmBar, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -4);
  lv_bar_set_range(rpmBar, 0, 7000);
  lv_obj_set_style_local_bg_color(rpmBar, LV_BAR_PART_BG, LV_STATE_DEFAULT, lv_color_hex(colorPanel));
  lv_obj_set_style_local_bg_color(rpmBar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(colorAccent));
}

void Car::BuildBoost() {
  static lv_color_t needleColor = LV_COLOR_WHITE;
  boostGauge = lv_gauge_create(lv_scr_act(), nullptr);
  lv_gauge_set_needle_count(boostGauge, 1, &needleColor);
  // -30 inHg vacuum .. +25 psi boost, zero at centre
  lv_gauge_set_range(boostGauge, -30, 25);
  lv_gauge_set_scale(boostGauge, 260, 23, 12);
  lv_gauge_set_critical_value(boostGauge, 18);
  lv_obj_set_size(boostGauge, 200, 200);
  lv_obj_align(boostGauge, nullptr, LV_ALIGN_IN_TOP_MID, 0, 6);
  lv_obj_set_style_local_scale_grad_color(boostGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorBoost));
  lv_obj_set_style_local_scale_end_color(boostGauge, LV_GAUGE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorWarn));
  lv_obj_set_style_local_text_color(boostGauge, LV_GAUGE_PART_MAJOR, LV_STATE_DEFAULT, LV_COLOR_WHITE);

  boostValue = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(boostValue, &jetbrains_mono_42, colorBoost);
  lv_label_set_text_static(boostValue, "0");

  boostUnit = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(boostUnit, &jetbrains_mono_bold_14, colorCaption);
  lv_label_set_text_static(boostUnit, "PSI");

  lv_obj_t* hint = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(hint, &jetbrains_mono_bold_14, colorCaption);
  lv_label_set_text_static(hint, "VAC <    > BOOST");
  lv_obj_align(hint, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -6);
}

void Car::BuildHud() {
  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  StyleLabel(title, &jetbrains_mono_bold_14, colorAccent);
  lv_label_set_text_static(title, "HUD");
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 2);

  const lv_coord_t left = 4;
  const lv_coord_t right = screenSize - 80;
  lv_coord_t y = 20;
  CreateStatTile(left, y, "COOLANT", colorSpeed, &hudCoolant);
  CreateStatTile(right, y, "INTAKE", colorAccent, &hudIntake);
  y += 48;
  CreateStatTile(left, y, "RPM", colorAccent, &hudRpm);
  CreateStatTile(right, y, "THROTTLE", colorBoost, &hudThrottle);
  y += 48;
  CreateStatTile(left, y, "O2 B1S1", colorVac, &hudO2);
  CreateStatTile(right, y, "BATTERY", colorSpeed, &hudBattery);
  y += 48;
  CreateStatTile(left, y, "SPEED", colorSpeed, &hudSpeed);
  CreateStatTile(right, y, "MAP KPA", colorBoost, &hudMap);
}

void Car::Refresh() {
  if (viewDirty) {
    viewDirty = false;
    lv_obj_clean(lv_scr_act());
    speedGauge = boostGauge = nullptr;
    hudCoolant = nullptr;
    currentView = pendingView;
    switch (currentView) {
      case View::Menu:
        BuildMenu();
        break;
      case View::Speed:
        BuildSpeed();
        break;
      case View::Boost:
        BuildBoost();
        break;
      case View::Hud:
        BuildHud();
        break;
    }
  }

  if (demoMode) {
    obd.Update();
  }

  switch (currentView) {
    case View::Speed:
      RefreshSpeed();
      break;
    case View::Boost:
      RefreshBoost();
      break;
    case View::Hud:
      RefreshHud();
      break;
    case View::Menu:
      break;
  }
}

void Car::RefreshSpeed() {
  const auto& data = Data();
  lv_gauge_set_value(speedGauge, 0, data.connected ? std::min<int>(data.speedKmh, 220) : 0);
  if (data.connected) {
    lv_label_set_text_fmt(speedValue, "%d", data.speedKmh);
    lv_label_set_text_fmt(rpmValue, "%d RPM", data.rpm);
  } else {
    lv_label_set_text_static(speedValue, "--");
    lv_label_set_text_static(rpmValue, "NO ADAPTER");
  }
  lv_obj_align(speedValue, speedGauge, LV_ALIGN_CENTER, 0, 6);
  lv_obj_align(speedUnit, speedValue, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
  lv_bar_set_value(rpmBar, data.connected ? std::min<int>(data.rpm, 7000) : 0, LV_ANIM_OFF);
}

void Car::RefreshBoost() {
  const auto& data = Data();
  if (data.connected) {
    char unit[4];
    const int16_t value = MapPressureToBoost(data, true, unit);
    lv_gauge_set_value(boostGauge, 0, std::clamp<int>(value, -30, 25));
    lv_label_set_text_fmt(boostValue, "%d", value);
    lv_label_set_text_static(boostUnit, unit);
  } else {
    lv_gauge_set_value(boostGauge, 0, 0);
    lv_label_set_text_static(boostValue, "--");
    lv_label_set_text_static(boostUnit, "NO ADAPTER");
  }
  lv_obj_align(boostValue, boostGauge, LV_ALIGN_CENTER, 0, 6);
  lv_obj_align(boostUnit, boostValue, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);
}

void Car::RefreshHud() {
  const auto& data = Data();
  if (!data.connected) {
    lv_label_set_text_static(hudCoolant, "--");
    lv_label_set_text_static(hudIntake, "--");
    lv_label_set_text_static(hudRpm, "--");
    lv_label_set_text_static(hudThrottle, "--");
    lv_label_set_text_static(hudO2, "--");
    lv_label_set_text_static(hudBattery, "--");
    lv_label_set_text_static(hudSpeed, "--");
    lv_label_set_text_static(hudMap, "--");
    return;
  }
  lv_label_set_text_fmt(hudCoolant, "%dC", data.coolantTempC);
  lv_label_set_text_fmt(hudIntake, "%dC", data.intakeTempC);
  lv_label_set_text_fmt(hudRpm, "%d", data.rpm);
  lv_label_set_text_fmt(hudThrottle, "%d%%", data.throttlePct);
  lv_label_set_text_fmt(hudO2, "%d.%02d", data.o2Millivolt / 1000, (data.o2Millivolt % 1000) / 10);
  lv_label_set_text_fmt(hudBattery, "%d.%dV", data.batteryMillivolt / 1000, (data.batteryMillivolt % 1000) / 100);
  lv_label_set_text_fmt(hudSpeed, "%d", data.speedKmh);
  lv_label_set_text_fmt(hudMap, "%d", data.mapKpa);
}
