#include "mcp/json_rpc_validator.h"

#include <cctype>
#include <limits>
#include <unordered_set>

namespace cx::mcp {
namespace {

class Cursor {
public:
  explicit Cursor(std::string_view input) : input_(input) {}

  void SkipWhitespace() {
    while (position_ < input_.size() &&
           std::isspace(
               static_cast<unsigned char>(input_[position_]))) {
      ++position_;
    }
  }

  bool Consume(char expected) {
    SkipWhitespace();
    if (position_ >= input_.size() ||
        input_[position_] != expected) {
      return false;
    }
    ++position_;
    return true;
  }

  bool End() {
    SkipWhitespace();
    return position_ == input_.size();
  }

  bool String(std::string* output) {
    SkipWhitespace();
    if (position_ >= input_.size() ||
        input_[position_] != '"') {
      return false;
    }
    ++position_;
    output->clear();
    while (position_ < input_.size()) {
      const unsigned char ch =
          static_cast<unsigned char>(input_[position_++]);
      if (ch == '"') {
        return true;
      }
      if (ch < 0x20) {
        return false;
      }
      if (ch != '\\') {
        output->push_back(static_cast<char>(ch));
        continue;
      }
      if (position_ >= input_.size()) {
        return false;
      }
      const char escaped = input_[position_++];
      switch (escaped) {
        case '"': output->push_back('"'); break;
        case '\\': output->push_back('\\'); break;
        case '/': output->push_back('/'); break;
        case 'b': output->push_back('\b'); break;
        case 'f': output->push_back('\f'); break;
        case 'n': output->push_back('\n'); break;
        case 'r': output->push_back('\r'); break;
        case 't': output->push_back('\t'); break;
        default: return false;
      }
    }
    return false;
  }

  bool Number() {
    SkipWhitespace();
    const std::size_t start = position_;
    if (position_ < input_.size() &&
        input_[position_] == '-') {
      ++position_;
    }
    if (position_ >= input_.size() ||
        !std::isdigit(
            static_cast<unsigned char>(input_[position_]))) {
      return false;
    }
    if (input_[position_] == '0') {
      ++position_;
    } else {
      while (position_ < input_.size() &&
             std::isdigit(
                 static_cast<unsigned char>(
                     input_[position_]))) {
        ++position_;
      }
    }
    if (position_ < input_.size() &&
        input_[position_] == '.') {
      ++position_;
      const std::size_t fraction = position_;
      while (position_ < input_.size() &&
             std::isdigit(
                 static_cast<unsigned char>(
                     input_[position_]))) {
        ++position_;
      }
      if (fraction == position_) {
        return false;
      }
    }
    if (position_ < input_.size() &&
        (input_[position_] == 'e' ||
         input_[position_] == 'E')) {
      ++position_;
      if (position_ < input_.size() &&
          (input_[position_] == '+' ||
           input_[position_] == '-')) {
        ++position_;
      }
      const std::size_t exponent = position_;
      while (position_ < input_.size() &&
             std::isdigit(
                 static_cast<unsigned char>(
                     input_[position_]))) {
        ++position_;
      }
      if (exponent == position_) {
        return false;
      }
    }
    return position_ > start;
  }

  bool Literal(std::string_view literal) {
    SkipWhitespace();
    if (input_.substr(position_, literal.size()) != literal) {
      return false;
    }
    position_ += literal.size();
    return true;
  }

  bool Value(std::size_t depth) {
    if (depth > JsonRpcValidator::kMaxDepth) {
      return false;
    }
    SkipWhitespace();
    if (position_ >= input_.size()) {
      return false;
    }
    if (input_[position_] == '"') {
      std::string ignored;
      return String(&ignored);
    }
    if (input_[position_] == '{') {
      return Object(depth + 1);
    }
    if (input_[position_] == '[') {
      return Array(depth + 1);
    }
    const char first = input_[position_];
    if (first == '-' || std::isdigit(
            static_cast<unsigned char>(first))) {
      return Number();
    }
    if (first == 't') {
      return Literal("true");
    }
    if (first == 'f') {
      return Literal("false");
    }
    if (first == 'n') {
      return Literal("null");
    }
    return false;
  }

  bool Object(std::size_t depth) {
    if (!Consume('{')) {
      return false;
    }
    SkipWhitespace();
    if (Consume('}')) {
      return true;
    }
    while (true) {
      std::string key;
      if (!String(&key) ||
          !Consume(':') ||
          !Value(depth)) {
        return false;
      }
      SkipWhitespace();
      if (Consume('}')) {
        return true;
      }
      if (!Consume(',')) {
        return false;
      }
    }
  }

  bool Array(std::size_t depth) {
    if (!Consume('[')) {
      return false;
    }
    SkipWhitespace();
    if (Consume(']')) {
      return true;
    }
    while (true) {
      if (!Value(depth)) {
        return false;
      }
      SkipWhitespace();
      if (Consume(']')) {
        return true;
      }
      if (!Consume(',')) {
        return false;
      }
    }
  }

  char Peek() {
    SkipWhitespace();
    return position_ < input_.size()
        ? input_[position_]
        : '\0';
  }

private:
  std::string_view input_;
  std::size_t position_ = 0;
};

bool IsValidMethod(std::string_view method) {
  if (method.empty() || method.size() > 128) {
    return false;
  }
  for (const unsigned char ch : method) {
    if (!std::isalnum(ch) &&
        ch != '.' && ch != '/' &&
        ch != '_' && ch != '-') {
      return false;
    }
  }
  return true;
}

}  // namespace

bool JsonRpcValidator::ValidateRequest(
    std::string_view json,
    std::string* method) {
  if (!method) {
    return false;
  }
  method->clear();
  Cursor cursor(json);
  if (!cursor.Consume('{')) {
    return false;
  }

  bool has_version = false;
  bool has_id = false;
  bool has_method = false;
  std::unordered_set<std::string> keys;

  while (true) {
    if (cursor.Consume('}')) {
      break;
    }

    std::string key;
    if (!cursor.String(&key) ||
        !keys.insert(key).second ||
        !cursor.Consume(':')) {
      return false;
    }

    if (key == "jsonrpc") {
      std::string version;
      if (!cursor.String(&version) ||
          version != "2.0") {
        return false;
      }
      has_version = true;
    } else if (key == "id") {
      std::string string_id;
      if (cursor.Peek() == '"') {
        if (!cursor.String(&string_id) ||
            string_id.empty() ||
            string_id.size() > 128) {
          return false;
        }
      } else if (!cursor.Number()) {
        return false;
      }
      has_id = true;
    } else if (key == "method") {
      if (!cursor.String(method) ||
          !IsValidMethod(*method)) {
        return false;
      }
      has_method = true;
    } else if (key == "params") {
      const char shape = cursor.Peek();
      if ((shape != '{' && shape != '[') ||
          !cursor.Value(0)) {
        return false;
      }
    } else {
      return false;
    }

    if (cursor.Consume('}')) {
      break;
    }
    if (!cursor.Consume(',') || cursor.Peek() == '}') {
      return false;
    }
  }

  return has_version && has_id && has_method &&
      cursor.End();
}

}  // namespace cx::mcp
