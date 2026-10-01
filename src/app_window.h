#pragma once
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

class AppWindow {
public:
  int Run(HINSTANCE instance, int show_command);

private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool Create(HINSTANCE instance, int show_command);
  void InitializeWebView();
  void ResizeWebView();

  HWND hwnd_ = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
};
