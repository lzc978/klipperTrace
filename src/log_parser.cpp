#include "log_parser.h"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool parse_double_fast(const std::string& s, double& out) {
    const std::string cleaned = trim_copy(s);
    if (cleaned.empty()) return false;

    const char* begin = cleaned.data();
    const char* end = cleaned.data() + cleaned.size();
    auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec == std::errc() && ptr == end) {
        return true;
    }

    // Some toolchains still have edge-cases in floating from_chars.
    char* parse_end = nullptr;
    out = std::strtod(begin, &parse_end);
    return parse_end == end;
}

bool token_is_group(const std::string& token, bool prev_was_kv) {
    if (!prev_was_kv || token.size() < 2 || token.back() != ':') {
        return false;
    }
    for (char c : token) {
        if (c == ':') {
            continue;
        }
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool parse_stats_time(const std::string& line, double& out_time, std::size_t& payload_pos) {
    const std::size_t marker = line.find("Stats ");
    if (marker == std::string::npos) {
        return false;
    }
    const std::size_t t_begin = marker + 6;
    const std::size_t colon = line.find(':', t_begin);
    if (colon == std::string::npos || colon <= t_begin) {
        return false;
    }
    const std::string t_str = line.substr(t_begin, colon - t_begin);
    if (!parse_double_fast(t_str, out_time)) {
        return false;
    }
    payload_pos = colon + 1;
    return true;
}

bool parse_wallclock_prefix_time(const std::string& line, double& out_time) {
    // Expected prefix sample:
    // [INFO] 2026-06-01 00:00:01,607 [root] ...
    if (line.size() < 29) return false;
    const std::size_t rb = line.find(']');
    if (rb == std::string::npos) return false;
    std::size_t p = rb + 1;
    while (p < line.size() && std::isspace(static_cast<unsigned char>(line[p]))) p++;
    if (p + 23 > line.size()) return false;

    auto all_digits = [&](std::size_t pos, std::size_t n) {
        if (pos + n > line.size()) return false;
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(line[pos + i]))) return false;
        }
        return true;
    };

    if (!all_digits(p + 0, 4) || line[p + 4] != '-' || !all_digits(p + 5, 2) || line[p + 7] != '-' || !all_digits(p + 8, 2) ||
        line[p + 10] != ' ' || !all_digits(p + 11, 2) || line[p + 13] != ':' || !all_digits(p + 14, 2) || line[p + 16] != ':' ||
        !all_digits(p + 17, 2) || line[p + 19] != ',' || !all_digits(p + 20, 3)) {
        return false;
    }

    std::tm tmv{};
    tmv.tm_year = std::atoi(line.substr(p + 0, 4).c_str()) - 1900;
    tmv.tm_mon = std::atoi(line.substr(p + 5, 2).c_str()) - 1;
    tmv.tm_mday = std::atoi(line.substr(p + 8, 2).c_str());
    tmv.tm_hour = std::atoi(line.substr(p + 11, 2).c_str());
    tmv.tm_min = std::atoi(line.substr(p + 14, 2).c_str());
    tmv.tm_sec = std::atoi(line.substr(p + 17, 2).c_str());

    const int ms = std::atoi(line.substr(p + 20, 3).c_str());
    std::time_t base = std::mktime(&tmv);
    if (base == static_cast<std::time_t>(-1)) return false;
    out_time = static_cast<double>(base) + static_cast<double>(ms) / 1000.0;
    return true;
}

void add_point(ParsedStats& stats, const std::string& group, const std::string& field, double t, double stats_t, double v) {
    const std::string key = group + "." + field;
    auto it = stats.series_by_key.find(key);
    if (it == stats.series_by_key.end()) {
        Series s;
        s.group = group;
        s.field = field;
        s.label = key;
        it = stats.series_by_key.emplace(key, std::move(s)).first;
        stats.ordered_keys.push_back(key);
    }
    it->second.points.push_back({t, stats_t, v});
}

}  // namespace

bool LogParser::parse_file(const std::string& path, ParsedStats& out_stats, std::string& out_error) const {
    std::ifstream ifs(std::filesystem::u8path(path));
    if (!ifs) {
        out_error = "Cannot open log file: " + path;
        return false;
    }

    out_stats = ParsedStats{};
    std::string line;
    while (std::getline(ifs, line)) {
        parse_stats_line(line, out_stats);
    }

    if (out_stats.total_stats_lines == 0) {
        out_error = "No 'Stats <time>:' lines found in file.";
        return false;
    }
    return true;
}

bool LogParser::parse_stats_line(const std::string& line, ParsedStats& stats) const {
    double stats_t = 0.0;
    std::size_t payload_pos = 0;
    if (!parse_stats_time(line, stats_t, payload_pos)) {
        return false;
    }

    // Force wall-clock prefix timestamp as X axis.
    // We intentionally do not fall back to "Stats <sec>".
    double wall_t = 0.0;
    if (!parse_wallclock_prefix_time(line, wall_t)) {
        return false;
    }
    const double t = wall_t;

    stats.total_stats_lines++;
    if (!stats.has_time) {
        stats.has_time = true;
        stats.min_t = t;
        stats.max_t = t;
    } else {
        if (t < stats.min_t) stats.min_t = t;
        if (t > stats.max_t) stats.max_t = t;
    }

    std::istringstream iss(line.substr(payload_pos));
    std::string token;
    std::string current_group = "global";
    bool prev_was_kv = true;

    while (iss >> token) {
        if (token_is_group(token, prev_was_kv)) {
            current_group = token.substr(0, token.size() - 1);
            prev_was_kv = false;
            continue;
        }

        const std::size_t eq = token.find('=');
        if (eq == std::string::npos || eq == 0 || eq == token.size() - 1) {
            prev_was_kv = false;
            continue;
        }

        const std::string field = token.substr(0, eq);
        const std::string value = token.substr(eq + 1);
        double v = 0.0;
        if (!parse_double_fast(value, v)) {
            prev_was_kv = false;
            continue;
        }
        add_point(stats, current_group, field, t, stats_t, v);
        prev_was_kv = true;
    }
    return true;
}
