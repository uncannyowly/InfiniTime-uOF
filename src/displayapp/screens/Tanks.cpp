#include "displayapp/screens/Tanks.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr int screenSize = Tanks::screenSize;
  constexpr int stride = screenSize / 8;

  constexpr uint32_t colorSky = 0x000000;
  constexpr uint32_t colorTerrain = 0x3a7d3a;
  constexpr uint32_t colorPlayer = 0x33ccff;
  constexpr uint32_t colorAi = 0xff5544;
  constexpr uint32_t colorShell = 0xffee66;
  constexpr uint32_t colorBoom = 0xff9922;

  constexpr int8_t tankWidth = 9;
  constexpr int8_t tankHeight = 5;
  constexpr float gravity = 0.16f;

  // index into Shell enum order
  const Tanks::ShellSpec shellSpecs[] = {
    // name        blast dmg proj vel  split roller
    {"BASIC BOMB",    12, 34, 1, 42, false, false},
    {"HEAVY SHELL",   20, 55, 1, 40, false, false},
    {"NUKE",          34, 80, 1, 38, false, false},
    {"TRI-SHOT",      10, 26, 3, 44, false, false},
    {"CLUSTER",       11, 24, 1, 44, true,  false},
    {"ROLLER",        13, 38, 1, 42, false, true},
    {"DIGGER",         8, 30, 1, 46, false, false},
    {"SNIPER",         7, 46, 1, 60, false, false},
    {"NAPALM",        18, 30, 1, 40, false, true},
    {"MIRV",          10, 22, 1, 42, true,  false},
  };

  float DegToRad(float deg) {
    return deg * 3.14159265f / 180.0f;
  }
}

Tanks::Tanks() {
  randomState = 0x1234abcdu ^ lv_tick_get();
  lv_obj_set_style_local_bg_color(lv_scr_act(), LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);

  // Terrain image palette: index 0 = transparent sky, index 1 = terrain
  auto writePalette = [&](int slot, uint32_t argb, uint8_t alpha) {
    terrainBuf[slot * 4 + 0] = argb & 0xff;
    terrainBuf[slot * 4 + 1] = (argb >> 8) & 0xff;
    terrainBuf[slot * 4 + 2] = (argb >> 16) & 0xff;
    terrainBuf[slot * 4 + 3] = alpha;
  };
  writePalette(0, colorSky, 0x00);
  writePalette(1, colorTerrain, 0xff);

  terrainDsc.header.always_zero = 0;
  terrainDsc.header.w = screenSize;
  terrainDsc.header.h = screenSize;
  terrainDsc.header.cf = LV_IMG_CF_INDEXED_1BIT;
  terrainDsc.data = terrainBuf;
  terrainDsc.data_size = sizeof(terrainBuf);

  terrainImg = lv_img_create(lv_scr_act(), nullptr);
  lv_obj_set_click(terrainImg, false);
  lv_img_set_src(terrainImg, &terrainDsc);
  lv_obj_set_pos(terrainImg, 0, 0);

  for (auto& tank : tanks) {
    tank.body = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_click(tank.body, false);
    lv_obj_set_size(tank.body, tankWidth, tankHeight);
    lv_obj_set_style_local_border_width(tank.body, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 0);
    lv_obj_set_style_local_radius(tank.body, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 1);
    tank.barrel = lv_line_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_line_width(tank.barrel, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, 2);
  }
  lv_obj_set_style_local_bg_color(tanks[0].body, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPlayer));
  lv_obj_set_style_local_line_color(tanks[0].barrel, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPlayer));
  lv_obj_set_style_local_bg_color(tanks[1].body, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorAi));
  lv_obj_set_style_local_line_color(tanks[1].barrel, LV_LINE_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorAi));

  for (auto& p : projectiles) {
    p.active = false;
    p.dot = lv_obj_create(lv_scr_act(), nullptr);
    lv_obj_set_click(p.dot, false);
    lv_obj_set_size(p.dot, 3, 3);
    lv_obj_set_style_local_bg_color(p.dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorShell));
    lv_obj_set_style_local_radius(p.dot, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);
    lv_obj_set_hidden(p.dot, true);
  }

  windLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(windLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_style_local_text_color(windLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xaaaaaa));
  lv_label_set_text_static(windLabel, "");
  lv_obj_align(windLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 2);

  healthPlayer = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(healthPlayer, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_style_local_text_color(healthPlayer, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorPlayer));
  lv_label_set_text_static(healthPlayer, "");
  lv_obj_align(healthPlayer, nullptr, LV_ALIGN_IN_TOP_LEFT, 4, 2);

  healthAi = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(healthAi, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_obj_set_style_local_text_color(healthAi, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorAi));
  lv_label_set_text_static(healthAi, "");

  powerBar = lv_bar_create(lv_scr_act(), nullptr);
  lv_obj_set_size(powerBar, screenSize - 40, 8);
  lv_obj_align(powerBar, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -4);
  lv_bar_set_range(powerBar, 0, 100);
  lv_obj_set_style_local_bg_color(powerBar, LV_BAR_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(colorShell));

  message = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_align(message, LV_LABEL_ALIGN_CENTER);
  lv_obj_set_style_local_text_color(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xffffff));
  lv_obj_set_style_local_bg_color(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_BLACK);
  lv_obj_set_style_local_bg_opa(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_70);

  StartMatch();
  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
}

Tanks::~Tanks() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

uint32_t Tanks::NextRandom() {
  randomState ^= randomState << 13;
  randomState ^= randomState >> 17;
  randomState ^= randomState << 5;
  return randomState;
}

void Tanks::SetSolid(int16_t x, int16_t y, bool solid) {
  if (x < 0 || x >= screenSize || y < 0 || y >= screenSize) {
    return;
  }
  uint8_t& byte = terrainBuf[8 + y * stride + x / 8];
  const uint8_t bit = 0x80 >> (x % 8);
  if (solid) {
    byte |= bit;
  } else {
    byte &= ~bit;
  }
}

bool Tanks::IsSolid(int16_t x, int16_t y) const {
  if (x < 0 || x >= screenSize || y < 0 || y >= screenSize) {
    return false;
  }
  return (terrainBuf[8 + y * stride + x / 8] & (0x80 >> (x % 8))) != 0;
}

int16_t Tanks::SurfaceY(int16_t x) const {
  for (int16_t y = 0; y < screenSize; y++) {
    if (IsSolid(x, y)) {
      return y;
    }
  }
  return screenSize;
}

void Tanks::GenerateTerrain() {
  std::fill(terrainBuf + 8, terrainBuf + sizeof(terrainBuf), 0);
  const float p1 = (NextRandom() % 628) / 100.0f;
  const float p2 = (NextRandom() % 628) / 100.0f;
  const float p3 = (NextRandom() % 628) / 100.0f;
  const float a1 = 18 + NextRandom() % 22;
  const float a2 = 8 + NextRandom() % 14;
  const int base = 150 + NextRandom() % 30;
  for (int x = 0; x < screenSize; x++) {
    int h = base + static_cast<int>(a1 * std::sin(x * 0.021f + p1) + a2 * std::sin(x * 0.053f + p2) + 6 * std::sin(x * 0.11f + p3));
    h = std::clamp(h, 70, 232);
    for (int y = h; y < screenSize; y++) {
      SetSolid(x, y, true);
    }
  }
  wind = static_cast<int8_t>((NextRandom() % 11) - 5);
}

void Tanks::PlaceTanks() {
  tanks[0].x = 20 + NextRandom() % 40;
  tanks[1].x = screenSize - 30 - NextRandom() % 40;
  for (auto& tank : tanks) {
    tank.health = 100;
    tank.y = SurfaceY(tank.x) - tankHeight;
  }
}

void Tanks::StartMatch() {
  GenerateTerrain();
  PlaceTanks();

  // Choose 3 shells for the match: Basic plus 2 random distinct others
  availableShells[0] = Shell::Basic;
  bool used[static_cast<int>(Shell::Count)] = {};
  used[0] = true;
  for (int i = 1; i < 3; i++) {
    uint8_t pick;
    do {
      pick = 1 + NextRandom() % (static_cast<int>(Shell::Count) - 1);
    } while (used[pick]);
    used[pick] = true;
    availableShells[i] = static_cast<Shell>(pick);
  }
  selectedShell = 0;

  for (auto& p : projectiles) {
    p.active = false;
    lv_obj_set_hidden(p.dot, true);
  }
  DrawTerrain();
  StartIntro();
}

void Tanks::StartIntro() {
  phase = Phase::Intro;
  introZoom = 512; // 2x
  phaseTick = lv_tick_get();
  lv_obj_set_hidden(tanks[0].body, true);
  lv_obj_set_hidden(tanks[1].body, true);
  lv_obj_set_hidden(tanks[0].barrel, true);
  lv_obj_set_hidden(tanks[1].barrel, true);
  lv_obj_set_hidden(powerBar, true);
  lv_label_set_text_static(message, "");
  lv_obj_set_hidden(message, true);
}

void Tanks::BeginPlayerTurn() {
  phase = Phase::Aim;
  power = 0;
  charging = false;
  lv_bar_set_value(powerBar, 0, LV_ANIM_OFF);
  lv_obj_set_hidden(powerBar, false);
  UpdateAimLine();
  UpdateHud();
}

void Tanks::DrawTerrain() {
  lv_obj_invalidate(terrainImg);
}

void Tanks::UpdateAimLine() {
  for (int t = 0; t < 2; t++) {
    Tank& tank = tanks[t];
    const int16_t cx = tank.x + tankWidth / 2;
    const int16_t cy = tank.y;
    // player barrel uses aimAngle to the right; AI mirrors to the left
    const float ang = (t == 0) ? aimAngle : (180 - aimAngle);
    const int16_t ex = cx + static_cast<int16_t>(14 * std::cos(DegToRad(ang)));
    const int16_t ey = cy - static_cast<int16_t>(14 * std::sin(DegToRad(ang)));
    tank.barrelPoints[0] = {cx, cy};
    tank.barrelPoints[1] = {ex, ey};
    lv_line_set_points(tank.barrel, tank.barrelPoints, 2);
  }
}

void Tanks::UpdateHud() {
  lv_label_set_text_fmt(healthPlayer, "P%d", tanks[0].health < 0 ? 0 : tanks[0].health);
  lv_label_set_text_fmt(healthAi, "E%d", tanks[1].health < 0 ? 0 : tanks[1].health);
  lv_obj_align(healthAi, nullptr, LV_ALIGN_IN_TOP_RIGHT, -4, 2);
  const ShellSpec& spec = shellSpecs[static_cast<int>(availableShells[selectedShell])];
  lv_label_set_text_fmt(windLabel, "W%+d", wind);
  lv_obj_align(windLabel, nullptr, LV_ALIGN_IN_TOP_MID, 0, 2);
  lv_label_set_text_static(message, spec.name); // reuse message label as a shell tag while aiming
  lv_obj_set_style_local_text_color(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0xffee66));
  lv_obj_set_style_local_bg_opa(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_align(message, nullptr, LV_ALIGN_IN_BOTTOM_LEFT, 4, -16);
  lv_obj_set_hidden(message, false);
}

void Tanks::FireFrom(uint8_t owner, int16_t angleDeg, uint8_t pwr, Shell shell) {
  const ShellSpec& spec = shellSpecs[static_cast<int>(shell)];
  flyingShell = shell;
  lastShooter = owner;
  Tank& tank = tanks[owner];
  const float ang = (owner == 0) ? angleDeg : (180 - angleDeg);
  const float speed = pwr / 100.0f * spec.velocityScale / 10.0f + 1.2f;
  const int count = spec.projectiles;
  const int16_t bx = tank.x + tankWidth / 2 + static_cast<int16_t>(12 * std::cos(DegToRad(ang)));
  const int16_t by = tank.y - static_cast<int16_t>(12 * std::sin(DegToRad(ang)));
  int launched = 0;
  for (int i = 0; i < maxProjectiles && launched < count; i++) {
    if (projectiles[i].active) {
      continue;
    }
    const float spread = (count > 1) ? (i - (count - 1) / 2.0f) * 6.0f : 0.0f;
    const float a = DegToRad(ang + spread);
    projectiles[i] = {true, static_cast<float>(bx), static_cast<float>(by), speed * std::cos(a), -speed * std::sin(a), owner, false, projectiles[i].dot};
    lv_obj_set_hidden(projectiles[i].dot, false);
    launched++;
  }
  phase = Phase::Flying;
  phaseTick = lv_tick_get();
  lv_obj_set_hidden(powerBar, true);
}

void Tanks::StepProjectiles() {
  const ShellSpec& spec = shellSpecs[static_cast<int>(flyingShell)];
  bool anyActive = false;
  for (auto& p : projectiles) {
    if (!p.active) {
      continue;
    }
    p.vx += wind * 0.004f;
    p.vy += gravity;
    p.x += p.vx;
    p.y += p.vy;

    // split shells drop bomblets once, on the way down
    if (spec.splitAtApex && !p.split && p.vy > 0 && p.y < 150) {
      p.split = true;
      for (auto& q : projectiles) {
        if (!q.active) {
          q = {true, p.x, p.y, p.vx - 0.6f, p.vy - 0.4f, p.owner, true, q.dot};
          lv_obj_set_hidden(q.dot, false);
          break;
        }
      }
    }

    const int16_t ix = static_cast<int16_t>(p.x);
    const int16_t iy = static_cast<int16_t>(p.y);
    bool hit = false;
    if (iy >= screenSize - 1) {
      hit = true;
    } else if (ix < -30 || ix > screenSize + 30) {
      p.active = false;
      lv_obj_set_hidden(p.dot, true);
      continue;
    } else if (iy >= 0 && IsSolid(ix, iy)) {
      hit = true;
    } else {
      for (int t = 0; t < 2; t++) {
        if (ix >= tanks[t].x && ix < tanks[t].x + tankWidth && iy >= tanks[t].y && iy < tanks[t].y + tankHeight) {
          hit = true;
        }
      }
    }

    if (hit) {
      p.active = false;
      lv_obj_set_hidden(p.dot, true);
      Explode(ix, std::min<int16_t>(iy, screenSize - 1), spec, p.owner);
    } else {
      anyActive = true;
    }
  }

  if (!anyActive && (phase == Phase::Flying)) {
    SettleTanks();
    phase = Phase::Resolve;
    phaseTick = lv_tick_get();
  }
}

void Tanks::Explode(int16_t cx, int16_t cy, const ShellSpec& spec, uint8_t owner) {
  const int r = spec.blastRadius;
  for (int dy = -r; dy <= r; dy++) {
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy <= r * r) {
        SetSolid(cx + dx, cy + dy, false);
      }
    }
  }
  DrawTerrain();

  for (int t = 0; t < 2; t++) {
    const int16_t tx = tanks[t].x + tankWidth / 2;
    const int16_t ty = tanks[t].y + tankHeight / 2;
    const int dist = static_cast<int>(std::sqrt(static_cast<float>((tx - cx) * (tx - cx) + (ty - cy) * (ty - cy))));
    if (dist <= r + 4) {
      const int dmg = spec.damage * (r + 4 - dist) / (r + 4);
      tanks[t].health -= dmg;
    }
  }

  // roller shells keep chewing a short trench downhill
  if (spec.roller) {
    for (int i = 1; i <= 10; i++) {
      const int16_t rx = cx + (owner == 0 ? i : -i);
      const int16_t ry = SurfaceY(rx);
      for (int dy = -4; dy <= 4; dy++) {
        for (int dx = -4; dx <= 4; dx++) {
          if (dx * dx + dy * dy <= 16) {
            SetSolid(rx + dx, ry + dy, false);
          }
        }
      }
    }
    DrawTerrain();
  }
}

void Tanks::SettleTanks() {
  for (auto& tank : tanks) {
    int16_t surface = SurfaceY(tank.x + tankWidth / 2);
    tank.y = surface - tankHeight;
    if (tank.y < 0) {
      tank.y = 0;
    }
  }
  UpdateAimLine();
}

void Tanks::RenderProjectiles() {
  for (auto& p : projectiles) {
    if (p.active) {
      lv_obj_set_pos(p.dot, static_cast<int16_t>(p.x) - 1, static_cast<int16_t>(p.y) - 1);
    }
  }
}

void Tanks::AiTakeTurn() {
  // Rough ballistic solution toward the player, with a bit of noise so it walks shots in.
  const float dx = std::abs(static_cast<float>(tanks[0].x - tanks[1].x));
  const int angle = 42 + NextRandom() % 16;                        // 42-58 deg
  const float sinTwo = std::max(0.3f, std::sin(2 * DegToRad(angle)));
  const float speed = std::sqrt(std::max(1.0f, dx * gravity / sinTwo));
  // invert FireFrom: speed = pwr/100 * velScale/10 + 1.2
  float pwr = (speed - 1.2f) * 1000.0f / shellSpecs[static_cast<int>(Shell::Basic)].velocityScale;
  pwr += static_cast<int>(NextRandom() % 17) - 8;                  // +/-8 noise
  pwr = std::clamp(pwr, 25.0f, 100.0f);
  FireFrom(1, angle, static_cast<uint8_t>(pwr), Shell::Basic);
}

void Tanks::EndGame(bool playerWon) {
  phase = Phase::GameOver;
  lv_obj_set_hidden(powerBar, true);
  lv_label_set_text(message, playerWon ? "YOU WIN\ntap to replay" : "DESTROYED\ntap to replay");
  lv_obj_set_style_local_text_color(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(playerWon ? 0x33ff88 : colorAi));
  lv_obj_set_style_local_bg_opa(message, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_70);
  lv_obj_align(message, nullptr, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_hidden(message, false);
}

void Tanks::Refresh() {
  RenderProjectiles();

  switch (phase) {
    case Phase::Intro: {
      const uint32_t elapsed = lv_tick_get() - phaseTick;
      // zoom the terrain from 2x down to 1x over ~700 ms
      uint16_t zoom = 512 - static_cast<uint16_t>(std::min<uint32_t>(elapsed, 700) * 256 / 700);
      lv_img_set_zoom(terrainImg, zoom);
      lv_img_set_pivot(terrainImg, screenSize / 2, screenSize / 2);
      if (elapsed >= 700) {
        lv_img_set_zoom(terrainImg, 256);
        for (int t = 0; t < 2; t++) {
          lv_obj_set_hidden(tanks[t].body, false);
          lv_obj_set_hidden(tanks[t].barrel, false);
        }
        BeginPlayerTurn();
      }
      break;
    }
    case Phase::Charging:
      if (charging) {
        power = std::min<int>(power + 2, 100);
        lv_bar_set_value(powerBar, power, LV_ANIM_OFF);
      }
      break;
    case Phase::Flying:
      StepProjectiles();
      break;
    case Phase::Resolve:
      UpdateHud();
      if (tanks[0].health <= 0 || tanks[1].health <= 0) {
        EndGame(tanks[1].health <= 0);
      } else if (lv_tick_get() - phaseTick > 500) {
        if (lastShooter == 0) {
          phase = Phase::AiAim;
          phaseTick = lv_tick_get();
        } else {
          BeginPlayerTurn();
        }
      }
      break;
    case Phase::AiAim:
      if (lv_tick_get() - phaseTick > 600) {
        AiTakeTurn();
      }
      break;
    case Phase::Aim:
    case Phase::GameOver:
      break;
  }

  // update tank body positions every frame
  for (int t = 0; t < 2; t++) {
    lv_obj_set_pos(tanks[t].body, tanks[t].x, tanks[t].y);
  }
}

// ---- input ----

bool Tanks::OnTouchEvent(uint16_t x, uint16_t y) {
  if (phase == Phase::Aim || phase == Phase::Charging) {
    // angle from the player tank toward the touch point
    const int16_t cx = tanks[0].x + tankWidth / 2;
    const int16_t cy = tanks[0].y;
    float a = std::atan2(static_cast<float>(cy - static_cast<int>(y)), static_cast<float>(static_cast<int>(x) - cx)) * 180.0f / 3.14159265f;
    aimAngle = static_cast<int16_t>(std::clamp(a, 5.0f, 175.0f));
    UpdateAimLine();
  }
  return true;
}

bool Tanks::OnTouchEvent(Pinetime::Applications::TouchEvents event) {
  if (event == TouchEvents::SwipeDown) {
    running = false; // quit to launcher
    return true;
  }
  if (event == TouchEvents::DoubleTap) {
    if (shellMenu == nullptr && (phase == Phase::Aim || phase == Phase::Charging)) {
      OpenShellMenu();
    } else {
      CloseShellMenu();
    }
    return true;
  }
  if (event == TouchEvents::Tap) {
    if (phase == Phase::GameOver) {
      StartMatch();
      return true;
    }
    if (shellMenu != nullptr) {
      return true; // taps handled by menu buttons
    }
  }
  return true;
}

void Tanks::OnButtonDown() {
  if (phase == Phase::Aim) {
    phase = Phase::Charging;
    charging = true;
    power = 0;
  }
}

void Tanks::OnButtonUp() {
  if (phase == Phase::Charging) {
    charging = false;
    FireFrom(0, aimAngle, std::max<uint8_t>(power, 8), availableShells[selectedShell]);
  }
}

bool Tanks::OnButtonPushed() {
  return true; // consume so the app is never exited by the button
}

// ---- shell selection menu ----

namespace {
  Tanks* menuOwner = nullptr;
}

void Tanks::OpenShellMenu() {
  menuOwner = this;
  shellMenu = lv_obj_create(lv_scr_act(), nullptr);
  lv_obj_set_size(shellMenu, 180, 150);
  lv_obj_align(shellMenu, nullptr, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_local_bg_color(shellMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(0x181818));
  lv_obj_set_style_local_border_color(shellMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(colorShell));
  lv_obj_set_style_local_border_width(shellMenu, LV_OBJ_PART_MAIN, LV_STATE_DEFAULT, 2);

  lv_obj_t* title = lv_label_create(shellMenu, nullptr);
  lv_obj_set_style_local_text_font(title, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
  lv_label_set_text_static(title, "SELECT SHELL");
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 6);

  for (int i = 0; i < 3; i++) {
    lv_obj_t* btn = lv_btn_create(shellMenu, nullptr);
    lv_obj_set_size(btn, 160, 34);
    lv_obj_align(btn, nullptr, LV_ALIGN_IN_TOP_MID, 0, 28 + i * 38);
    lv_obj_set_style_local_bg_color(btn, LV_BTN_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(i == selectedShell ? 0x445544 : 0x2a2a2a));
    btn->user_data = reinterpret_cast<void*>(static_cast<intptr_t>(i));
    lv_obj_set_event_cb(btn, [](lv_obj_t* obj, lv_event_t e) {
      if (e == LV_EVENT_CLICKED && menuOwner != nullptr) {
        menuOwner->selectedShell = static_cast<uint8_t>(reinterpret_cast<intptr_t>(obj->user_data));
        menuOwner->CloseShellMenu();
        menuOwner->UpdateHud();
      }
    });
    lv_obj_t* label = lv_label_create(btn, nullptr);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_14);
    lv_label_set_text_static(label, shellSpecs[static_cast<int>(availableShells[i])].name);
  }
}

void Tanks::CloseShellMenu() {
  if (shellMenu != nullptr) {
    lv_obj_del(shellMenu);
    shellMenu = nullptr;
  }
}
