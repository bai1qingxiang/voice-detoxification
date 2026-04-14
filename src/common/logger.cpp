#include "logger.h"
#include <iostream>

void log_info(const char* message) {
    std::cout << "[INFO] " << message << std::endl;
}