// Host-side tests for Pinetime::Controllers::SnakeGame (no watch or LVGL needed). From the repo root:
//   g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined -Isrc tests/snake/test_snake.cpp src/components/snake/SnakeGame.cpp -o /tmp/test_snake && /tmp/test_snake
#include "components/snake/SnakeGame.h"
#include <cstdio>
#include <cstdlib>

using Pinetime::Controllers::SnakeGame;
using D = SnakeGame::Direction;
using C = SnakeGame::Cell;

static int failures = 0;
#define CHECK(cond)                                                                                  \
  do {                                                                                               \
    if (!(cond)) {                                                                                   \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                  \
      failures++;                                                                                    \
    }                                                                                                \
  } while (0)

// Walk a fresh game with food kept out of the way so movement is predictable.
static void TestStartState() {
  std::puts("start state");
  SnakeGame g(1);
  CHECK(g.Length() == SnakeGame::startLength);
  CHECK(g.Score() == 0);
  CHECK(!g.IsOver());
  CHECK(g.Heading() == D::Right);
  C head = g.Head();
  CHECK(head.row == SnakeGame::rows / 2);
  for (uint16_t i = 0; i < g.Length(); i++) {
    CHECK(g.Occupied(g.Segment(i)));
  }
  CHECK(g.Segment(g.Length() - 1) == head);
  CHECK(!g.Occupied(g.Food()));
}

static void TestMovesAndFreesTail() {
  std::puts("moves forward and frees tail");
  SnakeGame g(7);
  C head = g.Head();
  C tail = g.Segment(0);
  // step until we are not about to eat, to test plain movement
  auto r = g.Step();
  if (!r.ate) {
    CHECK(r.moved);
    CHECK(r.head.col == head.col + 1 && r.head.row == head.row);
    CHECK(r.tailFreed);
    CHECK(r.freedTail == tail);
    CHECK(!g.Occupied(tail));
    CHECK(g.Length() == SnakeGame::startLength);
  }
}

static void TestIgnoresReverseAndRepeat() {
  std::puts("ignores reverse and repeat");
  SnakeGame g(3);
  g.Turn(D::Left);  // reverse of Right: ignored
  g.Turn(D::Right); // same: ignored
  g.Step();
  CHECK(g.Heading() == D::Right);
}

static void TestQueuesTwoTurns() {
  std::puts("queues two turns");
  SnakeGame g(3);
  g.Turn(D::Up);
  g.Turn(D::Left); // valid after Up even though it reverses the current heading
  g.Turn(D::Down); // queue full: dropped
  g.Step();
  CHECK(g.Heading() == D::Up);
  g.Step();
  CHECK(g.Heading() == D::Left);
  g.Step();
  CHECK(g.Heading() == D::Left);
}

static void TestQueuedReverseCheckedAgainstQueue() {
  std::puts("reverse checked against last queued turn");
  SnakeGame g(3);
  g.Turn(D::Up);
  g.Turn(D::Down); // reverse of the queued Up: ignored
  g.Step();
  g.Step();
  CHECK(g.Heading() == D::Up);
}

static void TestWallKills() {
  std::puts("wall kills");
  SnakeGame g(5);
  g.Turn(D::Up);
  SnakeGame::StepResult r;
  int steps = 0;
  do {
    r = g.Step();
    steps++;
  } while (!g.IsOver() && steps < 100);
  CHECK(g.IsOver());
  CHECK(r.died);
  CHECK(!r.won);
  // started mid-board; must die within rows steps (or sooner by eating, never later)
  CHECK(steps <= SnakeGame::rows);
  auto after = g.Step();
  CHECK(!after.moved);
}

static void TestMoveIntoVacatingTailIsLegal() {
  std::puts("moving into the cell the tail vacates is legal");
  // Reach length 4 heading Right, then turn Up, Left, Down, Right: a 2x2 loop whose fourth move
  // enters the cell the tail leaves on that same step.
  for (uint32_t seed = 1; seed < 5000; seed++) {
    SnakeGame g(seed);
    bool grew = false;
    for (int i = 0; i < SnakeGame::cols && !g.IsOver(); i++) {
      if (g.Step().ate) {
        grew = true;
        break;
      }
    }
    if (!grew || g.IsOver() || g.Heading() != D::Right) {
      continue;
    }
    C h = g.Head();
    if (h.row < 1 || h.col < 1) {
      continue;
    }
    bool ateDuringLoop = false;
    SnakeGame::StepResult r;
    for (D turn : {D::Up, D::Left, D::Down, D::Right}) {
      g.Turn(turn);
      r = g.Step();
      ateDuringLoop |= r.ate;
    }
    if (ateDuringLoop) {
      continue; // food sat on the loop; that is a different case
    }
    CHECK(g.Length() == 4);
    CHECK(!r.died);
    CHECK(!g.IsOver());
    CHECK(r.tailFreed);
    CHECK(r.head == r.freedTail);
    CHECK(r.head == h);
    return;
  }
  CHECK(!"no seed set up a 2x2 loop");
}

// Differential test: an obviously-correct reference snake (a plain array scanned linearly)
// replays the same moves. Food positions are taken from the game under test, whose placement
// is checked separately.
static void TestAgainstReferenceModel() {
  std::puts("matches a reference model over random games");
  int selfDeaths = 0, wallDeaths = 0, meals = 0, tailChases = 0;
  for (uint32_t seed = 1; seed < 3000; seed++) {
    SnakeGame g(seed);
    C ref[SnakeGame::cellCount];
    int refLen = g.Length();
    for (int i = 0; i < refLen; i++) {
      ref[i] = g.Segment(i); // ref[refLen - 1] is the head
    }
    D heading = D::Right;
    uint32_t rng = seed * 2654435761u;
    for (int step = 0; step < 2000 && !g.IsOver(); step++) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      const C food = g.Food();
      const C head = ref[refLen - 1];
      auto isReverse = [](D a, D b) {
        return (a == D::Up && b == D::Down) || (a == D::Down && b == D::Up) || (a == D::Left && b == D::Right) ||
               (a == D::Right && b == D::Left);
      };
      auto hitsWall = [&](D d) {
        int c = head.col + (d == D::Right) - (d == D::Left);
        int r = head.row + (d == D::Down) - (d == D::Up);
        return c < 0 || r < 0 || c >= SnakeGame::cols || r >= SnakeGame::rows;
      };
      // A player that mostly chases food and mostly avoids walls, so snakes grow long enough to
      // run into themselves, with enough randomness left to still die on walls sometimes.
      D want = heading;
      switch (rng % 4) {
        case 0:
          want = static_cast<D>((rng >> 8) % 4);
          break;
        case 1:
        case 2:
          want = food.col > head.col   ? D::Right
                 : food.col < head.col ? D::Left
                 : food.row > head.row ? D::Down
                                       : D::Up;
          break;
        default:
          break;
      }
      if (isReverse(want, heading)) {
        want = heading;
      }
      if (hitsWall(want) && (rng >> 16) % 10 != 0) {
        for (D d : {D::Up, D::Down, D::Left, D::Right}) {
          if (!hitsWall(d) && !isReverse(d, heading)) {
            want = d;
            break;
          }
        }
      }
      if (want != heading) {
        g.Turn(want);
        heading = want;
      }
      int col = head.col, row = head.row;
      col += heading == D::Right ? 1 : heading == D::Left ? -1 : 0;
      row += heading == D::Down ? 1 : heading == D::Up ? -1 : 0;

      auto r = g.Step();
      CHECK(r.moved);
      if (col < 0 || row < 0 || col >= SnakeGame::cols || row >= SnakeGame::rows) {
        CHECK(r.died);
        wallDeaths++;
        break;
      }
      const C next {static_cast<uint8_t>(col), static_cast<uint8_t>(row)};
      const bool grows = next == food;
      bool hit = false;
      for (int i = grows ? 0 : 1; i < refLen; i++) { // the tail moves away unless growing
        hit |= ref[i] == next;
      }
      if (!grows && next == ref[0]) {
        tailChases++;
      }
      if (hit) {
        CHECK(r.died);
        selfDeaths++;
        break;
      }
      CHECK(!r.died);
      CHECK(r.head == next);
      CHECK(r.ate == grows);
      if (grows) {
        meals++;
        ref[refLen++] = next;
      } else {
        CHECK(r.freedTail == ref[0]);
        for (int i = 1; i < refLen; i++) {
          ref[i - 1] = ref[i];
        }
        ref[refLen - 1] = next;
      }
      CHECK(g.Length() == refLen);
    }
  }
  std::printf("  exercised: %d self deaths, %d wall deaths, %d meals, %d tail chases\n", selfDeaths, wallDeaths, meals,
              tailChases);
  CHECK(selfDeaths > 0);
  CHECK(wallDeaths > 0);
  CHECK(meals > 0);
}

static void TestEatingGrowsAndScores() {
  std::puts("eating grows and scores");
  for (uint32_t seed = 1; seed < 2000; seed++) {
    SnakeGame g(seed);
    C food = g.Food();
    C head = g.Head();
    if (food.row != head.row || food.col <= head.col) {
      continue; // want food straight ahead
    }
    SnakeGame::StepResult r;
    for (int i = 0; i < food.col - head.col; i++) {
      r = g.Step();
    }
    CHECK(r.ate);
    CHECK(!r.tailFreed);
    CHECK(g.Score() == 1);
    CHECK(g.Length() == SnakeGame::startLength + 1);
    CHECK(!g.Occupied(g.Food()));
    CHECK(!(g.Food() == r.head));
    return;
  }
  CHECK(!"no seed put food straight ahead");
}

static void TestResetRestores() {
  std::puts("reset restores");
  SnakeGame g(11);
  g.Turn(D::Up);
  for (int i = 0; i < 100 && !g.IsOver(); i++) {
    g.Step();
  }
  CHECK(g.IsOver());
  g.Reset();
  CHECK(!g.IsOver());
  CHECK(g.Score() == 0);
  CHECK(g.Length() == SnakeGame::startLength);
  CHECK(g.Heading() == D::Right);
  uint16_t occupiedCount = 0;
  for (uint8_t r = 0; r < SnakeGame::rows; r++) {
    for (uint8_t c = 0; c < SnakeGame::cols; c++) {
      occupiedCount += g.Occupied({c, r}) ? 1 : 0;
    }
  }
  CHECK(occupiedCount == SnakeGame::startLength);
}

static void TestFoodNeverOnSnakeAcrossManyGames() {
  std::puts("food never lands on the snake (fuzz)");
  for (uint32_t seed = 1; seed < 300; seed++) {
    SnakeGame g(seed);
    uint32_t rng = seed * 2654435761u;
    for (int i = 0; i < 400 && !g.IsOver(); i++) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      if (rng % 4 == 0) {
        g.Turn(static_cast<D>((rng >> 8) % 4));
      }
      g.Step();
      if (!g.IsOver()) {
        CHECK(!g.Occupied(g.Food()) || g.Length() == SnakeGame::cellCount);
        CHECK(g.Occupied(g.Head()));
      }
    }
  }
}

int main() {
  TestStartState();
  TestMovesAndFreesTail();
  TestIgnoresReverseAndRepeat();
  TestQueuesTwoTurns();
  TestQueuedReverseCheckedAgainstQueue();
  TestWallKills();
  TestMoveIntoVacatingTailIsLegal();
  TestAgainstReferenceModel();
  TestEatingGrowsAndScores();
  TestResetRestores();
  TestFoodNeverOnSnakeAcrossManyGames();
  std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
  return failures == 0 ? 0 : 1;
}
