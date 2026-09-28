#pragma once

#include <cstdint>
#include <memory>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "components/car/ObdSimulator.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      // OBD-II car dashboard. A menu leads to a Speed dial, a Boost/Vacuum dial, and a
      // multi-stat HUD. Data comes from ObdSource; today that is a built-in simulator, and a
      // BLE ELM327 transport can replace it without touching this screen.
      class Car : public Screen {
      public:
        Car();
        ~Car() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;

        void OnMenuButton(uint8_t view);

        struct MenuItem {
          Car* self;
          uint8_t view;
        };

        // Sentinel MenuItem::view value: toggle demo mode rather than switch view
        static constexpr uint8_t toggleDemo = 255;

      private:
        enum class View : uint8_t { Menu, Speed, Boost, Hud };

        // The values the views read. In demo mode this is the simulator; otherwise a
        // disconnected snapshot, because no BLE ELM327 transport is wired in yet.
        const Controllers::ObdData& Data() const;

        void SwitchTo(View view);
        void BuildMenu();
        void BuildSpeed();
        void BuildBoost();
        void BuildHud();
        void RefreshSpeed();
        void RefreshBoost();
        void RefreshHud();

        lv_obj_t* CreateStatTile(lv_coord_t x, lv_coord_t y, const char* caption, uint32_t color, lv_obj_t** valueLabel);

        Controllers::ObdSimulator obd;
        Controllers::ObdData disconnected {}; // connected=false, all zero

        bool demoMode = false; // off by default: show "no adapter" until a source connects

        View currentView = View::Menu;
        View pendingView = View::Menu;
        bool viewDirty = true;

        MenuItem menuItems[4]; // 3 views + demo toggle

        // Speed view
        lv_obj_t* speedGauge = nullptr;
        lv_obj_t* speedValue = nullptr;
        lv_obj_t* speedUnit = nullptr;
        lv_obj_t* rpmBar = nullptr;
        lv_obj_t* rpmValue = nullptr;

        // Boost view
        lv_obj_t* boostGauge = nullptr;
        lv_obj_t* boostValue = nullptr;
        lv_obj_t* boostUnit = nullptr;

        // HUD view
        lv_obj_t* hudCoolant = nullptr;
        lv_obj_t* hudIntake = nullptr;
        lv_obj_t* hudRpm = nullptr;
        lv_obj_t* hudThrottle = nullptr;
        lv_obj_t* hudO2 = nullptr;
        lv_obj_t* hudBattery = nullptr;
        lv_obj_t* hudSpeed = nullptr;
        lv_obj_t* hudMap = nullptr;

        lv_obj_t* statusLabel = nullptr;

        lv_task_t* taskRefresh = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::Car> {
      static constexpr Apps app = Apps::Car;
      static constexpr const char* icon = Screens::Symbols::car;

      static Screens::Screen* Create(AppControllers& /*controllers*/) {
        return new Screens::Car();
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
