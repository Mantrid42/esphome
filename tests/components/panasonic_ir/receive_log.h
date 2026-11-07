#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "esphome/components/logger/logger.h"

namespace esphome::panasonic_ir::testing {

class ReceiveLog {
 public:
  ReceiveLog() {
    this->logger_.pre_setup();
    this->logger_.set_log_level(ESPHOME_LOG_LEVEL_DEBUG);
    this->logger_.add_log_callback(
        this, [](void *instance, uint8_t level, const char *tag, const char *message, size_t len) {
          if (level == ESPHOME_LOG_LEVEL_DEBUG && std::string_view(tag).starts_with("panasonic_ir."))
            static_cast<ReceiveLog *>(instance)->lines.emplace_back(message, len);
        });
  }
  ~ReceiveLog() { logger::global_logger = this->previous_; }

  std::vector<std::string> lines;

 protected:
  logger::Logger *previous_{logger::global_logger};
  logger::Logger logger_{0};
};

}  // namespace esphome::panasonic_ir::testing
