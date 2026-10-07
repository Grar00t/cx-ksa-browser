#pragma once

#include <array>

namespace cx::storage {

struct Migration {
  int version;
  const char* sql;
};

inline constexpr std::array<Migration, 3> kMigrations{{
    {1, R"SQL(
CREATE TABLE settings (
  key TEXT PRIMARY KEY NOT NULL,
  value TEXT NOT NULL,
  updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);

CREATE TABLE tabs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  position INTEGER NOT NULL,
  url TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  pinned INTEGER NOT NULL DEFAULT 0 CHECK (pinned IN (0, 1)),
  created_at INTEGER NOT NULL DEFAULT (unixepoch()),
  updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX tabs_position_idx ON tabs(position, id);

CREATE TABLE history (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  url TEXT NOT NULL,
  title TEXT NOT NULL DEFAULT '',
  visited_at INTEGER NOT NULL
);

CREATE INDEX history_visited_at_idx
  ON history(visited_at DESC, id DESC);
)SQL"},
    {2, R"SQL(
CREATE TABLE permissions (
  capability TEXT PRIMARY KEY NOT NULL,
  granted INTEGER NOT NULL DEFAULT 0 CHECK (granted IN (0, 1)),
  updated_at INTEGER NOT NULL DEFAULT (unixepoch())
);
)SQL"},
    {3, R"SQL(
CREATE TABLE bookmarks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  url TEXT NOT NULL UNIQUE,
  title TEXT NOT NULL DEFAULT '',
  created_at INTEGER NOT NULL DEFAULT (unixepoch())
);
CREATE INDEX bookmarks_created_at_idx
  ON bookmarks(created_at DESC, id DESC);
)SQL"},
}};

}  // namespace cx::storage
