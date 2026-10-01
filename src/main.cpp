#include "app_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  AppWindow app;
  return app.Run(instance, show_command);
}
