#pragma once

#include <array>
#include <bitset>
#include <cstdint>

namespace Pinetime {
  namespace Controllers {

    // The rules of Snake, with no LVGL dependency so they can be tested off the watch.
    // The screen calls Step() once per tick and redraws only the cells the result names.
    class SnakeGame {
    public:
      static constexpr uint8_t cols = 20;
      static constexpr uint8_t rows = 18;
      static constexpr uint16_t cellCount = cols * rows;
      static constexpr uint8_t startLength = 3;
      static constexpr uint8_t maxQueuedTurns = 2;

      enum class Direction : uint8_t { Up, Down, Left, Right };

      struct Cell {
        uint8_t col;
        uint8_t row;

        bool operator==(const Cell& other) const {
          return col == other.col && row == other.row;
        }
      };

      struct StepResult {
        bool moved = false;     // false when the game is already over
        bool ate = false;       // head landed on the food; the snake grew and new food was placed
        bool died = false;      // this step ended the game (wall or self)
        bool won = false;       // this step filled the board
        bool tailFreed = false; // freedTail was vacated; false while growing
        Cell head {};
        Cell freedTail {};
      };

      explicit SnakeGame(uint32_t seed);

      void Reset();

      // Queue a turn for a coming step. Up to two turns are buffered so a quick pair of swipes
      // between ticks is not lost. A turn that repeats, or reverses, the direction the snake will
      // be travelling at that point is ignored.
      void Turn(Direction direction);

      StepResult Step();

      bool IsOver() const {
        return over;
      }

      uint16_t Score() const {
        return score;
      }

      uint16_t Length() const {
        return length;
      }

      Cell Head() const {
        return CellOf(body[headIndex]);
      }

      Cell Food() const {
        return CellOf(food);
      }

      Direction Heading() const {
        return heading;
      }

      bool Occupied(Cell cell) const {
        return occupied.test(IndexOf(cell));
      }

      // Body segments from tail (0) to head (Length() - 1), for a full redraw.
      Cell Segment(uint16_t fromTail) const {
        return CellOf(body[(tailIndex + fromTail) % cellCount]);
      }

    private:
      static uint16_t IndexOf(Cell cell) {
        return static_cast<uint16_t>(cell.row) * cols + cell.col;
      }

      static Cell CellOf(uint16_t index) {
        return {static_cast<uint8_t>(index % cols), static_cast<uint8_t>(index / cols)};
      }

      static bool Opposite(Direction a, Direction b);
      void PlaceFood();
      uint32_t NextRandom();

      // Ring buffer of cell indices; the live snake runs from tailIndex to headIndex.
      std::array<uint16_t, cellCount> body {};
      std::bitset<cellCount> occupied;
      uint16_t headIndex = 0;
      uint16_t tailIndex = 0;
      uint16_t length = 0;

      Direction heading = Direction::Right;
      std::array<Direction, maxQueuedTurns> queued {};
      uint8_t queuedCount = 0;

      uint16_t food = 0;
      uint16_t score = 0;
      bool over = false;
      uint32_t randomState;
    };
  }
}
