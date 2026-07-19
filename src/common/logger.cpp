#include "logger.h"
#include <iostream>

/// 使用统一的控制台前缀输出信息消息。
void log_info(const char* message) {
    std::cout << "[INFO] " << message << std::endl;
}
