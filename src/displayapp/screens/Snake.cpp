#include "displayapp/screens/Snake.h"

#include <algorithm>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr uint32_t colorBody = 0x3fcf4f;
  constexpr uint32_t colorHead = 0xb6ff5a;
  constexpr uint32_t colorFood = 0xff4f6a;
  constexpr uint32_t colorBest = 0x33ff66;
  constexpr uint32_t colorDivider = 0x2a2a2a;
  constexpr uint32_t colorMessage = 0xffe14f;

  // A step every 180 ms at the start, 5 ms quicker per food, never faster than 80 ms.
  constexpr uint32_t startInterval = 180;
  constexpr uint32_t intervalPerFood = 5;
  constexpr uint32_t fastestInterval = 80;

  uint16_t bestScore = 0; // kept until the watch restarts

  lv_obj_t* CreateRect(lv_coord_t size, uint32_t color, lv_style_int_t radius) {
    lv_obj_t* obj = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_click(obj, false);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_local_bg_color(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_obj_set_style_local_border_width(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(obj, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, radius);
    return obj;
  }
}

Snake::Snake() : game {0x5a4e3bu ^ lv_tick_get()} {
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  labelScore = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(labelScore, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  labelBest = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(labelBest, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_style_local_text_color(labelBest, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorBest));

  lv_obj_t* divider = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(divider, false);
  lv_obj_set_size(divider, boardWidth, 1);
  lv_obj_set_pos(divider, 0, boardTop - 2);
  lv_obj_set_style_local_bg_color(divider, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorDivider));
  lv_obj_set_style_local_border_width(divider, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_radius(divider, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);

  // Palette entries are B, G, R, A. Index 0 is transparent so the black screen shows through.
  const uint32_t palette[2] = {0x000000, colorBody};
  for (int slot = 0; slot < 2; slot++) {
    boardBuf[slot * 4 + 0] = palette[slot] & 0xff;
    boardBuf[slot * 4 + 1] = (palette[slot] >> 8) & 0xff;
    boardBuf[slot * 4 + 2] = (palette[slot] >> 16) & 0xff;
    boardBuf[slot * 4 + 3] = slot == 0 ? 0x00 : 0xff;
  }
  boardDsc.header.always_zero = 0;
  boardDsc.header.w = boardWidth;
  boardDsc.header.h = boardHeight;
  boardDsc.header.cf = LV_IMG_CF_INDEXED_1BIT;
  boardDsc.data = boardBuf;
  boardDsc.data_size = sizeof(boardBuf);

  board = lv_img_create(lv_scr_act(), nullptr);
  lv_obj_set_click(board, false);
  lv_img_set_src(board, &boardDsc);
  lv_obj_set_pos(board, 0, boardTop);

  food = CreateRect(cellSize - 4, colorFood, LV_RADIUS_CIRCLE);
  head = CreateRect(cellSize - 2, colorHead, 2);

  labelMessage = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(labelMessage, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(labelMessage, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorMessage));
  lv_obj_set_style_local_bg_color(labelMessage, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(labelMessage, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_70);

  NewGame();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Snake::~Snake() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void Snake::NewGame() {
  game.Reset();
  state = State::Ready;
  DrawWholeSnake();
  PlaceHead();
  PlaceFood();
  UpdateHud();
  ShowMessage("SWIPE\nTO START");
}

uint32_t Snake::StepInterval() const {
  const uint32_t speedUp = static_cast<uint32_t>(game.Score()) * intervalPerFood;
  return speedUp >= startInterval - fastestInterval ? fastestInterval : startInterval - speedUp;
}

void Snake::Refresh() {
  if (state != State::Playing || lv_tick_elaps(lastStepTick) < StepInterval()) {
    return;
  }
  lastStepTick = lv_tick_get();

  const Game::StepResult result = game.Step();
  if (result.died) {
    EndGame(false);
    return;
  }
  // Clear the tail before drawing the head: they are the same cell when the snake chases its tail.
  if (result.tailFreed) {
    SetCell(result.freedTail, false);
    InvalidateCell(result.freedTail);
  }
  SetCell(result.head, true);
  InvalidateCell(result.head);
  PlaceHead();

  if (result.ate) {
    UpdateHud();
    if (result.won) {
      EndGame(true);
      return;
    }
    PlaceFood();
  }
}

bool Snake::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  Game::Direction direction;
  switch (event) {
    case TouchEvents::SwipeUp:
      direction = Game::Direction::Up;
      break;
    case TouchEvents::SwipeDown:
      direction = Game::Direction::Down;
      break;
    case TouchEvents::SwipeLeft:
      direction = Game::Direction::Left;
      break;
    case TouchEvents::SwipeRight:
      direction = Game::Direction::Right;
      break;
    case TouchEvents::Tap:
      if (state == State::Over) {
        NewGame();
      }
      return true;
    default:
      return true; // swallow every gesture so none of them navigates away mid-game
  }

  if (state == State::Over) {
    return true;
  }
  game.Turn(direction);
  if (state == State::Ready) {
    state = State::Playing;
    lastStepTick = lv_tick_get();
    lv_obj_set_hidden(labelMessage, true);
  }
  return true;
}

void Snake::EndGame(bool won) {
  state = State::Over;
  bestScore = std::max(bestScore, game.Score());
  UpdateHud();
  lv_label_set_text_fmt(labelMessage, "%s\nSCORE %u\nTAP TO PLAY", won ? "YOU WIN" : "GAME OVER", static_cast<unsigned>(game.Score()));
  lv_obj_set_hidden(labelMessage, false);
  lv_obj_align(labelMessage, nullptr, LV_ALIGN_CENTER, 0, boardTop / 2);
}

void Snake::ShowMessage(const char* text) {
  lv_label_set_text_static(labelMessage, text);
  lv_obj_set_hidden(labelMessage, false);
  // Below the board's middle row, where the snake starts, so it stays visible before the first swipe.
  lv_obj_align(labelMessage, nullptr, LV_ALIGN_CENTER, 0, boardTop / 2 + 6 * cellSize);
}

void Snake::UpdateHud() {
  lv_label_set_text_fmt(labelScore, "SCORE %u", static_cast<unsigned>(game.Score()));
  lv_obj_set_pos(labelScore, 4, 4);
  lv_label_set_text_fmt(labelBest, "BEST %u", static_cast<unsigned>(std::max(bestScore, game.Score())));
  lv_obj_align(labelBest, nullptr, LV_ALIGN_IN_TOP_RIGHT, -4, 4);
}

void Snake::SetCell(Game::Cell cell, bool on) {
  // Each cell is drawn 10x10 inside its 12x12 slot, leaving a gap between segments.
  const int x0 = cell.col * cellSize + 1;
  const int y0 = cell.row * cellSize + 1;
  for (int y = y0; y < y0 + cellSize - 2; y++) {
    uint8_t* row = boardBuf + 8 + y * stride;
    for (int x = x0; x < x0 + cellSize - 2; x++) {
      const uint8_t bit = 0x80 >> (x % 8);
      if (on) {
        row[x / 8] |= bit;
      } else {
        row[x / 8] &= ~bit;
      }
    }
  }
}

void Snake::InvalidateCell(Game::Cell cell) {
  lv_area_t area;
  area.x1 = cell.col * cellSize;
  area.y1 = boardTop + cell.row * cellSize;
  area.x2 = area.x1 + cellSize - 1;
  area.y2 = area.y1 + cellSize - 1;
  lv_obj_invalidate_area(board, &area);
}

void Snake::DrawWholeSnake() {
  std::fill(boardBuf + 8, boardBuf + sizeof(boardBuf), 0);
  for (uint16_t i = 0; i < game.Length(); i++) {
    SetCell(game.Segment(i), true);
  }
  lv_obj_invalidate(board);
}

void Snake::PlaceHead() {
  const Game::Cell cell = game.Head();
  lv_obj_set_pos(head, cell.col * cellSize + 1, boardTop + cell.row * cellSize + 1);
}

void Snake::PlaceFood() {
  const Game::Cell cell = game.Food();
  lv_obj_set_pos(food, cell.col * cellSize + 2, boardTop + cell.row * cellSize + 2);
}
