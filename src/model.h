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

struct ShutdownEvent {
    std::size_t line_index = 0;
    double wall_t = 0.0;
    std::string reason;
    std::string analysis;
    std::vector<std::string> dump_lines;
};

struct TimingIssueEvent {
    std::size_t line_index = 0;
    double wall_t = 0.0;
    std::string type;
    std::string line_text;
    std::string analysis;
};

struct ParsedStats {
    std::unordered_map<std::string, Series> series_by_key;
    std::vector<std::string> ordered_keys;
    std::size_t total_stats_lines = 0;
    double min_t = 0.0;
    double max_t = 0.0;
    bool has_time = false;
    std::vector<std::string> raw_lines;
    std::vector<double> raw_line_wall_t;
    std::vector<std::size_t> stats_line_indices;
    std::vector<double> stats_line_wall_t;
    std::vector<ShutdownEvent> shutdown_events;
    std::vector<TimingIssueEvent> timing_issue_events;
};
