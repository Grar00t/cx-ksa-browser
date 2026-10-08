#include "ui/athar_sound.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace cx::ui {
namespace {

constexpr std::uint32_t kSampleRate = 48000;
constexpr std::uint16_t kChannels = 2;
constexpr std::uint16_t kBitsPerSample = 16;
constexpr std::uint32_t kDurationSeconds = 8;
constexpr double kPi = 3.14159265358979323846;

void AppendU16(
    std::vector<std::uint8_t>& bytes,
    std::uint16_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value));
  bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}

void AppendU32(
    std::vector<std::uint8_t>& bytes,
    std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(
        static_cast<std::uint8_t>(value >> shift));
  }
}

void AppendTag(
    std::vector<std::uint8_t>& bytes,
    const char* tag) {
  for (int i = 0; i < 4; ++i) {
    bytes.push_back(
        static_cast<std::uint8_t>(tag[i]));
  }
}

double SmoothStep(double edge0, double edge1, double value) {
  const double unit = std::clamp(
      (value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return unit * unit * (3.0 - 2.0 * unit);
}

double Pluck(double time, double onset, double frequency) {
  const double age = time - onset;
  if (age < 0.0 || age > 2.2) {
    return 0.0;
  }
  const double attack = 1.0 - std::exp(-45.0 * age);
  const double decay = std::exp(-2.35 * age);
  const double phase = 2.0 * kPi * frequency * age;
  return attack * decay * (
      std::sin(phase) +
      0.34 * std::sin(2.0 * phase + 0.18) +
      0.12 * std::sin(3.0 * phase + 0.31));
}

double Shimmer(double time) {
  const double age = time - 5.0;
  if (age < 0.0) {
    return 0.0;
  }
  const double envelope =
      (1.0 - std::exp(-12.0 * age)) *
      std::exp(-0.72 * age);
  return envelope * (
      0.50 * std::sin(2.0 * kPi * 987.77 * age) +
      0.31 * std::sin(2.0 * kPi * 1318.51 * age) +
      0.19 * std::sin(2.0 * kPi * 1567.98 * age));
}

}  // namespace

AtharSound& AtharSound::Instance() {
  static AtharSound sound;
  return sound;
}

AtharSound::AtharSound()
    : wave_(BuildWave()) {}

bool AtharSound::Play() {
  if (wave_.empty()) {
    return false;
  }
  return PlaySoundW(
      reinterpret_cast<LPCWSTR>(wave_.data()),
      nullptr,
      SND_ASYNC | SND_MEMORY | SND_NODEFAULT) != FALSE;
}

void AtharSound::Stop() {
  PlaySoundW(nullptr, nullptr, 0);
}

std::vector<std::uint8_t> AtharSound::BuildWave() {
  constexpr std::uint32_t frame_count =
      kSampleRate * kDurationSeconds;
  constexpr std::uint16_t block_align =
      kChannels * (kBitsPerSample / 8);
  constexpr std::uint32_t data_bytes =
      frame_count * block_align;
  constexpr std::uint32_t byte_rate =
      kSampleRate * block_align;

  std::vector<std::uint8_t> wave;
  wave.reserve(44u + data_bytes);
  AppendTag(wave, "RIFF");
  AppendU32(wave, 36u + data_bytes);
  AppendTag(wave, "WAVE");
  AppendTag(wave, "fmt ");
  AppendU32(wave, 16);
  AppendU16(wave, 1);
  AppendU16(wave, kChannels);
  AppendU32(wave, kSampleRate);
  AppendU32(wave, byte_rate);
  AppendU16(wave, block_align);
  AppendU16(wave, kBitsPerSample);
  AppendTag(wave, "data");
  AppendU32(wave, data_bytes);

  std::uint32_t noise_state = 0x43584154u;
  double wind = 0.0;
  double sweep_phase = 0.0;

  for (std::uint32_t frame = 0;
       frame < frame_count; ++frame) {
    const double time =
        static_cast<double>(frame) / kSampleRate;
    noise_state ^= noise_state << 13;
    noise_state ^= noise_state >> 17;
    noise_state ^= noise_state << 5;
    const double raw_noise =
        (static_cast<double>(noise_state) /
             std::numeric_limits<std::uint32_t>::max()) *
            2.0 -
        1.0;
    wind = 0.985 * wind + 0.015 * raw_noise;

    const double fade_in = SmoothStep(0.0, 0.45, time);
    const double fade_out =
        1.0 - SmoothStep(7.35, 8.0, time);
    const double drone =
        0.105 * std::sin(2.0 * kPi * 55.0 * time) +
        0.065 * std::sin(2.0 * kPi * 82.41 * time + 0.4);
    const double pad_mix =
        SmoothStep(1.4, 3.0, time) *
        (0.042 * std::sin(2.0 * kPi * 164.81 * time) +
         0.032 * std::sin(2.0 * kPi * 220.0 * time + 0.8));
    const double oud =
        0.18 * Pluck(time, 0.95, 293.66) +
        0.17 * Pluck(time, 1.65, 311.13) +
        0.16 * Pluck(time, 2.38, 369.99);

    double sweep = 0.0;
    if (time >= 3.0 && time <= 5.05) {
      const double position =
          std::clamp((time - 3.0) / 2.0, 0.0, 1.0);
      const double frequency =
          180.0 + 1420.0 * position * position;
      sweep_phase +=
          2.0 * kPi * frequency / kSampleRate;
      sweep = 0.075 * std::sin(kPi * position) *
          std::sin(sweep_phase);
    }

    const double logo =
        0.105 * Pluck(time, 6.18, 293.66) +
        0.095 * Pluck(time, 6.72, 440.0) +
        0.090 * Pluck(time, 7.18, 587.33);
    const double base =
        fade_in * fade_out *
        (drone + pad_mix + oud + sweep +
         0.105 * Shimmer(time) + logo);
    const double air =
        fade_in * fade_out * 0.052 * wind;
    const double left = std::tanh(base + air);
    const double right = std::tanh(
        base * 0.985 + air * 0.72 +
        0.008 * std::sin(2.0 * kPi * 0.19 * time));

    for (const double sample : {left, right}) {
      const auto pcm = static_cast<std::int16_t>(
          std::lround(
              std::clamp(sample, -1.0, 1.0) * 32767.0));
      AppendU16(
          wave,
          static_cast<std::uint16_t>(pcm));
    }
  }
  return wave;
}

}  // namespace cx::ui
