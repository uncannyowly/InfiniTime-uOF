#pragma once

#include <lvgl/src/lv_core/lv_obj.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include "displayapp/Controllers.h"
#include "displayapp/screens/Screen.h"
#include "components/datetime/DateTimeController.h"
#include "components/ble/SimpleWeatherService.h"
#include "utility/DirtyValue.h"
#include "displayapp/apps/Apps.h"

namespace Pinetime {
  namespace Controllers {
    class Settings;
    class Battery;
    class Ble;
    class NotificationManager;
    class HeartRateController;
    class MotionController;
  }

  namespace Applications {
    namespace Screens {

      // Colour roles of the LCARS frame, so the same layout can be skinned
      struct LcarsPalette {
        uint32_t primary;   // bottom frame, time
        uint32_t secondary; // date, top bar end cap, STEP row
        uint32_t header;    // top frame, stardate
        uint32_t tertiary;  // PWR row
        uint32_t cool;      // bottom bar end cap, WX row, COMM status
        uint32_t highlight; // bottom bar segment
        uint32_t alert;     // HR row, MSG, low battery
        uint32_t dim;       // inactive status
      };

      // TNG-era LCARS
      constexpr LcarsPalette lcarsClassic {0xff9900, 0xffcc99, 0xcc99cc, 0x9999ff, 0x99ccff, 0xffcc66, 0xcc6666, 0x555566};
      // magenta / cyan / pink
      constexpr LcarsPalette lcarsNeon {0xff33cc, 0xff99dd, 0x33ddff, 0xcc88ff, 0x99f0ff, 0xff66aa, 0xff3377, 0x553355};

      class WatchFaceLcars : public Screen {
      public:
        WatchFaceLcars(Controllers::DateTime& dateTimeController,
                       const Controllers::Battery& batteryController,
                       const Controllers::Ble& bleController,
                       Controllers::NotificationManager& notificationManager,
                       Controllers::Settings& settingsController,
                       Controllers::HeartRateController& heartRateController,
                       Controllers::MotionController& motionController,
                       Controllers::SimpleWeatherService& weatherService,
                       const LcarsPalette& palette);
        ~WatchFaceLcars() override;

        void Refresh() override;

      private:
        Utility::DirtyValue<int> batteryPercentRemaining {};
        Utility::DirtyValue<bool> powerPresent {};
        Utility::DirtyValue<bool> bleState {};
        Utility::DirtyValue<bool> bleRadioEnabled {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>> currentDateTime {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::minutes>> currentMinute {};
        Utility::DirtyValue<std::chrono::time_point<std::chrono::system_clock, std::chrono::days>> currentDate {};
        Utility::DirtyValue<uint32_t> stepCount {};
        Utility::DirtyValue<uint8_t> heartbeat {};
        Utility::DirtyValue<bool> heartbeatRunning {};
        Utility::DirtyValue<bool> notificationState {};
        Utility::DirtyValue<std::optional<Controllers::SimpleWeatherService::CurrentWeather>> currentWeather {};

        lv_obj_t* labelTime;
        lv_obj_t* labelAmPm;
        lv_obj_t* labelSeconds;
        lv_obj_t* labelDate;
        lv_obj_t* labelStardate;
        lv_obj_t* labelComm;
        lv_obj_t* labelMsg;
        lv_obj_t* labelBattery;
        lv_obj_t* labelSteps;
        lv_obj_t* labelHeartRate;
        lv_obj_t* labelWeather;

        Controllers::DateTime& dateTimeController;
        const Controllers::Battery& batteryController;
        const Controllers::Ble& bleController;
        Controllers::NotificationManager& notificationManager;
        Controllers::Settings& settingsController;
        Controllers::HeartRateController& heartRateController;
        Controllers::MotionController& motionController;
        Controllers::SimpleWeatherService& weatherService;
        const LcarsPalette palette;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct WatchFaceTraits<WatchFace::Lcars> {
      static constexpr WatchFace watchFace = WatchFace::Lcars;
      static constexpr const char* name = "LCARS";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceLcars(controllers.dateTimeController,
                                           controllers.batteryController,
                                           controllers.bleController,
                                           controllers.notificationManager,
                                           controllers.settingsController,
                                           controllers.heartRateController,
                                           controllers.motionController,
                                           *controllers.weatherController,
                                           Screens::lcarsClassic);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct WatchFaceTraits<WatchFace::LcarsNeon> {
      static constexpr WatchFace watchFace = WatchFace::LcarsNeon;
      static constexpr const char* name = "LCARS Neon";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceLcars(controllers.dateTimeController,
                                           controllers.batteryController,
                                           controllers.bleController,
                                           controllers.notificationManager,
                                           controllers.settingsController,
                                           controllers.heartRateController,
                                           controllers.motionController,
                                           *controllers.weatherController,
                                           Screens::lcarsNeon);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
