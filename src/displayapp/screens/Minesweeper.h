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

      // Touch Minesweeper. Tap a cell to reveal it, long-press to flag it, tap the face to
      // start a new game.
      class Minesweeper : public Screen {
      public:
        Minesweeper();
        ~Minesweeper() override;

        bool OnTouchEvent(TouchEvents event) override;
        bool OnTouchEvent(uint16_t x, uint16_t y) override;

      private:
        static constexpr uint8_t cols = 8;
        static constexpr uint8_t rows = 8;
        static constexpr uint8_t mineCount = 10;
        static constexpr uint8_t cellCount = cols * rows;

        enum class State : uint8_t { Playing, Won, Lost };

        void NewGame();
        void PlaceMines(uint8_t safeIndex);
        void Reveal(int8_t col, int8_t row);
        void ToggleFlag(int8_t col, int8_t row);
        void RevealAllMines();
        void RenderCell(uint8_t index);
        void UpdateHud();
        uint8_t NeighbourMines(int8_t col, int8_t row) const;
        int8_t CellAt(uint16_t x, uint16_t y, int8_t& col, int8_t& row) const;
        uint32_t NextRandom();

        bool mine[cellCount];
        bool revealed[cellCount];
        bool flagged[cellCount];
        uint8_t adjacent[cellCount];

        lv_obj_t* board; // single lv_table for the whole grid (low memory)
        lv_obj_t* mineCounter;
        lv_obj_t* face;
        lv_obj_t* timer;

        State state = State::Playing;
        bool minesPlaced = false;
        uint8_t revealedCount = 0;
        uint8_t flagsUsed = 0;
        uint32_t startTick = 0;
        uint32_t randomState;

        uint16_t lastX = 0;
        uint16_t lastY = 0;

        lv_task_t* taskRefresh;

      public:
        void Refresh() override;
      };
    }

    template <>
    struct AppTraits<Apps::Minesweeper> {
      static constexpr Apps app = Apps::Minesweeper;
      static constexpr const char* icon = Screens::Symbols::mine;

      static Screens::Screen* Create(AppControllers& /*controllers*/) {
        return new Screens::Minesweeper();
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      }
    };
  }
}
