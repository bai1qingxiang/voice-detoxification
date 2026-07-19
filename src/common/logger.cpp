#include "logger.h"
#include <iostream>

/// Writes an informational message with a consistent console prefix.
void log_info(const char* message) {
    std::cout << "[INFO] " << message << std::endl;
}
