#pragma once

#include "storage/database.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace cx::browser {

class HistoryService;

class HistoryDialog {
public:
  HistoryDialog(
      HistoryService& history,
      std::function<void(std::string)> open_url);

  void Show(HWND owner);

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message,
      WPARAM wparam, LPARAM lparam);

  void CreateControls();
  void Refresh();
  void HandleCommand(WORD command);

  std::string QueryText() const;
  int SelectedIndex() const;

  HistoryService& history_;
  std::function<void(std::string)> open_url_;
  std::vector<storage::HistoryEntry> entries_;

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND query_ = nullptr;
  HWND list_ = nullptr;
};

}  // namespace cx::browser
