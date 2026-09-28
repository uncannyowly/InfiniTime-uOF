#include "displayapp/screens/Minesweeper.h"

#include <cstdio>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr lv_coord_t screenSize = 240;
  constexpr lv_coord_t hudHeight = 22;
  constexpr lv_coord_t cellSize = 27;
  constexpr lv_coord_t gridLeft = (screenSize - 8 * cellSize) / 2; // 8 == cols
  constexpr lv_coord_t gridTop = hudHeight + 2;

  constexpr uint32_t colorHidden = 0x4a4a5a;
  constexpr uint32_t colorRevealed = 0x20202a;
  constexpr uint32_t colorBorder = 0x101018;
  constexpr uint32_t colorFlag = 0xff5544;
  constexpr uint32_t colorMine = 0xff3344;
  constexpr uint32_t colorHud = 0xffcc33;
  constexpr uint32_t colorNumber = 0x8fd0ff;

  // lv_table cell types (1-based in LVGL)
  enum CellType : uint8_t { TypeHidden = 1, TypeRevealed = 2, TypeMine = 3, TypeFlag = 4 };

  constexpr lv_coord_t hudRow = 22; // room for the HUD row
}

Minesweeper::Minesweeper() {
  randomState = 0x2545f491u ^ lv_tick_get();
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  mineCounter = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(mineCounter, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_set_style_local_text_color(mineCounter, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorFlag));
  lv_obj_align(mineCounter, nullptr, LV_ALIGN_IN_TOP_LEFT, 6, 2);

  face = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(face, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_align(face, nullptr, LV_ALIGN_IN_TOP_MID, 0, 2);

  timer = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(timer, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_set_style_local_text_color(timer, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorHud));
  lv_label_set_text_static(timer, "000");
  lv_obj_align(timer, nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, 2);

  board = lv_table_create(lv_scr_act(), nullptr);
  lv_table_set_col_cnt(board, cols);
  lv_table_set_row_cnt(board, rows);
  for (uint8_t c = 0; c < cols; c++) {
    lv_table_set_col_width(board, c, cellSize);
  }
  // cell padding tuned so each row is about cellSize tall
  const struct {
    uint8_t part;
    uint32_t bg;
    uint32_t text;
  } styles[] = {
    {LV_TABLE_PART_CELL1, colorHidden, colorHidden},
    {LV_TABLE_PART_CELL2, colorRevealed, colorNumber},
    {LV_TABLE_PART_CELL3, colorMine, 0x000000},
    {LV_TABLE_PART_CELL4, colorHidden, colorFlag},
  };
  for (const auto& st : styles) {
    lv_obj_set_style_local_bg_color(board, st.part, LV_STATE_DEFAULT, lv_color_hex(st.bg));
    lv_obj_set_style_local_bg_opa(board, st.part, LV_STATE_DEFAULT, LV_OPA_COVER);
    lv_obj_set_style_local_text_color(board, st.part, LV_STATE_DEFAULT, lv_color_hex(st.text));
    lv_obj_set_style_local_text_font(board, st.part, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
    lv_obj_set_style_local_border_color(board, st.part, LV_STATE_DEFAULT, lv_color_hex(colorBorder));
    lv_obj_set_style_local_border_width(board, st.part, LV_STATE_DEFAULT, 1);
    lv_obj_set_style_local_pad_top(board, st.part, LV_STATE_DEFAULT, 3);
    lv_obj_set_style_local_pad_bottom(board, st.part, LV_STATE_DEFAULT, 3);
    lv_obj_set_style_local_pad_left(board, st.part, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_pad_right(board, st.part, LV_STATE_DEFAULT, 0);
  }
  for (uint8_t r = 0; r < rows; r++) {
    for (uint8_t c = 0; c < cols; c++) {
      lv_table_set_cell_align(board, r, c, LV_LABEL_ALIGN_CENTER);
    }
  }
  lv_obj_align(board, nullptr, LV_ALIGN_IN_TOP_MID, 0, hudRow);

  NewGame();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Minesweeper::~Minesweeper() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

uint32_t Minesweeper::NextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

void Minesweeper::NewGame() {
  for (uint8_t i = 0; i < cellCount; i++) {
    mine[i] = false;
    revealed[i] = false;
    flagged[i] = false;
    adjacent[i] = 0;
  }
  state = State::Playing;
  minesPlaced = false;
  revealedCount = 0;
  flagsUsed = 0;
  startTick = 0;
  for (uint8_t i = 0; i < cellCount; i++) {
    RenderCell(i);
  }
  UpdateHud();
}

void Minesweeper::PlaceMines(uint8_t safeIndex) {
  uint8_t placed = 0;
  while (placed < mineCount) {
    const uint8_t index = NextRandom() % cellCount;
    if (index == safeIndex || mine[index]) {
      continue;
    }
    mine[index] = true;
    placed++;
  }
  for (int8_t row = 0; row < static_cast<int8_t>(rows); row++) {
    for (int8_t col = 0; col < static_cast<int8_t>(cols); col++) {
      adjacent[row * cols + col] = NeighbourMines(col, row);
    }
  }
  minesPlaced = true;
  startTick = lv_tick_get();
}

uint8_t Minesweeper::NeighbourMines(int8_t col, int8_t row) const {
  uint8_t count = 0;
  for (int8_t dy = -1; dy <= 1; dy++) {
    for (int8_t dx = -1; dx <= 1; dx++) {
      if (dx == 0 && dy == 0) {
        continue;
      }
      const int8_t nc = col + dx;
      const int8_t nr = row + dy;
      if (nc >= 0 && nc < static_cast<int8_t>(cols) && nr >= 0 && nr < static_cast<int8_t>(rows) && mine[nr * cols + nc]) {
        count++;
      }
    }
  }
  return count;
}

void Minesweeper::Reveal(int8_t col, int8_t row) {
  if (col < 0 || col >= static_cast<int8_t>(cols) || row < 0 || row >= static_cast<int8_t>(rows)) {
    return;
  }
  const uint8_t index = row * cols + col;
  if (revealed[index] || flagged[index]) {
    return;
  }
  revealed[index] = true;
  revealedCount++;
  RenderCell(index);

  if (mine[index]) {
    state = State::Lost;
    RevealAllMines();
    UpdateHud();
    return;
  }

  // flood fill across empty (zero) regions
  if (adjacent[index] == 0) {
    for (int8_t dy = -1; dy <= 1; dy++) {
      for (int8_t dx = -1; dx <= 1; dx++) {
        if (dx != 0 || dy != 0) {
          Reveal(col + dx, row + dy);
        }
      }
    }
  }

  if (revealedCount == cellCount - mineCount) {
    state = State::Won;
    UpdateHud();
  }
}

void Minesweeper::ToggleFlag(int8_t col, int8_t row) {
  const uint8_t index = row * cols + col;
  if (revealed[index]) {
    return;
  }
  flagged[index] = !flagged[index];
  flagsUsed += flagged[index] ? 1 : -1;
  RenderCell(index);
  UpdateHud();
}

void Minesweeper::RevealAllMines() {
  for (uint8_t i = 0; i < cellCount; i++) {
    if (mine[i]) {
      revealed[i] = true;
      RenderCell(i);
    }
  }
}

void Minesweeper::RenderCell(uint8_t index) {
  const uint8_t r = index / cols;
  const uint8_t c = index % cols;
  const bool isRevealed = revealed[index];
  if (!isRevealed) {
    lv_table_set_cell_type(board, r, c, flagged[index] ? TypeFlag : TypeHidden);
    lv_table_set_cell_value(board, r, c, flagged[index] ? "P" : "");
  } else if (mine[index]) {
    lv_table_set_cell_type(board, r, c, TypeMine);
    lv_table_set_cell_value(board, r, c, "*");
  } else if (adjacent[index] > 0) {
    char digit[2] = {static_cast<char>('0' + adjacent[index]), '\0'};
    lv_table_set_cell_type(board, r, c, TypeRevealed);
    lv_table_set_cell_value(board, r, c, digit);
  } else {
    lv_table_set_cell_type(board, r, c, TypeRevealed);
    lv_table_set_cell_value(board, r, c, "");
  }
}

void Minesweeper::UpdateHud() {
  const int remaining = static_cast<int>(mineCount) - static_cast<int>(flagsUsed);
  lv_label_set_text_fmt(mineCounter, "%02d", remaining);
  switch (state) {
    case State::Playing:
      lv_label_set_text_static(face, ":)");
      lv_obj_set_style_local_text_color(face, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorHud));
      break;
    case State::Won:
      lv_label_set_text_static(face, "B)");
      lv_obj_set_style_local_text_color(face, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x4fd06a));
      break;
    case State::Lost:
      lv_label_set_text_static(face, "X(");
      lv_obj_set_style_local_text_color(face, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorMine));
      break;
  }
  lv_obj_align(face, nullptr, LV_ALIGN_IN_TOP_MID, 0, 2);
  lv_obj_align(timer, nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, 2);
}

int8_t Minesweeper::CellAt(uint16_t x, uint16_t y, int8_t& col, int8_t& row) const {
  const lv_coord_t left = lv_obj_get_x(board);
  const lv_coord_t top = lv_obj_get_y(board);
  const lv_coord_t cellW = lv_obj_get_width(board) / cols;
  const lv_coord_t cellH = lv_obj_get_height(board) / rows;
  if (cellW <= 0 || cellH <= 0 || x < left || y < top) {
    return -1;
  }
  col = (x - left) / cellW;
  row = (y - top) / cellH;
  if (col < 0 || col >= static_cast<int8_t>(cols) || row < 0 || row >= static_cast<int8_t>(rows)) {
    return -1;
  }
  return row * cols + col;
}

bool Minesweeper::OnTouchEvent(uint16_t x, uint16_t y) {
  lastX = x;
  lastY = y;
  return true;
}

bool Minesweeper::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  // face area (top ~22px) restarts the game
  if (lastY < hudHeight && (event == TouchEvents::Tap || event == TouchEvents::LongTap)) {
    NewGame();
    return true;
  }
  if (state != State::Playing) {
    if (event == TouchEvents::Tap) {
      NewGame();
      return true;
    }
    return true;
  }

  int8_t col, row;
  if (CellAt(lastX, lastY, col, row) < 0) {
    return true;
  }

  if (event == TouchEvents::LongTap) {
    ToggleFlag(col, row);
    return true;
  }
  if (event == TouchEvents::Tap) {
    if (!minesPlaced) {
      PlaceMines(row * cols + col);
    }
    if (!flagged[row * cols + col]) {
      Reveal(col, row);
    }
    return true;
  }
  return true;
}

void Minesweeper::Refresh() {
  // Churn the RNG until the first tap so the (human) tap time seeds the layout, not just boot ticks.
  if (state == State::Playing && !minesPlaced) {
    NextRandom();
  }
  if (state == State::Playing && minesPlaced) {
    const uint32_t seconds = (lv_tick_get() - startTick) / 1000;
    lv_label_set_text_fmt(timer, "%03lu", static_cast<unsigned long>(seconds > 999 ? 999 : seconds));
    lv_obj_align(timer, nullptr, LV_ALIGN_IN_TOP_RIGHT, -6, 2);
  }
}
