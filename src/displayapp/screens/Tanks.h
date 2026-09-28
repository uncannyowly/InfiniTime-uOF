#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      // Artillery duel. Touch sets the barrel angle, hold the button to charge power and release
      // to fire, double-tap to pick a shell. Terrain is destructible and tanks slide into craters.
      class Tanks : public Screen {
      public:
        Tanks();
        ~Tanks() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;
        bool OnButtonPushed() override;
        void OnButtonDown() override;
        void OnButtonUp() override;
        bool WantsRawButton() const override {
          return true; // hold-to-charge; we exit via swipe down
        }

        static constexpr int screenSize = 240;

        // 10 shell types; 3 (incl. Basic) are drawn per match
        enum class Shell : uint8_t {
          Basic, Heavy, Nuke, TriShot, Cluster, Roller, Digger, Sniper, Napalm, Mirv, Count
        };

        struct ShellSpec {
          const char* name;
          uint8_t blastRadius;
          uint8_t damage;
          uint8_t projectiles; // >1 = fired as a spread
          uint8_t velocityScale;
          bool splitAtApex;    // cluster/mirv: split into bomblets while descending
          bool roller;         // continues along the ground after impact
        };


      private:
        enum class Phase : uint8_t { Intro, Aim, Charging, Flying, Resolve, AiAim, GameOver };

        void GenerateTerrain();
        void PlaceTanks();
        void StartMatch();
        void StartIntro();
        void BeginPlayerTurn();
        void FireFrom(uint8_t tank, int16_t angleDeg, uint8_t power, Shell shell);
        void StepProjectiles();
        void Explode(int16_t x, int16_t y, const ShellSpec& spec, uint8_t owner);
        void SettleTanks();
        void DrawTerrain();
        void RenderProjectiles();
        void UpdateHud();
        void UpdateAimLine();
        void OpenShellMenu();
        void CloseShellMenu();
        void AiTakeTurn();
        void EndGame(bool playerWon);

        void SetSolid(int16_t x, int16_t y, bool solid);
        bool IsSolid(int16_t x, int16_t y) const;
        int16_t SurfaceY(int16_t x) const;
        uint32_t NextRandom();

        // Terrain: 240x240 1-bit image (8 byte palette + bitmap) redrawn on damage
        static constexpr int stride = screenSize / 8;
        static constexpr int bitmapBytes = stride * screenSize;
        uint8_t terrainBuf[8 + bitmapBytes];
        lv_img_dsc_t terrainDsc;
        lv_obj_t* terrainImg;

        struct Tank {
          int16_t x;
          int16_t y;
          int8_t health;
          lv_obj_t* body;
          lv_obj_t* barrel;
          lv_point_t barrelPoints[2];
        };
        Tank tanks[2];

        static constexpr int maxProjectiles = 6;
        struct Projectile {
          bool active;
          float x, y, vx, vy;
          uint8_t owner;
          bool split;
          lv_obj_t* dot;
        };
        Projectile projectiles[maxProjectiles];
        Shell flyingShell = Shell::Basic;

        Shell availableShells[3];
        uint8_t selectedShell = 0; // index into availableShells

        lv_obj_t* hud;
        lv_obj_t* powerBar;
        lv_obj_t* healthPlayer;
        lv_obj_t* healthAi;
        lv_obj_t* message;
        lv_obj_t* shellMenu = nullptr;
        lv_obj_t* windLabel;

        Phase phase = Phase::Intro;
        int16_t aimAngle = 45;   // degrees, player barrel
        uint8_t power = 0;
        bool charging = false;
        int8_t wind = 0;         // horizontal acceleration on shells
        uint32_t phaseTick = 0;
        uint16_t introZoom = 512;
        uint32_t randomState;
        int16_t aiLastError = 0;
        uint8_t lastShooter = 0;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct AppTraits<Apps::Tanks> {
      static constexpr Apps app = Apps::Tanks;
      static constexpr const char* icon = Screens::Symbols::tank;

      static Screens::Screen* Create(AppControllers& /*controllers*/) {
        return new Screens::Tanks();
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
