#include "displayapp/screens/WatchFaceCyberdeck.h"

#include <lvgl/lvgl.h>
#include <algorithm>
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"
#include "displayapp/screens/WeatherSymbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  // Neon-on-black terminal palette
  constexpr uint32_t colorBackground = 0x000000;
  constexpr uint32_t colorNeon = 0x00ff9c;
  constexpr uint32_t colorCyan = 0x00e5ff;
  constexpr uint32_t colorAmber = 0xffb000;
  constexpr uint32_t colorMagenta = 0xff2a6d;
  constexpr uint32_t colorFrame = 0x1e5a47;
  constexpr uint32_t colorDim = 0x3c7f6a;

  constexpr lv_coord_t margin = 2;
  constexpr lv_coord_t screenSize = 240;

  constexpr lv_coord_t clockY = 20;
  constexpr lv_coord_t clockHeight = 86;
  constexpr lv_coord_t dateY = clockY + clockHeight + 4;
  constexpr lv_coord_t gridY = 134;
  constexpr lv_coord_t cellWidth = 116;
  constexpr lv_coord_t cellHeight = 50;
  constexpr lv_coord_t cellGap = screenSize - 2 * margin - 2 * cellWidth;
  constexpr lv_coord_t leftX = margin;
  constexpr lv_coord_t rightX = margin + cellWidth + cellGap;

  // NetOps layout
  constexpr lv_coord_t clockRadius = 36;
  constexpr lv_coord_t clockCenterX = leftX + cellWidth / 2;
  constexpr lv_coord_t clockCenterY = clockY + clockHeight / 2 + 2;
  constexpr lv_coord_t netX = rightX + 6;
  constexpr lv_coord_t netWidth = cellWidth - 12;
  constexpr lv_coord_t waveY = clockY + 30;
  constexpr lv_coord_t waveHeight = 22;
  constexpr uint32_t animationPeriodMs = 120;
  constexpr const char* hackStages[] = {"> SCAN", "> BREACH", "> DECRYPT", "> UPLINK"};
  constexpr uint8_t hackStageCount = sizeof(hackStages) / sizeof(hackStages[0]);
  // _lv_trigo_sin() returns sin(angle) scaled to this value
  constexpr int32_t trigoScale = 32767;

  lv_obj_t* CreateLabel(const lv_font_t* font, uint32_t color, const char* text) {
    lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_label_set_text_static(label, text);
    return label;
  }

  void SetTextColor(lv_obj_t* label, uint32_t color) {
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  }

  lv_obj_t* CreateRect(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint32_t color) {
    lv_obj_t* obj = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_click(obj, false);
    lv_obj_set_style_local_bg_color(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_obj_set_style_local_bg_opa(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
  }

  // A small label with a solid background, so it cuts through a panel border
  lv_obj_t* CreateTag(uint32_t color, const char* text) {
    lv_obj_t* label = CreateLabel(&jetbrains_mono_bold_14, color, text);
    lv_obj_set_style_local_bg_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorBackground));
    lv_obj_set_style_local_bg_opa(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    return label;
  }

  // A thin-bordered panel with a "[TAG]" cut into its top edge
  lv_obj_t* CreatePanel(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, const char* tag) {
    lv_obj_t* panel = CreateRect(x, y, w, h, colorBackground);
    lv_obj_set_style_local_border_width(panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_border_color(panel, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorFrame));

    lv_obj_t* label = CreateTag(colorDim, tag);
    lv_obj_set_pos(label, x + 8, y - lv_obj_get_height(label) / 2);
    return panel;
  }

  // Bright L-shaped brackets on the four corners of a rectangle
  void CreateCornerBrackets(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
    constexpr lv_coord_t arm = 8;
    constexpr lv_coord_t thickness = 2;
    const lv_coord_t right = x + w - arm;
    const lv_coord_t bottom = y + h - thickness;
    CreateRect(x, y, arm, thickness, colorNeon);
    CreateRect(x, y, thickness, arm, colorNeon);
    CreateRect(right, y, arm, thickness, colorNeon);
    CreateRect(x + w - thickness, y, thickness, arm, colorNeon);
    CreateRect(x, bottom, arm, thickness, colorNeon);
    CreateRect(x, y + h - arm, thickness, arm, colorNeon);
    CreateRect(right, bottom, arm, thickness, colorNeon);
    CreateRect(x + w - thickness, y + h - arm, thickness, arm, colorNeon);
  }

  lv_obj_t* CreateLine(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint32_t color, lv_style_int_t width) {
    lv_obj_t* line = lv_line_create(lv_scr_act(), nullptr);
    lv_line_set_auto_size(line, false);
    lv_obj_set_pos(line, x, y);
    lv_obj_set_size(line, w, h);
    lv_obj_set_style_local_line_color(line, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_obj_set_style_local_line_width(line, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, width);
    lv_obj_set_style_local_line_rounded(line, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, true);
    return line;
  }

  // Point a clock hand (whose line object spans the clock face) at angle degrees clockwise from 12
  void SetHand(lv_obj_t* hand, lv_point_t* points, int16_t angle, lv_coord_t length) {
    points[0] = {clockRadius, clockRadius};
    points[1] = {static_cast<lv_coord_t>(clockRadius + length * _lv_trigo_sin(angle) / trigoScale),
                 static_cast<lv_coord_t>(clockRadius - length * _lv_trigo_sin(angle + 90) / trigoScale)};
    lv_line_set_points(hand, points, 2);
  }

  lv_obj_t* CreateBar(lv_coord_t x, lv_coord_t y, lv_coord_t w, uint32_t color) {
    lv_obj_t* bar = lv_bar_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_bg_color(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, lv_color_hex(colorFrame));
    lv_obj_set_style_local_bg_opa(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_radius(bar, LV_BAR_PART_BG, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_bg_color(bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_obj_set_style_local_radius(bar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, 0);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, 5);
    lv_bar_set_range(bar, 0, 100);
    return bar;
  }
}

WatchFaceCyberdeck::WatchFaceCyberdeck(Controllers::DateTime& dateTimeController,
                                       const Controllers::Battery& batteryController,
                                       const Controllers::Ble& bleController,
                                       Controllers::NotificationManager& notificationManager,
                                       Controllers::Settings& settingsController,
                                       Controllers::HeartRateController& heartRateController,
                                       Controllers::MotionController& motionController,
                                       Controllers::SimpleWeatherService& weatherService,
                                       CyberdeckLayout layout)
  : dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    weatherService {weatherService},
    layout {layout} {

  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorBackground));

  // ---- Status line
  lv_obj_t* labelHost = CreateLabel(&jetbrains_mono_bold_14, colorNeon, "DECK//01");
  lv_obj_set_pos(labelHost, margin + 2, 1);
  labelLink = CreateLabel(&jetbrains_mono_bold_14, colorDim, "LNK");
  lv_obj_set_pos(labelLink, screenSize - margin - 2 - lv_obj_get_width(labelLink), 1);
  labelMsg = CreateLabel(&jetbrains_mono_bold_14, colorMagenta, "MSG");
  lv_obj_set_pos(labelMsg, lv_obj_get_x(labelLink) - 8 - lv_obj_get_width(labelMsg), 1);
  lv_obj_set_hidden(labelMsg, true);

  // ---- Clock section
  if (layout == CyberdeckLayout::Digital) {
    CreateDigitalClock();
  } else {
    CreateNetOps();
  }

  // ---- Date prompt
  labelDate = CreateLabel(&jetbrains_mono_bold_14, colorCyan, "");
  lv_obj_set_pos(labelDate, margin + 4, dateY);

  // ---- 2x2 sensor grid
  const lv_coord_t bottomY = gridY + cellHeight + 6;

  CreatePanel(leftX, gridY, cellWidth, cellHeight, "[PWR]");
  labelBattery = CreateLabel(&jetbrains_mono_bold_20, colorNeon, "");
  lv_obj_set_pos(labelBattery, leftX + 8, gridY + 10);
  labelCharging = CreateLabel(&jetbrains_mono_bold_14, colorAmber, "CHG");
  lv_obj_set_pos(labelCharging, leftX + cellWidth - 8 - lv_obj_get_width(labelCharging), gridY + 14);
  lv_obj_set_hidden(labelCharging, true);
  barBattery = CreateBar(leftX + 8, gridY + cellHeight - 12, cellWidth - 16, colorNeon);

  CreatePanel(rightX, gridY, cellWidth, cellHeight, "[STEP]");
  labelSteps = CreateLabel(&jetbrains_mono_bold_20, colorAmber, "");
  lv_obj_set_pos(labelSteps, rightX + 8, gridY + 10);
  barSteps = CreateBar(rightX + 8, gridY + cellHeight - 12, cellWidth - 16, colorAmber);

  CreatePanel(leftX, bottomY, cellWidth, cellHeight, "[HR]");
  labelHeartRate = CreateLabel(&jetbrains_mono_bold_20, colorMagenta, "");
  lv_obj_set_pos(labelHeartRate, leftX + 8, bottomY + 14);
  labelBpm = CreateLabel(&jetbrains_mono_bold_14, colorDim, "BPM");
  lv_obj_set_pos(labelBpm, leftX + cellWidth - 8 - lv_obj_get_width(labelBpm), bottomY + 18);

  CreatePanel(rightX, bottomY, cellWidth, cellHeight, "[WX]");
  labelTemperature = CreateLabel(&jetbrains_mono_bold_20, colorCyan, "");
  lv_obj_set_pos(labelTemperature, rightX + 8, bottomY + 8);
  labelCondition = CreateLabel(&jetbrains_mono_bold_14, colorDim, "");
  lv_obj_set_pos(labelCondition, rightX + 8, bottomY + 30);

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

void WatchFaceCyberdeck::CreateDigitalClock() {
  CreatePanel(margin, clockY, screenSize - 2 * margin, clockHeight, "[T-CLK]");
  CreateCornerBrackets(margin, clockY, screenSize - 2 * margin, clockHeight);
  labelTime = CreateLabel(&jetbrains_mono_extrabold_compressed, colorNeon, "");
  labelSeconds = CreateTag(colorAmber, "");
  labelDayOfYear = CreateLabel(&jetbrains_mono_bold_14, colorDim, "");
}

void WatchFaceCyberdeck::CreateNetOps() {
  // Mini analog clock
  CreatePanel(leftX, clockY, cellWidth, clockHeight, "[CLK]");
  CreateCornerBrackets(leftX, clockY, cellWidth, clockHeight);

  lv_obj_t* face = CreateRect(clockCenterX - clockRadius, clockCenterY - clockRadius, 2 * clockRadius, 2 * clockRadius, colorBackground);
  lv_obj_set_style_local_radius(face, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);
  lv_obj_set_style_local_border_width(face, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
  lv_obj_set_style_local_border_color(face, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorFrame));

  lv_obj_t* ticks = lv_linemeter_create(lv_scr_act(), nullptr);
  lv_linemeter_set_scale(ticks, 330, 12);
  lv_linemeter_set_angle_offset(ticks, 195);
  lv_obj_set_pos(ticks, clockCenterX - clockRadius, clockCenterY - clockRadius);
  lv_obj_set_size(ticks, 2 * clockRadius, 2 * clockRadius);
  lv_obj_set_style_local_bg_opa(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_all(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_scale_width(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_scale_end_line_width(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, 2);
  lv_obj_set_style_local_scale_end_color(ticks, LV_LINEMETER_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorDim));

  const lv_coord_t handBoxX = clockCenterX - clockRadius;
  const lv_coord_t handBoxY = clockCenterY - clockRadius;
  const lv_coord_t handBoxSize = 2 * clockRadius + 1;
  hourHand = CreateLine(handBoxX, handBoxY, handBoxSize, handBoxSize, colorNeon, 3);
  minuteHand = CreateLine(handBoxX, handBoxY, handBoxSize, handBoxSize, colorCyan, 2);
  secondHand = CreateLine(handBoxX, handBoxY, handBoxSize, handBoxSize, colorMagenta, 1);
  lv_obj_t* hub = CreateRect(clockCenterX - 3, clockCenterY - 3, 7, 7, colorAmber);
  lv_obj_set_style_local_radius(hub, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_RADIUS_CIRCLE);

  // Network intrusion animation
  CreatePanel(rightX, clockY, cellWidth, clockHeight, "[NET]");
  CreateCornerBrackets(rightX, clockY, cellWidth, clockHeight);
  labelHackStage = CreateLabel(&jetbrains_mono_bold_14, colorMagenta, hackStages[0]);
  lv_obj_set_pos(labelHackStage, netX, clockY + 10);
  labelHackPercent = CreateLabel(&jetbrains_mono_bold_14, colorAmber, "");

  randomState = 0x9e3779b9u ^ static_cast<uint32_t>(dateTimeController.CurrentDateTime().time_since_epoch().count());
  for (int i = 0; i < wavePointCount; i++) {
    wavePoints[i] = {static_cast<lv_coord_t>(i * netWidth / (wavePointCount - 1)), waveHeight / 2};
  }
  waveform = CreateLine(netX, waveY, netWidth + 1, waveHeight + 1, colorCyan, 1);
  lv_line_set_points(waveform, wavePoints, wavePointCount);

  labelHex = CreateLabel(&jetbrains_mono_bold_14, colorDim, "");
  lv_obj_set_pos(labelHex, netX, waveY + waveHeight + 4);
  barHack = CreateBar(netX, clockY + clockHeight - 11, netWidth, colorMagenta);
}

uint32_t WatchFaceCyberdeck::NextRandom() {
  // xorshift32
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

void WatchFaceCyberdeck::UpdateAnalogClock() {
  const uint8_t hour = dateTimeController.Hours();
  const uint8_t minute = dateTimeController.Minutes();
  const uint8_t second = dateTimeController.Seconds();
  SetHand(hourHand, hourPoints, (hour % 12) * 30 + minute / 2, 18);
  SetHand(minuteHand, minutePoints, minute * 6, 28);
  SetHand(secondHand, secondPoints, second * 6, 31);
}

void WatchFaceCyberdeck::AnimateNetOps() {
  if (lv_tick_elaps(lastAnimationTick) < animationPeriodMs) {
    return;
  }
  lastAnimationTick = lv_tick_get();

  // signal trace: scroll left, random walk with the occasional spike
  for (int i = 0; i < wavePointCount - 1; i++) {
    wavePoints[i].y = wavePoints[i + 1].y;
  }
  lv_coord_t next = wavePoints[wavePointCount - 2].y + static_cast<lv_coord_t>(NextRandom() % 11) - 5;
  if (NextRandom() % 8 == 0) {
    next = (NextRandom() % 2 == 0) ? 0 : waveHeight;
  }
  wavePoints[wavePointCount - 1].y = std::clamp<lv_coord_t>(next, 0, waveHeight);
  lv_line_set_points(waveform, wavePoints, wavePointCount);

  // intercepted bytes
  for (int i = 0; i < hexByteCount - 1; i++) {
    hexBytes[i] = hexBytes[i + 1];
  }
  hexBytes[hexByteCount - 1] = static_cast<uint8_t>(NextRandom());
  lv_label_set_text_fmt(labelHex, "%02X %02X %02X %02X", hexBytes[0], hexBytes[1], hexBytes[2], hexBytes[3]);

  // stage progress
  if (hackHold > 0) {
    if (--hackHold == 0) {
      hackStage = (hackStage + 1) % hackStageCount;
      hackProgress = 0;
      lv_label_set_text_static(labelHackStage, hackStages[hackStage]);
      SetTextColor(labelHackStage, colorMagenta);
    }
  } else {
    hackProgress = std::min<uint8_t>(hackProgress + 1 + NextRandom() % 4, 100);
    if (hackProgress == 100) {
      hackHold = 12;
      lv_label_set_text_static(labelHackStage, "> ACCESS OK");
      SetTextColor(labelHackStage, colorNeon);
    }
  }
  lv_label_set_text_fmt(labelHackPercent, "%d%%", hackProgress);
  lv_obj_set_pos(labelHackPercent, rightX + cellWidth - 6 - lv_obj_get_width(labelHackPercent), clockY + 10);
  lv_obj_set_hidden(labelHackPercent, hackHold > 0);
  lv_bar_set_value(barHack, hackProgress, LV_ANIM_OFF);
}

WatchFaceCyberdeck::~WatchFaceCyberdeck() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void WatchFaceCyberdeck::Refresh() {
  currentDateTime = std::chrono::time_point_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    const uint8_t second = dateTimeController.Seconds();
    const uint8_t minute = dateTimeController.Minutes();
    uint8_t hour = dateTimeController.Hours();
    const bool is12Hour = settingsController.GetClockType() == Controllers::Settings::ClockType::H12;
    const char* amPm = is12Hour ? (hour < 12 ? "AM" : "PM") : "";
    if (is12Hour) {
      hour = hour % 12;
      if (hour == 0) {
        hour = 12;
      }
    }
    const char* cursor = second % 2 == 0 ? "_" : "";

    if (layout == CyberdeckLayout::Digital) {
      // seconds (and AM/PM) sit in a tag on the clock panel's top-right border
      lv_label_set_text_fmt(labelSeconds, "[%s%s:%02d]", amPm, is12Hour ? " " : "", second);
      lv_obj_set_pos(labelSeconds,
                     screenSize - margin - 8 - lv_obj_get_width(labelSeconds),
                     clockY - lv_obj_get_height(labelSeconds) / 2);

      currentMinute = std::chrono::time_point_cast<std::chrono::minutes>(currentDateTime.Get());
      if (currentMinute.IsUpdated()) {
        lv_label_set_text_fmt(labelTime, "%02d:%02d", hour, minute);
        lv_obj_align(labelTime, nullptr, LV_ALIGN_IN_TOP_MID, 0, clockY + (clockHeight - lv_obj_get_height(labelTime)) / 2);
      }

      // blinking cursor on the date prompt
      lv_label_set_text_fmt(labelDate,
                            "> %s %02d %s %d%s",
                            dateTimeController.DayOfWeekShortToString(),
                            dateTimeController.Day(),
                            dateTimeController.MonthShortToString(),
                            dateTimeController.Year(),
                            cursor);
    } else {
      UpdateAnalogClock();
      // the analog clock is small, so the prompt carries the exact time
      lv_label_set_text_fmt(labelDate,
                            "> %02d:%02d:%02d%s %s %02d %s%s",
                            hour,
                            minute,
                            second,
                            amPm,
                            dateTimeController.DayOfWeekShortToString(),
                            dateTimeController.Day(),
                            dateTimeController.MonthShortToString(),
                            cursor);
    }

    currentDate = std::chrono::time_point_cast<std::chrono::days>(currentDateTime.Get());
    if (currentDate.IsUpdated() && layout == CyberdeckLayout::Digital) {
      lv_label_set_text_fmt(labelDayOfYear, "D%03d", dateTimeController.DayOfYear());
      lv_obj_set_pos(labelDayOfYear, screenSize - margin - 4 - lv_obj_get_width(labelDayOfYear), dateY);
    }
  }

  notificationState = notificationManager.AreNewNotificationsAvailable();
  if (notificationState.IsUpdated()) {
    lv_obj_set_hidden(labelMsg, !notificationState.Get());
  }

  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();
  if (bleState.IsUpdated() || bleRadioEnabled.IsUpdated()) {
    SetTextColor(labelLink, bleRadioEnabled.Get() && bleState.Get() ? colorCyan : colorDim);
  }

  powerPresent = batteryController.IsPowerPresent();
  batteryPercentRemaining = batteryController.PercentRemaining();
  if (batteryPercentRemaining.IsUpdated() || powerPresent.IsUpdated()) {
    const int percent = batteryPercentRemaining.Get();
    const uint32_t color = percent <= 20 ? colorMagenta : colorNeon;
    lv_label_set_text_fmt(labelBattery, "%d%%", percent);
    SetTextColor(labelBattery, color);
    lv_obj_set_style_local_bg_color(barBattery, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_bar_set_value(barBattery, percent, LV_ANIM_OFF);
    lv_obj_set_hidden(labelCharging, !powerPresent.Get());
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_label_set_text_fmt(labelSteps, "%lu", stepCount.Get());
    const uint32_t goal = std::max<uint32_t>(settingsController.GetStepsGoal(), 1);
    lv_bar_set_value(barSteps, static_cast<int16_t>(std::min<uint32_t>(stepCount.Get() * 100 / goal, 100)), LV_ANIM_OFF);
  }

  heartbeat = heartRateController.HeartRate();
  heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  if (heartbeat.IsUpdated() || heartbeatRunning.IsUpdated()) {
    if (heartbeatRunning.Get()) {
      lv_label_set_text_fmt(labelHeartRate, "%d", heartbeat.Get());
    } else {
      lv_label_set_text_static(labelHeartRate, "---");
    }
  }

  currentWeather = weatherService.Current();
  if (currentWeather.IsUpdated()) {
    auto optCurrentWeather = currentWeather.Get();
    if (optCurrentWeather) {
      int16_t temp = optCurrentWeather->temperature.Celsius();
      char tempUnit = 'C';
      if (settingsController.GetWeatherFormat() == Controllers::Settings::WeatherFormat::Imperial) {
        temp = optCurrentWeather->temperature.Fahrenheit();
        tempUnit = 'F';
      }
      lv_label_set_text_fmt(labelTemperature, "%d°%c", temp, tempUnit);
      lv_label_set_text(labelCondition, Symbols::GetSimpleCondition(optCurrentWeather->iconId));
    } else {
      lv_label_set_text_static(labelTemperature, "---");
      lv_label_set_text_static(labelCondition, "NO DATA");
    }
  }

  if (layout == CyberdeckLayout::NetOps) {
    AnimateNetOps();
  }
}
