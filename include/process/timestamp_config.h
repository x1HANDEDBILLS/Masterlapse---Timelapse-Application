#pragma once
#include <mutex>

struct TimestampConfig {
    double rel_x = 0.05;
    double rel_y = 0.10;
    double scale = 1.0;
    bool enabled = true;
};

extern TimestampConfig g_ts_config;
extern std::mutex g_ts_mutex;
