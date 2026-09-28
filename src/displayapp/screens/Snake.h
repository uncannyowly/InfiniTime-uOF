#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "components/snake/SnakeGame.h"
#include "displayapp/screens/Screen.h"
#include "displayapp/apps/Apps.h"
#include "displayapp/Controllers.h"
#include "Symbols.h"

namespace Pinetime {
  namespace Applications {
    namespace Screens {

      // Snake. Swipe to steer, tap to play again after a game over, press the button to quit.
      // The rules live in Controllers::SnakeGame; this screen only draws what each step changed.
      class Snake : public Screen {
      public:
        Snake();
        ~Snake() override;

        void Refresh() override;
        bool OnTouchEvent(TouchEvents event) override;

      private:
        using Game = Controllers::SnakeGame;

        static constexpr lv_coord_t cellSize = 12;
        static constexpr lv_coord_t boardTop = 24;
        static constexpr lv_coord_t boardWidth = Game::cols * cellSize;
        static constexpr lv_coord_t boardHeight = Game::rows * cellSize;
        static constexpr int stride = boardWidth / 8;

        enum class State : uint8_t { Ready, Playing, Over };

        void NewGame();
        void DrawWholeSnake();
        void SetCell(Game::Cell cell, bool on);
        void InvalidateCell(Game::Cell cell);
        void PlaceHead();
        void PlaceFood();
        void UpdateHud();
        void ShowMessage(const char* text);
        void EndGame(bool won);
        uint32_t StepInterval() const;

        Game game;
        State state = State::Ready;
        uint32_t lastStepTick = 0;

        // Board: 1-bit image, 8 byte palette followed by the bitmap. Index 0 is transparent.
        uint8_t boardBuf[8 + stride * boardHeight];
        lv_img_dsc_t boardDsc;
        lv_obj_t* board;
        lv_obj_t* head;
        lv_obj_t* food;
        lv_obj_t* labelScore;
        lv_obj_t* labelBest;
        lv_obj_t* labelMessage;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct AppTraits<Apps::Snake> {
      static constexpr Apps app = Apps::Snake;
      static constexpr const char* icon = Screens::Symbols::snake;

      static Screens::Screen* Create(AppControllers& /*controllers*/) {
        return new Screens::Snake();
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
