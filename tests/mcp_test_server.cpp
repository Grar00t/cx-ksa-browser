#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc == 3 && std::string(argv[1]) == "--marker") {
    std::ofstream marker(argv[2], std::ios::trunc);
    marker << "started\n";
    marker.flush();
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    std::cout << line << '\n';
    std::cout.flush();
  }
  return 0;
}
