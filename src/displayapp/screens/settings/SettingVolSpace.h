#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include "displayapp/screens/Screen.h"

namespace Pinetime {
  namespace Controllers {
    class FS;
  }

  namespace Applications {
    namespace Screens {

      // Used/free space of the internal flash (firmware image) and the external SPI flash (file system)
      class SettingVolSpace : public Screen {
      public:
        explicit SettingVolSpace(Controllers::FS& fs);
        ~SettingVolSpace() override;

      private:
        void CreateVolume(lv_coord_t centerX, const char* name, const char* content, uint32_t color, uint32_t used, uint32_t total);
      };
    }
  }
}
