#include "displayapp/screens/SpaceInvaders.h"

#include <algorithm>
#include <cstring>
#include "components/fs/FS.h"

using namespace Pinetime::Applications::Screens;

namespace {
  struct SpriteFile {
    const char* path;
    uint32_t color;
  };

  // Order matches SpaceInvaders::Sprite
  constexpr SpriteFile spriteFiles[] = {
    {"/images/si_scout0.bin", 0xff4fd8},
    {"/images/si_scout1.bin", 0xff4fd8},
    {"/images/si_drone0.bin", 0x4ff0ff},
    {"/images/si_drone1.bin", 0x4ff0ff},
    {"/images/si_brute0.bin", 0x7dff4f},
    {"/images/si_brute1.bin", 0x7dff4f},
    {"/images/si_cannon.bin", 0x33ff66},
    {"/images/si_ufo.bin", 0xff3344},
    {"/images/si_boom.bin", 0xffe14f},
  };

  constexpr lv_coord_t screenSize = 240;
  constexpr lv_coord_t hudHeight = 18;
  constexpr lv_coord_t cellWidth = 30;
  constexpr lv_coord_t cellHeight = 20;
  constexpr lv_coord_t formationTop = 36;
  constexpr lv_coord_t shieldY = 182;
  constexpr lv_coord_t shieldBlockWidth = 12;
  constexpr lv_coord_t shieldBlockHeight = 6;
  constexpr lv_coord_t shieldCenters[] = {48, 120, 192};
  constexpr lv_coord_t cannonY = 216;
  constexpr lv_coord_t ufoY = hudHeight + 2;

  constexpr lv_coord_t bulletWidth = 2;
  constexpr lv_coord_t bulletHeight = 8;
  constexpr lv_coord_t bulletSpeed = 8;
  constexpr lv_coord_t bombWidth = 3;
  constexpr lv_coord_t bombHeight = 7;
  constexpr lv_coord_t bombSpeed = 3;
  constexpr lv_coord_t cannonSpeed = 5;
  constexpr lv_coord_t ufoSpeed = 2;

  constexpr uint32_t rowPoints[] = {30, 20, 20, 10, 10};
  constexpr uint32_t ufoPoints[] = {50, 100, 150, 300};

  constexpr uint32_t colorShield = 0x33ff66;
  constexpr uint32_t colorShieldDamaged = 0x1a7a33;

  uint32_t highScore = 0; // kept until the watch restarts

  bool Overlaps(lv_coord_t ax, lv_coord_t ay, lv_coord_t aw, lv_coord_t ah, lv_coord_t bx, lv_coord_t by, lv_coord_t bw, lv_coord_t bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
  }
}

bool SpaceInvaders::IsAvailable(Pinetime::Controllers::FS& fs) {
  for (const auto& sprite : spriteFiles) {
    lfs_info info;
    if (fs.Stat(sprite.path, &info) != LFS_ERR_OK) {
      return false;
    }
  }
  return true;
}

SpaceInvaders::SpaceInvaders(Pinetime::Controllers::FS& fs) {
  randomState = 0x9e3779b9u ^ lv_tick_get();
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  labelScore = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(labelScore, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_pos(labelScore, 4, 1);
  labelLives = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(labelLives, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_style_local_text_color(labelLives, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x33ff66));

  labelMessage = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(labelMessage, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(labelMessage, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xffe14f));

  spritesLoaded = LoadSprites(fs);
  if (!spritesLoaded) {
    ShowMessage("Sprites missing\nInstall the\nresources package");
    return;
  }

  formation = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(formation, false);
  lv_obj_set_size(formation, columns * cellWidth, rows * cellHeight);
  lv_obj_set_style_local_bg_opa(formation, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(formation, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_all(formation, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      invaders[row][column] = lv_img_create(formation, nullptr);
      lv_obj_set_click(invaders[row][column], false);
    }
  }
  for (int shield = 0; shield < shieldCount; shield++) {
    for (int block = 0; block < shieldBlocks; block++) {
      shields[shield][block] = CreateRect(shieldBlockWidth, shieldBlockHeight, colorShield);
      lv_obj_set_pos(shields[shield][block],
                     shieldCenters[shield] - shieldBlockWidth + (block % 2) * shieldBlockWidth,
                     shieldY + (block / 2) * shieldBlockHeight);
    }
  }
  cannon = CreateSprite(Cannon);
  ufo = CreateSprite(Ufo);
  boom = CreateSprite(Boom);
  bullet = CreateRect(bulletWidth, bulletHeight, 0xffffff);
  for (auto& bomb : bombs) {
    bomb = CreateRect(bombWidth, bombHeight, 0xff9933);
  }
  // keep the message above the playfield
  lv_obj_move_foreground(labelMessage);

  StartGame();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

SpaceInvaders::~SpaceInvaders() {
  if (taskRefresh != nullptr) {
    lv_task_del(taskRefresh);
  }
  lv_obj_clean(lv_scr_act());
}

bool SpaceInvaders::LoadSprites(Pinetime::Controllers::FS& fs) {
  for (int i = 0; i < SpriteCount; i++) {
    lfs_info info;
    if (fs.Stat(spriteFiles[i].path, &info) != LFS_ERR_OK || info.size > sizeof(sprites[i].file) || info.size < 12) {
      return false;
    }
    lfs_file_t file;
    if (fs.FileOpen(&file, spriteFiles[i].path, LFS_O_RDONLY) < 0) {
      return false;
    }
    const int read = fs.FileRead(&file, sprites[i].file, info.size);
    fs.FileClose(&file);
    if (read != static_cast<int>(info.size)) {
      return false;
    }

    // LVGL binary image: 4 byte header, then a 2 entry palette (B, G, R, A) and the 1 bpp bitmap.
    // The converter stores the lit colour as white, so paint it here.
    uint8_t* litColor = sprites[i].file + 8;
    litColor[0] = spriteFiles[i].color & 0xff;
    litColor[1] = (spriteFiles[i].color >> 8) & 0xff;
    litColor[2] = (spriteFiles[i].color >> 16) & 0xff;
    litColor[3] = 0xff;

    std::memcpy(&sprites[i].descriptor.header, sprites[i].file, sizeof(lv_img_header_t));
    sprites[i].descriptor.data = sprites[i].file + sizeof(lv_img_header_t);
    sprites[i].descriptor.data_size = info.size - sizeof(lv_img_header_t);
  }
  return true;
}

lv_obj_t* SpaceInvaders::CreateSprite(Sprite sprite) {
  lv_obj_t* image = lv_img_create(lv_scr_act(), nullptr);
  lv_obj_set_click(image, false);
  lv_img_set_src(image, &sprites[sprite].descriptor);
  lv_obj_set_hidden(image, true);
  return image;
}

lv_obj_t* SpaceInvaders::CreateRect(lv_coord_t w, lv_coord_t h, uint32_t color) {
  lv_obj_t* rect = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_click(rect, false);
  lv_obj_set_size(rect, w, h);
  lv_obj_set_style_local_bg_color(rect, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
  lv_obj_set_style_local_border_width(rect, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_radius(rect, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
  lv_obj_set_hidden(rect, true);
  return rect;
}

const lv_img_dsc_t* SpaceInvaders::InvaderImage(int row) const {
  const int base = row == 0 ? Scout0 : (row < 3 ? Drone0 : Brute0);
  return &sprites[base + animationFrame].descriptor;
}

uint32_t SpaceInvaders::NextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

void SpaceInvaders::StartGame() {
  score = 0;
  lives = 3;
  wave = 0;
  gameOver = false;
  cannonX = (screenSize - sprites[Cannon].descriptor.header.w) / 2;
  targetX = cannonX;
  lv_obj_set_pos(cannon, cannonX, cannonY);
  lv_obj_set_hidden(cannon, false);
  nextUfoTick = lv_tick_get() + 15000;
  StartWave();
  UpdateScore();
}

void SpaceInvaders::StartWave() {
  formationX = (screenSize - columns * cellWidth) / 2;
  formationY = formationTop + std::min<int>(wave, 4) * 6;
  formationDirection = 1;
  animationFrame = 0;
  aliveCount = rows * columns;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      alive[row][column] = true;
      lv_obj_set_hidden(invaders[row][column], false);
    }
  }
  for (int shield = 0; shield < shieldCount; shield++) {
    for (int block = 0; block < shieldBlocks; block++) {
      shieldHealth[shield][block] = 2;
      lv_obj_set_style_local_bg_color(shields[shield][block], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorShield));
      lv_obj_set_hidden(shields[shield][block], false);
    }
  }
  bulletActive = false;
  lv_obj_set_hidden(bullet, true);
  for (int i = 0; i < maxBombs; i++) {
    bombActive[i] = false;
    lv_obj_set_hidden(bombs[i], true);
  }
  ufoActive = false;
  lv_obj_set_hidden(ufo, true);

  // place the invaders without moving them
  formationX -= 4 * formationDirection;
  animationFrame ^= 1;
  StepFormation();
  lastStepTick = lv_tick_get();
  lastBombTick = lv_tick_get();

  char text[16];
  snprintf(text, sizeof(text), "WAVE %d", wave + 1);
  ShowMessage(text);
  pausedUntilTick = lv_tick_get() + 1200;
}

void SpaceInvaders::UpdateScore() {
  lv_label_set_text_fmt(labelScore, "SCORE %05lu", static_cast<unsigned long>(score));
  lv_label_set_text_fmt(labelLives, "LIVES %d", lives);
  lv_obj_set_pos(labelLives, screenSize - 4 - lv_obj_get_width(labelLives), 1);
}

void SpaceInvaders::ShowMessage(const char* text) {
  lv_label_set_text(labelMessage, text);
  lv_obj_align(labelMessage, nullptr, LV_ALIGN_CENTER, 0, 10);
  lv_obj_set_hidden(labelMessage, false);
}

void SpaceInvaders::Explode(lv_coord_t centerX, lv_coord_t centerY) {
  lv_obj_set_pos(boom, centerX - sprites[Boom].descriptor.header.w / 2, centerY - sprites[Boom].descriptor.header.h / 2);
  lv_obj_set_hidden(boom, false);
  boomUntilTick = lv_tick_get() + 250;
}

void SpaceInvaders::StepFormation() {
  int firstColumn = columns;
  int lastColumn = -1;
  int lastRow = -1;
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      if (alive[row][column]) {
        firstColumn = std::min(firstColumn, column);
        lastColumn = std::max(lastColumn, column);
        lastRow = std::max(lastRow, row);
      }
    }
  }
  if (lastColumn < 0) {
    return;
  }

  const lv_coord_t step = 4 * formationDirection;
  const lv_coord_t left = formationX + firstColumn * cellWidth + 2 + step;
  const lv_coord_t right = formationX + (lastColumn + 1) * cellWidth - 2 + step;
  if (left < 2 || right > screenSize - 2) {
    formationY += 8;
    formationDirection = -formationDirection;
  } else {
    formationX += step;
  }
  animationFrame ^= 1;

  lv_obj_set_pos(formation, formationX, formationY);
  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < columns; column++) {
      if (!alive[row][column]) {
        continue;
      }
      // positions inside the container never change, only the animation frame does
      const lv_img_dsc_t* image = InvaderImage(row);
      lv_img_set_src(invaders[row][column], image);
      lv_obj_set_pos(invaders[row][column], column * cellWidth + (cellWidth - image->header.w) / 2, row * cellHeight + (cellHeight - image->header.h) / 2);
    }
  }

  const lv_coord_t formationBottom = formationY + (lastRow + 1) * cellHeight;
  // invaders chew through any shield they touch
  if (formationBottom >= shieldY) {
    for (int row = 0; row < rows; row++) {
      for (int column = 0; column < columns; column++) {
        if (alive[row][column]) {
          const lv_coord_t x = formationX + column * cellWidth;
          const lv_coord_t y = formationY + row * cellHeight;
          while (HitShield(x, y, cellWidth, cellHeight)) {
          }
        }
      }
    }
  }
  // the invaders reached the cannon: game over
  if (formationBottom >= cannonY) {
    lives = 1;
    PlayerHit();
  }
}

void SpaceInvaders::MoveCannon() {
  if (cannonX < targetX) {
    cannonX = std::min<lv_coord_t>(cannonX + cannonSpeed, targetX);
  } else if (cannonX > targetX) {
    cannonX = std::max<lv_coord_t>(cannonX - cannonSpeed, targetX);
  } else {
    return;
  }
  lv_obj_set_pos(cannon, cannonX, cannonY);
}

void SpaceInvaders::MoveBullet() {
  if (!bulletActive) {
    // auto fire
    bulletActive = true;
    bulletX = cannonX + sprites[Cannon].descriptor.header.w / 2 - bulletWidth / 2;
    bulletY = cannonY - bulletHeight;
    lv_obj_set_hidden(bullet, false);
  } else {
    bulletY -= bulletSpeed;
  }

  if (bulletY < hudHeight) {
    bulletActive = false;
    lv_obj_set_hidden(bullet, true);
    return;
  }
  lv_obj_set_pos(bullet, bulletX, bulletY);

  if (ufoActive && Overlaps(bulletX,
                            bulletY,
                            bulletWidth,
                            bulletHeight,
                            ufoX,
                            ufoY,
                            sprites[Ufo].descriptor.header.w,
                            sprites[Ufo].descriptor.header.h)) {
    score += ufoPoints[NextRandom() % 4];
    ufoActive = false;
    lv_obj_set_hidden(ufo, true);
    Explode(ufoX + sprites[Ufo].descriptor.header.w / 2, ufoY + sprites[Ufo].descriptor.header.h / 2);
    bulletActive = false;
    lv_obj_set_hidden(bullet, true);
    UpdateScore();
    return;
  }

  for (int row = rows - 1; row >= 0; row--) {
    for (int column = 0; column < columns; column++) {
      if (!alive[row][column]) {
        continue;
      }
      const lv_img_dsc_t* image = InvaderImage(row);
      const lv_coord_t x = formationX + column * cellWidth + (cellWidth - image->header.w) / 2;
      const lv_coord_t y = formationY + row * cellHeight + (cellHeight - image->header.h) / 2;
      if (Overlaps(bulletX, bulletY, bulletWidth, bulletHeight, x, y, image->header.w, image->header.h)) {
        alive[row][column] = false;
        aliveCount--;
        lv_obj_set_hidden(invaders[row][column], true);
        Explode(x + image->header.w / 2, y + image->header.h / 2);
        score += rowPoints[row];
        bulletActive = false;
        lv_obj_set_hidden(bullet, true);
        UpdateScore();
        if (aliveCount == 0) {
          wave++;
          StartWave();
        }
        return;
      }
    }
  }

  if (HitShield(bulletX, bulletY, bulletWidth, bulletHeight)) {
    bulletActive = false;
    lv_obj_set_hidden(bullet, true);
  }
}

bool SpaceInvaders::HitShield(lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
  if (y > shieldY + 2 * shieldBlockHeight || y + h < shieldY) {
    return false;
  }
  for (int shield = 0; shield < shieldCount; shield++) {
    for (int block = 0; block < shieldBlocks; block++) {
      if (shieldHealth[shield][block] == 0) {
        continue;
      }
      const lv_coord_t blockX = shieldCenters[shield] - shieldBlockWidth + (block % 2) * shieldBlockWidth;
      const lv_coord_t blockY = shieldY + (block / 2) * shieldBlockHeight;
      if (Overlaps(x, y, w, h, blockX, blockY, shieldBlockWidth, shieldBlockHeight)) {
        if (--shieldHealth[shield][block] == 0) {
          lv_obj_set_hidden(shields[shield][block], true);
        } else {
          lv_obj_set_style_local_bg_color(shields[shield][block], LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorShieldDamaged));
        }
        return true;
      }
    }
  }
  return false;
}

void SpaceInvaders::DropBomb() {
  const uint32_t interval = std::max<int>(400, 1000 - wave * 100);
  if (lv_tick_elaps(lastBombTick) < interval) {
    return;
  }
  lastBombTick = lv_tick_get();

  int slot = -1;
  for (int i = 0; i < maxBombs; i++) {
    if (!bombActive[i]) {
      slot = i;
      break;
    }
  }
  if (slot < 0) {
    return;
  }

  // a random column that still has invaders; its lowest invader drops the bomb
  const int start = NextRandom() % columns;
  for (int offset = 0; offset < columns; offset++) {
    const int column = (start + offset) % columns;
    for (int row = rows - 1; row >= 0; row--) {
      if (alive[row][column]) {
        bombActive[slot] = true;
        bombX[slot] = formationX + column * cellWidth + cellWidth / 2 - bombWidth / 2;
        bombY[slot] = formationY + (row + 1) * cellHeight - 4;
        lv_obj_set_pos(bombs[slot], bombX[slot], bombY[slot]);
        lv_obj_set_hidden(bombs[slot], false);
        return;
      }
    }
  }
}

void SpaceInvaders::MoveBombs() {
  const lv_coord_t cannonWidth = sprites[Cannon].descriptor.header.w;
  const lv_coord_t cannonHeight = sprites[Cannon].descriptor.header.h;
  for (int i = 0; i < maxBombs; i++) {
    if (!bombActive[i]) {
      continue;
    }
    bombY[i] += bombSpeed;
    if (bombY[i] > screenSize || HitShield(bombX[i], bombY[i], bombWidth, bombHeight)) {
      bombActive[i] = false;
      lv_obj_set_hidden(bombs[i], true);
      continue;
    }
    lv_obj_set_pos(bombs[i], bombX[i], bombY[i]);
    if (Overlaps(bombX[i], bombY[i], bombWidth, bombHeight, cannonX + 2, cannonY + 2, cannonWidth - 4, cannonHeight - 2)) {
      PlayerHit();
      return;
    }
  }
}

void SpaceInvaders::MoveUfo() {
  const lv_coord_t width = sprites[Ufo].descriptor.header.w;
  if (!ufoActive) {
    if (static_cast<int32_t>(lv_tick_get() - nextUfoTick) < 0) {
      return;
    }
    ufoActive = true;
    ufoDirection = (NextRandom() % 2 == 0) ? 1 : -1;
    ufoX = ufoDirection > 0 ? -width : screenSize;
    lv_obj_set_hidden(ufo, false);
  }
  ufoX += ufoSpeed * ufoDirection;
  if (ufoX < -width || ufoX > screenSize) {
    ufoActive = false;
    lv_obj_set_hidden(ufo, true);
    nextUfoTick = lv_tick_get() + 12000 + NextRandom() % 10000;
    return;
  }
  lv_obj_set_pos(ufo, ufoX, ufoY);
}

void SpaceInvaders::PlayerHit() {
  Explode(cannonX + sprites[Cannon].descriptor.header.w / 2, cannonY + sprites[Cannon].descriptor.header.h / 2);
  for (int i = 0; i < maxBombs; i++) {
    bombActive[i] = false;
    lv_obj_set_hidden(bombs[i], true);
  }
  if (lives > 0) {
    lives--;
  }
  UpdateScore();
  pausedUntilTick = lv_tick_get() + 1000;
  if (lives == 0) {
    gameOver = true;
    highScore = std::max(highScore, score);
    lv_obj_set_hidden(cannon, true);
    char text[48];
    snprintf(text, sizeof(text), "GAME OVER\nHI %05lu\nTAP TO PLAY", static_cast<unsigned long>(highScore));
    ShowMessage(text);
  }
}

void SpaceInvaders::Refresh() {
  if (!spritesLoaded) {
    return;
  }
  if (!lv_obj_get_hidden(boom) && static_cast<int32_t>(lv_tick_get() - boomUntilTick) >= 0) {
    lv_obj_set_hidden(boom, true);
  }
  if (gameOver) {
    return;
  }
  if (static_cast<int32_t>(lv_tick_get() - pausedUntilTick) < 0) {
    return;
  }
  lv_obj_set_hidden(labelMessage, true);

  MoveCannon();
  MoveBullet();
  if (gameOver || static_cast<int32_t>(lv_tick_get() - pausedUntilTick) < 0) {
    return; // a new wave started
  }

  const uint32_t stepInterval = std::max<int>(40, 50 + aliveCount * 17 - wave * 15);
  if (lv_tick_elaps(lastStepTick) >= stepInterval) {
    lastStepTick = lv_tick_get();
    StepFormation();
    if (gameOver) {
      return;
    }
  }
  DropBomb();
  MoveBombs();
  MoveUfo();
}

bool SpaceInvaders::OnTouchEvent(Pinetime::Applications::TouchEvents /*event*/) {
  // swallow swipes so steering does not leave the game; the side button exits
  return true;
}

bool SpaceInvaders::OnTouchEvent(uint16_t x, uint16_t /*y*/) {
  if (!spritesLoaded) {
    return true;
  }
  if (gameOver) {
    if (static_cast<int32_t>(lv_tick_get() - pausedUntilTick) >= 0) {
      StartGame();
    }
    return true;
  }
  const lv_coord_t width = sprites[Cannon].descriptor.header.w;
  targetX = std::clamp<lv_coord_t>(static_cast<lv_coord_t>(x) - width / 2, 0, screenSize - width);
  return true;
}
