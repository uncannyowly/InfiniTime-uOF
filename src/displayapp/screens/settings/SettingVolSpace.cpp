#include "displayapp/screens/settings/SettingVolSpace.h"

#include <cstdio>
#include "components/firmware/FirmwareImage.h"
#include "components/fs/FS.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  constexpr uint32_t colorInternal = 0xff9900;
  constexpr uint32_t colorExternal = 0x00b0ff;
  constexpr uint32_t colorFree = 0x383838;
  constexpr uint32_t colorCaption = 0x808080;

  constexpr lv_coord_t pieRadius = 40;
  constexpr lv_coord_t pieTop = 34;
  constexpr lv_coord_t textTop = pieTop + 2 * pieRadius + 6;
  constexpr lv_coord_t lineHeight = 18;

  // "393.7K" below 1 MiB, "3.28M" above; integer maths only
  void FormatSize(char* buffer, size_t size, uint32_t bytes) {
    if (bytes >= 1024 * 1024) {
      const uint32_t hundredths = bytes / 1024 * 100 / 1024;
      snprintf(buffer, size, "%lu.%02luM", static_cast<unsigned long>(hundredths / 100), static_cast<unsigned long>(hundredths % 100));
    } else {
      const uint32_t tenths = bytes * 10 / 1024;
      snprintf(buffer, size, "%lu.%luK", static_cast<unsigned long>(tenths / 10), static_cast<unsigned long>(tenths % 10));
    }
  }

  lv_obj_t* CreateLabel(const lv_font_t* font, uint32_t color, lv_coord_t centerX, lv_coord_t y, const char* text) {
    lv_obj_t* label = lv_label_create(lv_scr_act(), nullptr);
    lv_obj_set_style_local_text_font(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, font);
    lv_obj_set_style_local_text_color(label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, lv_color_hex(color));
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, centerX - lv_obj_get_width(label) / 2, y);
    return label;
  }
}

SettingVolSpace::SettingVolSpace(Controllers::FS& fs) {
  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_recolor(title, true);
  lv_label_set_text_fmt(title, "#ff9900 %s# Vol Space", Symbols::hdd);
  lv_obj_align(title, nullptr, LV_ALIGN_IN_TOP_MID, 0, 4);

  CreateVolume(60,
               "Internal",
               "firmware",
               colorInternal,
               Controllers::FirmwareImage::UsedBytes(),
               Controllers::FirmwareImage::SlotBytes());

  const lfs_ssize_t usedBlocks = fs.GetFSSize();
  const uint32_t fsUsed = usedBlocks < 0 ? 0 : static_cast<uint32_t>(usedBlocks) * Controllers::FS::getBlockSize();
  CreateVolume(180, "External", "file system", colorExternal, fsUsed, Controllers::FS::getSize());
}

SettingVolSpace::~SettingVolSpace() {
  lv_obj_clean(lv_scr_act());
}

void SettingVolSpace::CreateVolume(lv_coord_t centerX, const char* name, const char* content, uint32_t color, uint32_t used, uint32_t total) {
  const uint32_t permille = total == 0 ? 0 : static_cast<uint32_t>(static_cast<uint64_t>(used) * 1000 / total);

  // A ring as thick as its radius draws as a filled pie
  lv_obj_t* pie = lv_arc_create(lv_scr_act(), nullptr);
  lv_obj_set_click(pie, false);
  lv_obj_set_size(pie, 2 * pieRadius, 2 * pieRadius);
  lv_obj_set_pos(pie, centerX - pieRadius, pieTop);
  lv_obj_set_style_local_bg_opa(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_border_width(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_pad_all(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, 0);
  lv_obj_set_style_local_line_width(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, pieRadius);
  lv_obj_set_style_local_line_color(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, lv_color_hex(colorFree));
  lv_obj_set_style_local_line_rounded(pie, LV_ARC_PART_BG, LV_STATE_DEFAULT, false);
  lv_obj_set_style_local_line_width(pie, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, pieRadius);
  lv_obj_set_style_local_line_color(pie, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, lv_color_hex(color));
  lv_obj_set_style_local_line_rounded(pie, LV_ARC_PART_INDIC, LV_STATE_DEFAULT, false);
  lv_arc_set_bg_angles(pie, 0, 360);
  lv_arc_set_rotation(pie, 270); // start at 12 o'clock
  lv_arc_set_range(pie, 0, 1000);
  lv_arc_set_value(pie, static_cast<int16_t>(permille));

  char usedText[16];
  char freeText[16];
  char totalText[16];
  FormatSize(usedText, sizeof(usedText), used);
  FormatSize(freeText, sizeof(freeText), total > used ? total - used : 0);
  FormatSize(totalText, sizeof(totalText), total);

  char line[24];
  lv_coord_t y = textTop;
  CreateLabel(&jetbrains_mono_bold_20, color, centerX, y, name);
  y += 22;
  CreateLabel(&jetbrains_mono_bold_14, colorCaption, centerX, y, content);
  y += lineHeight;
  snprintf(line, sizeof(line), "Used %s", usedText);
  CreateLabel(&jetbrains_mono_bold_14, 0xffffff, centerX, y, line);
  y += lineHeight;
  snprintf(line, sizeof(line), "Free %s", freeText);
  CreateLabel(&jetbrains_mono_bold_14, 0xffffff, centerX, y, line);
  y += lineHeight;
  snprintf(line, sizeof(line), "Size %s", totalText);
  CreateLabel(&jetbrains_mono_bold_14, colorCaption, centerX, y, line);
  y += lineHeight;
  snprintf(line, sizeof(line), "%lu.%lu%% used", static_cast<unsigned long>(permille / 10), static_cast<unsigned long>(permille % 10));
  CreateLabel(&jetbrains_mono_bold_14, color, centerX, y, line);
}
