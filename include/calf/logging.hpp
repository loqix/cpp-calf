#ifndef CALF_LOGGING_HPP_
#define CALF_LOGGING_HPP_

#include "singleton.hpp"

#include <sstream>
#include <string>
#include <iostream>
#include <codecvt>
#include <map>
#include <mutex>
#include <memory>
#include <cctype>
#include <algorithm>

namespace calf {
namespace logging {

enum class log_level {
  verbose,
  info,
  warn,
  error,
  fatal
};

class log_target {
public:
  virtual ~log_target() {}

  virtual void output(const std::wstring& data) = 0;
  virtual void sync() {}
};

class log_stderr_target
  : public log_target {
public:
  void output(const std::wstring& data) override {
    std::wcerr << data;
  }

  void sync() override {
    std::wcerr.flush();
  }
};

class log_stdout_target 
  : public log_target {
public:
  void output(const std::wstring& data) override {
    std::wcout << data;
  }

  void sync() override {
    std::wcout.flush();
  }
};

class log_manager : public singleton<log_manager> {
public:
  log_manager() : default_target_("stdout") {
    std::unique_lock<std::mutex> lock(mutex_);
    targets_.emplace("stdout", std::make_unique<log_stdout_target>());
    targets_.emplace("stderr", std::make_unique<log_stderr_target>());
  }

  log_target* get_target(const char* name) {
    if (name == nullptr) {
      name = default_target_.c_str();  // 默认输出
    }
    std::unique_lock<std::mutex> lock(mutex_);
    auto it = targets_.find(name);
    if (it != targets_.end()) {
      return it->second.get();
    }

    name = default_target_.c_str();
    it = targets_.find(name);
    if (it != targets_.end()) {
      return it->second.get();
    }
    return nullptr;
  }

  void add_target(const char* name, std::unique_ptr<log_target> target) {
    std::unique_lock<std::mutex> lock(mutex_);
    targets_.emplace(name, std::move(target));
  }

  void set_default_target(const char* name) {
    default_target_ = name;
  }

private:
  std::map<std::string, std::unique_ptr<log_target>> targets_;
  std::mutex mutex_;
  std::string default_target_;
};

class log {
public:
  log(const char* target_name, log_level level, const wchar_t* file, int line)
    : target_(nullptr) {
    target_ = log_manager::instance()->get_target(target_name);
    stream_ << L"[CALF ";
    if (target_name != nullptr) {
      std::string name(target_name);
      std::transform(name.begin(), name.end(), name.begin(), ::toupper);
      *this << name.c_str() << L" ";
    }
    stream_ << get_level_string(level) << "][" << file << L"(" << line << L")] ";
  }

  ~log() {
    stream_ << std::endl;
    if (target_ != nullptr) {
      target_->output(stream_.str());
    }
  }

  template<typename T>
  log& operator<< (T&& t) {
    stream_ << t;
    return *this;
  }

  template<>
  log& operator<< (const char*&& str) {   // VS2013在这里有问题，不能用 const char*。
    stream_ << convert_.from_bytes(str);
    return *this;
  }

  template<>
  log& operator<< (std::string&& str) {
    stream_ << convert_.from_bytes(str);
    return *this;
  }

private:
  static const wchar_t* get_level_string(log_level level) {
    switch (level)
    {
    case log_level::verbose:
      return L"VERBOSE";
    case log_level::info:
      return L"INFO";
    case log_level::warn:
      return L"WARN";
    case log_level::error:
      return L"ERROR";
    case log_level::fatal:
      return L"FATAL";
    default:
      return L"UNKNOWN";
    }
  }

protected:
  log_target* target_;
  std::wstringstream stream_;
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>, wchar_t> convert_;
};

} // namespace logging
} // namespace calf

#define CALF_LOG(level) calf::logging::log(nullptr, calf::logging::log_level::level, __FILEW__, __LINE__)
#define CALF_LOG_TARGET(target, level) calf::logging::log(#target, calf::logging::log_level::level, __FILEW__, __LINE__)

#endif // CALF_LOGGING_HPP_