#pragma once

#include <string>

#include "model.h"

class LogParser {
public:
    bool parse_file(const std::string& path, ParsedStats& out_stats, std::string& out_error) const;
    bool parse_stats_line(const std::string& line, ParsedStats& stats) const;
};
