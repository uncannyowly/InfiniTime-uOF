#include "components/snake/SnakeGame.h"

using namespace Pinetime::Controllers;

SnakeGame::SnakeGame(uint32_t seed) : randomState {seed != 0 ? seed : 0x9e3779b9u} {
  Reset();
}

void SnakeGame::Reset() {
  occupied.reset();
  constexpr uint8_t startRow = rows / 2;
  constexpr uint8_t startCol = cols / 4; // tail; the snake starts heading right from here
  for (uint8_t i = 0; i < startLength; i++) {
    const uint16_t index = IndexOf({static_cast<uint8_t>(startCol + i), startRow});
    body[i] = index;
    occupied.set(index);
  }
  tailIndex = 0;
  headIndex = startLength - 1;
  length = startLength;

  heading = Direction::Right;
  queuedCount = 0;
  score = 0;
  over = false;
  PlaceFood();
}

bool SnakeGame::Opposite(Direction a, Direction b) {
  switch (a) {
    case Direction::Up:
      return b == Direction::Down;
    case Direction::Down:
      return b == Direction::Up;
    case Direction::Left:
      return b == Direction::Right;
    case Direction::Right:
      return b == Direction::Left;
  }
  return false;
}

void SnakeGame::Turn(Direction direction) {
  if (over || queuedCount >= maxQueuedTurns) {
    return;
  }
  const Direction last = queuedCount > 0 ? queued[queuedCount - 1] : heading;
  if (direction == last || Opposite(direction, last)) {
    return;
  }
  queued[queuedCount++] = direction;
}

SnakeGame::StepResult SnakeGame::Step() {
  StepResult result;
  if (over) {
    return result;
  }
  result.moved = true;

  if (queuedCount > 0) {
    heading = queued[0];
    for (uint8_t i = 1; i < queuedCount; i++) {
      queued[i - 1] = queued[i];
    }
    queuedCount--;
  }

  const Cell head = Head();
  result.head = head;
  int col = head.col;
  int row = head.row;
  switch (heading) {
    case Direction::Up:
      row--;
      break;
    case Direction::Down:
      row++;
      break;
    case Direction::Left:
      col--;
      break;
    case Direction::Right:
      col++;
      break;
  }
  if (col < 0 || row < 0 || col >= cols || row >= rows) {
    over = true;
    result.died = true;
    return result;
  }

  const uint16_t next = IndexOf({static_cast<uint8_t>(col), static_cast<uint8_t>(row)});
  const bool grows = next == food;
  // The tail leaves its cell on this same step unless the snake is growing, so moving into it
  // is allowed.
  const bool intoLeavingTail = !grows && next == body[tailIndex];
  if (occupied.test(next) && !intoLeavingTail) {
    over = true;
    result.died = true;
    return result;
  }

  if (!grows) {
    const uint16_t tail = body[tailIndex];
    occupied.reset(tail);
    tailIndex = (tailIndex + 1) % cellCount;
    length--;
    result.tailFreed = true;
    result.freedTail = CellOf(tail);
  }

  headIndex = (headIndex + 1) % cellCount;
  body[headIndex] = next;
  occupied.set(next);
  length++;
  result.head = CellOf(next);

  if (grows) {
    score++;
    result.ate = true;
    if (length == cellCount) {
      over = true;
      result.won = true;
    } else {
      PlaceFood();
    }
  }
  return result;
}

void SnakeGame::PlaceFood() {
  // Pick uniformly among the free cells.
  uint16_t skip = NextRandom() % (cellCount - length);
  for (uint16_t index = 0; index < cellCount; index++) {
    if (occupied.test(index)) {
      continue;
    }
    if (skip == 0) {
      food = index;
      return;
    }
    skip--;
  }
}

uint32_t SnakeGame::NextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}
