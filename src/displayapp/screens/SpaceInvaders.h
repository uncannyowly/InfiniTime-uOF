#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Controllers {
    class FS;
  }

  namespace Applications {
    namespace Screens {

      // Space Invaders style shooter. The sprites live in the resources package (external flash)
      // and are copied to RAM when the game starts.
      class SpaceInvaders : public Screen {
      public:
        explicit SpaceInvaders(Controllers::FS& fs);
        ~SpaceInvaders() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;

        static bool IsAvailable(Controllers::FS& fs);

      private:
        enum Sprite : uint8_t { Scout0, Scout1, Drone0, Drone1, Brute0, Brute1, Cannon, Ufo, Boom, SpriteCount };

        struct SpriteImage {
          lv_img_dsc_t descriptor;
          uint8_t file[96];
        };

        static constexpr int columns = 6;
        static constexpr int rows = 5;
        static constexpr int maxBombs = 3;
        static constexpr int shieldCount = 3;
        static constexpr int shieldBlocks = 4;

        bool LoadSprites(Controllers::FS& fs);
        const lv_img_dsc_t* InvaderImage(int row) const;
        lv_obj_t* CreateSprite(Sprite sprite);
        lv_obj_t* CreateRect(lv_coord_t w, lv_coord_t h, uint32_t color);

        void StartGame();
        void StartWave();
        void StepFormation();
        void MoveCannon();
        void MoveBullet();
        void DropBomb();
        void MoveBombs();
        void MoveUfo();
        void PlayerHit();
        void Explode(lv_coord_t centerX, lv_coord_t centerY);
        bool HitShield(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h);
        void UpdateScore();
        void ShowMessage(const char* text);
        uint32_t NextRandom();

        SpriteImage sprites[SpriteCount];
        bool spritesLoaded = false;

        lv_obj_t* formation; // container: moving it moves every invader with one redraw
        lv_obj_t* invaders[rows][columns];
        bool alive[rows][columns];
        int aliveCount = 0;
        lv_coord_t formationX = 0;
        lv_coord_t formationY = 0;
        int8_t formationDirection = 1;
        uint8_t animationFrame = 0;
        uint32_t lastStepTick = 0;

        lv_obj_t* cannon;
        lv_coord_t cannonX = 0;
        lv_coord_t targetX = 0;

        lv_obj_t* bullet;
        bool bulletActive = false;
        lv_coord_t bulletX = 0;
        lv_coord_t bulletY = 0;

        lv_obj_t* bombs[maxBombs];
        bool bombActive[maxBombs];
        lv_coord_t bombX[maxBombs];
        lv_coord_t bombY[maxBombs];
        uint32_t lastBombTick = 0;

        lv_obj_t* ufo;
        bool ufoActive = false;
        lv_coord_t ufoX = 0;
        int8_t ufoDirection = 1;
        uint32_t nextUfoTick = 0;

        lv_obj_t* shields[shieldCount][shieldBlocks];
        uint8_t shieldHealth[shieldCount][shieldBlocks];

        lv_obj_t* boom;
        uint32_t boomUntilTick = 0;

        lv_obj_t* labelScore;
        lv_obj_t* labelLives;
        lv_obj_t* labelMessage;

        uint32_t score = 0;
        uint8_t lives = 0;
        uint8_t wave = 0;
        bool gameOver = false;
        uint32_t pausedUntilTick = 0;
        uint32_t randomState;

        lv_task_t* taskRefresh = nullptr;
      };
    }

    template <>
    struct AppTraits<Apps::SpaceInvaders> {
      static constexpr Apps app = Apps::SpaceInvaders;
      static constexpr const char* icon = Screens::Symbols::spaceInvader;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::SpaceInvaders(controllers.filesystem);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& filesystem) {
        return Screens::SpaceInvaders::IsAvailable(filesystem);
      };
    };
  }
}
