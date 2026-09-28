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

      enum class CyberdeckLayout : uint8_t {
        Digital, // one big digital clock
        NetOps,  // mini analog clock beside an animated "network intrusion" panel
      };

      class WatchFaceCyberdeck : public Screen {
      public:
        WatchFaceCyberdeck(Controllers::DateTime& dateTimeController,
                           const Controllers::Battery& batteryController,
                           const Controllers::Ble& bleController,
                           Controllers::NotificationManager& notificationManager,
                           Controllers::Settings& settingsController,
                           Controllers::HeartRateController& heartRateController,
                           Controllers::MotionController& motionController,
                           Controllers::SimpleWeatherService& weatherService,
                           CyberdeckLayout layout);
        ~WatchFaceCyberdeck() override;

        void Refresh() override;

      private:
        static constexpr int wavePointCount = 17;
        static constexpr int hexByteCount = 4;

        void CreateDigitalClock();
        void CreateNetOps();
        void UpdateAnalogClock();
        void AnimateNetOps();
        uint32_t NextRandom();

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

        lv_obj_t* labelLink;
        lv_obj_t* labelMsg;
        lv_obj_t* labelTime;
        lv_obj_t* labelSeconds;
        lv_obj_t* labelDate;
        lv_obj_t* labelDayOfYear;
        lv_obj_t* labelBattery;
        lv_obj_t* labelCharging;
        lv_obj_t* barBattery;
        lv_obj_t* labelSteps;
        lv_obj_t* barSteps;
        lv_obj_t* labelHeartRate;
        lv_obj_t* labelBpm;
        lv_obj_t* labelTemperature;
        lv_obj_t* labelCondition;

        // NetOps layout: analog clock
        lv_obj_t* hourHand;
        lv_obj_t* minuteHand;
        lv_obj_t* secondHand;
        lv_point_t hourPoints[2];
        lv_point_t minutePoints[2];
        lv_point_t secondPoints[2];

        // NetOps layout: intrusion animation
        lv_obj_t* labelHackStage;
        lv_obj_t* labelHackPercent;
        lv_obj_t* waveform;
        lv_obj_t* labelHex;
        lv_obj_t* barHack;
        lv_point_t wavePoints[wavePointCount];
        uint8_t hexBytes[hexByteCount] {};
        uint32_t randomState;
        uint32_t lastAnimationTick = 0;
        uint8_t hackStage = 0;
        uint8_t hackProgress = 0;
        uint8_t hackHold = 0;

        Controllers::DateTime& dateTimeController;
        const Controllers::Battery& batteryController;
        const Controllers::Ble& bleController;
        Controllers::NotificationManager& notificationManager;
        Controllers::Settings& settingsController;
        Controllers::HeartRateController& heartRateController;
        Controllers::MotionController& motionController;
        Controllers::SimpleWeatherService& weatherService;
        const CyberdeckLayout layout;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct WatchFaceTraits<WatchFace::Cyberdeck> {
      static constexpr WatchFace watchFace = WatchFace::Cyberdeck;
      static constexpr const char* name = "Cybrdek";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceCyberdeck(controllers.dateTimeController,
                                               controllers.batteryController,
                                               controllers.bleController,
                                               controllers.notificationManager,
                                               controllers.settingsController,
                                               controllers.heartRateController,
                                               controllers.motionController,
                                               *controllers.weatherController,
                                               Screens::CyberdeckLayout::Digital);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };

    template <>
    struct WatchFaceTraits<WatchFace::CyberdeckNetOps> {
      static constexpr WatchFace watchFace = WatchFace::CyberdeckNetOps;
      static constexpr const char* name = "Cybrdek Rnnr";

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::WatchFaceCyberdeck(controllers.dateTimeController,
                                               controllers.batteryController,
                                               controllers.bleController,
                                               controllers.notificationManager,
                                               controllers.settingsController,
                                               controllers.heartRateController,
                                               controllers.motionController,
                                               *controllers.weatherController,
                                               Screens::CyberdeckLayout::NetOps);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
