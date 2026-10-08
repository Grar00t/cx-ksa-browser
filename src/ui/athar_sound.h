#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cx::ui {

inline constexpr char kAtharStartupSetting[] =
    "ui.athar_startup_sound";

class AtharSound {
public:
  using WaveBuilder = std::function<std::vector<std::uint8_t>()>;
  using WavePlayer = std::function<bool(
      const std::vector<std::uint8_t>&)>;
  using PlaybackStopper = std::function<void()>;

  static AtharSound& Instance();

  // Play queues a request; success does not imply audible output.
  bool Play();
  void Stop();
  ~AtharSound();

  // Injectable backends make lifecycle tests independent of speakers.
  AtharSound(WaveBuilder build, WavePlayer play,
             PlaybackStopper stop);

  AtharSound(const AtharSound&) = delete;
  AtharSound& operator=(const AtharSound&) = delete;

  static std::vector<std::uint8_t> BuildWave();

private:
  AtharSound();
  void WorkerMain();

  WaveBuilder build_;
  WavePlayer play_;
  PlaybackStopper stop_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::thread worker_;
  std::vector<std::uint8_t> wave_;
  std::uint64_t generation_ = 0;
  bool pending_ = false;
  bool closing_ = false;
};

}  // namespace cx::ui
