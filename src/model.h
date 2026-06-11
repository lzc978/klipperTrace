#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct SamplePoint {
    double t = 0.0;
    double stats_t = 0.0;
    double v = 0.0;
};

struct Series {
    std::string group;
    std::string field;
    std::string label;
    std::vector<SamplePoint> points;
    bool visible = false;
};

struct ParsedStats {
    std::unordered_map<std::string, Series> series_by_key;
    std::vector<std::string> ordered_keys;
    std::size_t total_stats_lines = 0;
    double min_t = 0.0;
    double max_t = 0.0;
    bool has_time = false;
};
