#pragma once

#include "storage/database.h"

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

namespace cx::browser {

class BookmarkService;

class BookmarksDialog {
public:
  BookmarksDialog(
      BookmarkService& bookmarks,
      std::function<void(std::string)> open_url);

  void Show(HWND owner);
  void RefreshIfOpen();

private:
  static LRESULT CALLBACK WndProc(
      HWND hwnd, UINT message,
      WPARAM wparam, LPARAM lparam);
  void CreateControls();
  void Refresh();
  void HandleCommand(WORD command);

  int SelectedIndex() const;

  BookmarkService& bookmarks_;
  std::function<void(std::string)> open_url_;
  std::vector<storage::Bookmark> entries_;

  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND list_ = nullptr;
};

}  // namespace cx::browser
