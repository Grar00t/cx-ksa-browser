#pragma once

#include <cstdint>
#include <vector>

namespace cx::ui {

inline constexpr char kAtharStartupSetting[] =
    "ui.athar_startup_sound";

class AtharSound {
public:
  static AtharSound& Instance();

  bool Play();
  void Stop();

  static std::vector<std::uint8_t> BuildWave();

private:
  AtharSound();

  std::vector<std::uint8_t> wave_;
};

}  // namespace cx::ui
