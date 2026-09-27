#include "displayapp/screens/WatchFaceLcars.h"

#include <lvgl/lvgl.h>
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"
#include "displayapp/screens/WeatherSymbols.h"
#include <cctype>

using namespace Pinetime::Applications::Screens;

namespace {
  // Frame geometry (240x240 display)
  constexpr lv_coord_t screenSize = 240;
  constexpr lv_coord_t sidebarWidth = 48;
  constexpr lv_coord_t barHeight = 8;
  constexpr lv_coord_t gap = 3;
  constexpr lv_coord_t outerRadius = 24;
  constexpr lv_coord_t innerRadius = 12;
  constexpr lv_coord_t barEnd = screenSize - 6;
  constexpr lv_coord_t contentX = sidebarWidth + 10;

  constexpr lv_coord_t topBarY = 50;
  constexpr lv_coord_t bottomFrameY = topBarY + barHeight + 4;
  constexpr int rowCount = 4;
  constexpr lv_coord_t rowHeight = 22;
  constexpr lv_coord_t firstRowY = screenSize - rowCount * rowHeight - (rowCount - 1) * gap;

  lv_obj_t* CreateBlock(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, uint32_t color, lv_style_int_t radius = 0) {
    lv_obj_t* obj = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_click(obj, false);
    lv_obj_set_style_local_bg_color(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_obj_set_style_local_bg_opa(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_border_width(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, radius);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    return obj;
  }

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

  // Right-align a label so that its right edge sits at x
  void AlignRight(lv_obj_t* label, lv_coord_t x, lv_coord_t y) {
    lv_obj_set_pos(label, x - lv_obj_get_width(label), y);
  }

  // Place a black caption in the bottom-right corner of a sidebar block
  void SidebarCaption(lv_obj_t* label, lv_coord_t blockBottom) {
    AlignRight(label, sidebarWidth - 4, blockBottom - lv_obj_get_height(label) + 1);
  }
}

WatchFaceLcars::WatchFaceLcars(Controllers::DateTime& dateTimeController,
                               const Controllers::Battery& batteryController,
                               const Controllers::Ble& bleController,
                               Controllers::NotificationManager& notificationManager,
                               Controllers::Settings& settingsController,
                               Controllers::HeartRateController& heartRateController,
                               Controllers::MotionController& motionController,
                               Controllers::SimpleWeatherService& weatherService,
                               const LcarsPalette& palette)
  : dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController},
    weatherService {weatherService},
    palette {palette} {

  // ---- Top frame: sidebar elbow curving into a horizontal bar along the bottom of the header
  CreateBlock(0, 0, screenSize + 20, topBarY + barHeight, palette.header, outerRadius);
  CreateBlock(0, 0, sidebarWidth, outerRadius, palette.header); // square off the top-left corner
  CreateBlock(sidebarWidth, -20, screenSize, topBarY + 20, 0x000000, innerRadius);
  CreateBlock(barEnd, topBarY, screenSize - barEnd, barHeight, 0x000000);
  CreateBlock(150, topBarY, gap, barHeight, 0x000000);
  CreateBlock(150 + gap, topBarY, 47, barHeight, palette.primary);
  CreateBlock(200, topBarY, gap, barHeight, 0x000000);
  CreateBlock(200 + gap, topBarY, barEnd - 200 - gap, barHeight, palette.secondary, barHeight / 2);

  // ---- Bottom frame: horizontal bar curving down into the sidebar
  CreateBlock(0, bottomFrameY, screenSize + 20, screenSize, palette.primary, outerRadius);
  CreateBlock(sidebarWidth, bottomFrameY + barHeight, screenSize, screenSize, 0x000000, innerRadius);
  CreateBlock(barEnd, bottomFrameY, screenSize - barEnd, barHeight, 0x000000);
  CreateBlock(120, bottomFrameY, gap, barHeight, 0x000000);
  CreateBlock(120 + gap, bottomFrameY, 47, barHeight, palette.highlight);
  CreateBlock(170, bottomFrameY, gap, barHeight, 0x000000);
  CreateBlock(170 + gap, bottomFrameY, barEnd - 170 - gap, barHeight, palette.cool, barHeight / 2);

  // Sidebar blocks, one per data row
  const uint32_t rowColors[rowCount] = {palette.cool, palette.tertiary, palette.secondary, palette.alert};
  constexpr const char* rowCaptions[rowCount] = {"WX", "PWR", "STEP", "HR"};
  lv_obj_t* rowValues[rowCount];
  for (int i = 0; i < rowCount; i++) {
    lv_coord_t y = firstRowY + i * (rowHeight + gap);
    CreateBlock(0, y - gap, sidebarWidth, gap, 0x000000);
    lv_obj_t* block = CreateBlock(0, y, sidebarWidth, rowHeight, rowColors[i]);
    SidebarCaption(CreateLabel(&antonio_bold_16, 0x000000, rowCaptions[i]), y + rowHeight);

    rowValues[i] = CreateLabel(&antonio_bold_24, rowColors[i], "");
    lv_obj_align(rowValues[i], block, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
  }
  labelWeather = rowValues[0];
  labelBattery = rowValues[1];
  labelSteps = rowValues[2];
  labelHeartRate = rowValues[3];

  // ---- Sidebar captions
  lv_obj_t* labelLcars = CreateLabel(&antonio_bold_16, 0x000000, "LCARS");
  AlignRight(labelLcars, sidebarWidth - 4, 2);

  labelSeconds = CreateLabel(&antonio_bold_16, 0x000000, "00");
  SidebarCaption(labelSeconds, firstRowY - gap);
  labelAmPm = CreateLabel(&antonio_bold_16, 0x000000, "");
  lv_obj_set_hidden(labelAmPm, true);

  // ---- Header
  labelDate = CreateLabel(&antonio_bold_24, palette.secondary, "");
  labelStardate = CreateLabel(&antonio_bold_16, palette.header, "");
  labelMsg = CreateLabel(&antonio_bold_16, palette.alert, "MSG");
  labelComm = CreateLabel(&antonio_bold_16, palette.cool, "COMM");
  lv_coord_t statusY = topBarY - lv_obj_get_height(labelComm) - 1;
  lv_obj_set_pos(labelStardate, contentX, statusY);
  AlignRight(labelComm, barEnd, statusY);
  AlignRight(labelMsg, lv_obj_get_x(labelComm) - 8, statusY);
  lv_obj_set_hidden(labelMsg, true);

  // ---- Time
  labelTime = CreateLabel(&antonio_bold_64, palette.primary, "");

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

WatchFaceLcars::~WatchFaceLcars() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void WatchFaceLcars::Refresh() {
  currentDateTime = std::chrono::time_point_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    lv_label_set_text_fmt(labelSeconds, "%02d", dateTimeController.Seconds());
    SidebarCaption(labelSeconds, firstRowY - gap);

    currentMinute = std::chrono::time_point_cast<std::chrono::minutes>(currentDateTime.Get());
    if (currentMinute.IsUpdated()) {
      uint8_t hour = dateTimeController.Hours();
      uint8_t minute = dateTimeController.Minutes();

      if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
        lv_label_set_text_static(labelAmPm, hour < 12 ? "AM" : "PM");
        lv_obj_set_hidden(labelAmPm, false);
        SidebarCaption(labelAmPm, lv_obj_get_y(labelSeconds) + 2);
        hour = hour % 12;
        if (hour == 0) {
          hour = 12;
        }
      } else {
        lv_obj_set_hidden(labelAmPm, true);
      }
      lv_label_set_text_fmt(labelTime, "%02d:%02d", hour, minute);
      constexpr lv_coord_t timeAreaTop = bottomFrameY + barHeight;
      constexpr lv_coord_t timeAreaBottom = firstRowY - gap;
      lv_obj_align(labelTime,
                   nullptr,
                   LV_ALIGN_IN_TOP_MID,
                   sidebarWidth / 2,
                   (timeAreaTop + timeAreaBottom - lv_obj_get_height(labelTime)) / 2);
    }

    currentDate = std::chrono::time_point_cast<std::chrono::days>(currentDateTime.Get());
    if (currentDate.IsUpdated()) {
      lv_label_set_text_fmt(labelDate,
                            "%s %d %s",
                            dateTimeController.DayOfWeekShortToString(),
                            dateTimeController.Day(),
                            dateTimeController.MonthShortToString());
      AlignRight(labelDate, barEnd, 2);
      lv_label_set_text_fmt(labelStardate, "SD %d.%03d", dateTimeController.Year(), dateTimeController.DayOfYear());
    }
  }

  notificationState = notificationManager.AreNewNotificationsAvailable();
  if (notificationState.IsUpdated()) {
    lv_obj_set_hidden(labelMsg, !notificationState.Get());
  }

  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();
  if (bleState.IsUpdated() || bleRadioEnabled.IsUpdated()) {
    SetTextColor(labelComm, bleRadioEnabled.Get() && bleState.Get() ? palette.cool : palette.dim);
  }

  powerPresent = batteryController.IsPowerPresent();
  batteryPercentRemaining = batteryController.PercentRemaining();
  if (batteryPercentRemaining.IsUpdated() || powerPresent.IsUpdated()) {
    lv_label_set_text_fmt(labelBattery, "%d%%%s", batteryPercentRemaining.Get(), powerPresent.Get() ? " CHG" : "");
    SetTextColor(labelBattery, batteryPercentRemaining.Get() <= 20 ? palette.alert : palette.tertiary);
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
      // the Antonio fonts only carry upper case letters
      char condition[12] = {};
      const char* simpleCondition = Symbols::GetSimpleCondition(optCurrentWeather->iconId);
      for (size_t i = 0; i < sizeof(condition) - 1 && simpleCondition[i] != '\0'; i++) {
        condition[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(simpleCondition[i])));
      }
      lv_label_set_text_fmt(labelWeather, "%d°%c %s", temp, tempUnit, condition);
    } else {
      lv_label_set_text_static(labelWeather, "---");
    }
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_label_set_text_fmt(labelSteps, "%lu", stepCount.Get());
  }

  heartbeat = heartRateController.HeartRate();
  heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  if (heartbeat.IsUpdated() || heartbeatRunning.IsUpdated()) {
    if (heartbeatRunning.Get()) {
      lv_label_set_text_fmt(labelHeartRate, "%d BPM", heartbeat.Get());
    } else {
      lv_label_set_text_static(labelHeartRate, "---");
    }
  }
}
