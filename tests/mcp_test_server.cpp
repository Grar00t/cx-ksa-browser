#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
  int startup_delay_ms = 0;
  int response_delay_ms = 0;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--marker" && i + 1 < argc) {
      std::ofstream marker(argv[++i], std::ios::trunc);
      marker << "started\n";
      marker.flush();
      continue;
    }
    if (arg == "--startup-delay-ms" && i + 1 < argc) {
      startup_delay_ms = std::max(0, std::atoi(argv[++i]));
      continue;
    }
    if (arg == "--response-delay-ms" && i + 1 < argc) {
      response_delay_ms = std::max(0, std::atoi(argv[++i]));
      continue;
    }
  }

  if (startup_delay_ms > 0) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(startup_delay_ms));
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    if (response_delay_ms > 0) {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(response_delay_ms));
    }
    std::cout << line << '\n';
    std::cout.flush();
  }
  return 0;
}
