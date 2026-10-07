#include "browser/bookmark_service.h"

namespace cx::browser {

BookmarkService::BookmarkService(
    storage::Database& database)
    : database_(database) {}

std::int64_t BookmarkService::Add(
    std::string_view url,
    std::string_view title) {
  if (url.empty() || url == "about:blank") {
    return -1;
  }
  return database_.AddBookmark(url, title);
}

std::vector<storage::Bookmark>
BookmarkService::List() const {
  return database_.ListBookmarks();
}

bool BookmarkService::Remove(std::int64_t id) {
  return database_.DeleteBookmark(id);
}

}  // namespace cx::browser
