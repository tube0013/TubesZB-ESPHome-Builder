#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>
namespace esphome::select {
class Select {
 public:
  std::vector<std::string> options{"115200", "230400", "460800", "921600"};
  std::string value{"115200"};
  std::function<void()> callback;
  const std::string &current_option() const { return value; }
  bool has_option(const std::string &s) const { return std::find(options.begin(), options.end(), s) != options.end(); }
  class Call {
   public:
    Select *parent;
    std::string value;
    Call &set_option(const std::string &s) { value = s; return *this; }
    void perform() { if (parent->callback) parent->callback(); parent->value = value; }
  };
  Call make_call() { return {this, {}}; }
};
}
