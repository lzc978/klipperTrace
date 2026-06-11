#include "log_parser.h"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <vector>

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

bool contains_icase(const std::string& text, const std::string& pat) {
    if (pat.empty() || text.size() < pat.size()) return false;
    for (std::size_t i = 0; i + pat.size() <= text.size(); ++i) {
        bool ok = true;
        for (std::size_t j = 0; j < pat.size(); ++j) {
            const unsigned char a = static_cast<unsigned char>(text[i + j]);
            const unsigned char b = static_cast<unsigned char>(pat[j]);
            if (std::tolower(a) != std::tolower(b)) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

std::string extract_json_msg_field(const std::string& line) {
    const std::size_t p = line.find("\"msg\"");
    if (p == std::string::npos) return {};
    const std::size_t q1 = line.find('"', p + 5);
    if (q1 == std::string::npos) return {};
    const std::size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos || q2 <= q1) return {};
    return line.substr(q1 + 1, q2 - q1 - 1);
}

bool parse_field_value_in_scope(const std::string& line, const std::string& scope, const std::string& field, double& out) {
    const std::string anchor = scope + ":";
    const std::size_t sp = line.find(anchor);
    if (sp == std::string::npos) return false;
    const std::size_t eq = line.find(field + "=", sp);
    if (eq == std::string::npos) return false;
    std::size_t val_begin = eq + field.size() + 1;
    std::size_t val_end = val_begin;
    while (val_end < line.size()) {
        const char c = line[val_end];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+') {
            val_end++;
            continue;
        }
        break;
    }
    if (val_end <= val_begin) return false;
    return parse_double_fast(line.substr(val_begin, val_end - val_begin), out);
}

bool parse_field_value_any_token(const std::string& line, const std::string& field, double& out) {
    const std::string needle = field + "=";
    std::size_t pos = 0;
    while (true) {
        pos = line.find(needle, pos);
        if (pos == std::string::npos) return false;
        const bool left_ok = (pos == 0) || std::isspace(static_cast<unsigned char>(line[pos - 1])) || line[pos - 1] == ':';
        if (left_ok) break;
        pos += needle.size();
    }
    std::size_t val_begin = pos + needle.size();
    std::size_t val_end = val_begin;
    while (val_end < line.size()) {
        const char c = line[val_end];
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '+') {
            val_end++;
            continue;
        }
        break;
    }
    if (val_end <= val_begin) return false;
    return parse_double_fast(line.substr(val_begin, val_end - val_begin), out);
}

std::string build_timing_issue_analysis(const std::string& type, const std::string& near_stats_line) {
    std::ostringstream oss;
    oss << "Type: " << type << ". ";
    if (contains_icase(type, "timer too close") || contains_icase(type, "rescheduled timer in the past") ||
        contains_icase(type, "missed scheduling")) {
        oss << "Likely host/MCU timing jitter or scheduling latency.";
    } else {
        oss << "Potential timing/scheduling anomaly.";
    }

    if (!near_stats_line.empty()) {
        double sysload = std::numeric_limits<double>::quiet_NaN();
        double buffer_time = std::numeric_limits<double>::quiet_NaN();
        double print_stall = std::numeric_limits<double>::quiet_NaN();
        double mcu_srtt = std::numeric_limits<double>::quiet_NaN();
        double mcu_rttvar = std::numeric_limits<double>::quiet_NaN();
        double mcu_retx = std::numeric_limits<double>::quiet_NaN();
        double mcu_stalled = std::numeric_limits<double>::quiet_NaN();
        const bool ok_sysload = parse_field_value_any_token(near_stats_line, "sysload", sysload);
        const bool ok_buffer = parse_field_value_any_token(near_stats_line, "buffer_time", buffer_time);
        const bool ok_stall = parse_field_value_any_token(near_stats_line, "print_stall", print_stall);
        const bool ok_srtt = parse_field_value_in_scope(near_stats_line, "mcu", "srtt", mcu_srtt);
        const bool ok_rttvar = parse_field_value_in_scope(near_stats_line, "mcu", "rttvar", mcu_rttvar);
        const bool ok_retx = parse_field_value_in_scope(near_stats_line, "mcu", "bytes_retransmit", mcu_retx);
        const bool ok_stalled = parse_field_value_in_scope(near_stats_line, "mcu", "stalled_bytes", mcu_stalled);

        oss << " Nearest stats:";
        if (ok_sysload) oss << " sysload=" << sysload;
        if (ok_buffer) oss << " buffer_time=" << buffer_time;
        if (ok_stall) oss << " print_stall=" << print_stall;
        if (ok_srtt) oss << " mcu.srtt=" << mcu_srtt;
        if (ok_rttvar) oss << " mcu.rttvar=" << mcu_rttvar;
        if (ok_retx) oss << " mcu.bytes_retransmit=" << mcu_retx;
        if (ok_stalled) oss << " mcu.stalled_bytes=" << mcu_stalled;
        oss << ".";

        if (ok_buffer && buffer_time < 0.02) {
            oss << " Buffer is near empty; motion queue starvation is possible.";
        }
        if (ok_sysload && sysload > 2.0) {
            oss << " Host sysload is elevated; check CPU contention and background tasks.";
        }
        if (ok_rttvar && mcu_rttvar > 0.002) {
            oss << " MCU RTT variance is high; communication jitter may contribute.";
        }
    }
    return oss.str();
}

std::string build_timing_issue_analysis_cn(const std::string& type, const std::string& near_stats_line) {
    std::ostringstream oss;
    oss << "类型: " << type << "。";
    if (contains_icase(type, "timer too close") || contains_icase(type, "rescheduled timer in the past") ||
        contains_icase(type, "missed scheduling")) {
        oss << " 这通常是主机/MCU 时序抖动或调度延迟导致。";
    } else {
        oss << " 这是潜在的时序/调度异常。";
    }

    if (!near_stats_line.empty()) {
        double sysload = std::numeric_limits<double>::quiet_NaN();
        double buffer_time = std::numeric_limits<double>::quiet_NaN();
        double print_stall = std::numeric_limits<double>::quiet_NaN();
        double mcu_srtt = std::numeric_limits<double>::quiet_NaN();
        double mcu_rttvar = std::numeric_limits<double>::quiet_NaN();
        double mcu_retx = std::numeric_limits<double>::quiet_NaN();
        double mcu_stalled = std::numeric_limits<double>::quiet_NaN();
        const bool ok_sysload = parse_field_value_any_token(near_stats_line, "sysload", sysload);
        const bool ok_buffer = parse_field_value_any_token(near_stats_line, "buffer_time", buffer_time);
        const bool ok_stall = parse_field_value_any_token(near_stats_line, "print_stall", print_stall);
        const bool ok_srtt = parse_field_value_in_scope(near_stats_line, "mcu", "srtt", mcu_srtt);
        const bool ok_rttvar = parse_field_value_in_scope(near_stats_line, "mcu", "rttvar", mcu_rttvar);
        const bool ok_retx = parse_field_value_in_scope(near_stats_line, "mcu", "bytes_retransmit", mcu_retx);
        const bool ok_stalled = parse_field_value_in_scope(near_stats_line, "mcu", "stalled_bytes", mcu_stalled);

        oss << " 邻近 Stats 指标:";
        if (ok_sysload) oss << " sysload=" << sysload;
        if (ok_buffer) oss << " buffer_time=" << buffer_time;
        if (ok_stall) oss << " print_stall=" << print_stall;
        if (ok_srtt) oss << " mcu.srtt=" << mcu_srtt;
        if (ok_rttvar) oss << " mcu.rttvar=" << mcu_rttvar;
        if (ok_retx) oss << " mcu.bytes_retransmit=" << mcu_retx;
        if (ok_stalled) oss << " mcu.stalled_bytes=" << mcu_stalled;
        oss << "。";

        if (ok_buffer && buffer_time < 0.02) {
            oss << " 缓冲时间接近 0，可能存在供数不足/队列饥饿。";
        }
        if (ok_sysload && sysload > 2.0) {
            oss << " 主机负载偏高，建议排查 CPU 争用与后台任务。";
        }
        if (ok_rttvar && mcu_rttvar > 0.002) {
            oss << " MCU RTT 抖动偏大，通信链路波动可能是诱因。";
        }
    }
    return oss.str();
}

std::string build_shutdown_analysis(const std::string& reason, const std::string& near_stats_line) {
    std::ostringstream oss;
    if (!reason.empty()) {
        oss << "Root cause: " << reason << ".";
    } else {
        oss << "Root cause: unknown (no explicit shutdown message found).";
    }

    if (contains_icase(reason, "not heating at expected rate")) {
        oss << " Likely heater performance issue (heater cartridge / thermistor / fan cooling / PID mismatch).";
    } else if (contains_icase(reason, "timer too close")) {
        oss << " Likely MCU scheduling or host latency pressure around motion timing.";
    } else if (contains_icase(reason, "mcu") && contains_icase(reason, "shutdown")) {
        oss << " MCU entered shutdown; inspect communication timing and command queue dumps.";
    }

    if (!near_stats_line.empty()) {
        std::size_t heater_pos = std::string::npos;
        if (contains_icase(reason, "Heater ")) {
            heater_pos = reason.find("Heater ");
        } else if (contains_icase(reason, "heater ")) {
            heater_pos = reason.find("heater ");
        }
        if (heater_pos != std::string::npos) {
            std::size_t name_begin = heater_pos + 7;
            std::size_t name_end = near_stats_line.size();
            for (std::size_t i = name_begin; i < reason.size(); ++i) {
                if (std::isspace(static_cast<unsigned char>(reason[i]))) {
                    name_end = i;
                    break;
                }
            }
            const std::string heater = reason.substr(name_begin, name_end - name_begin);
            double target = 0.0;
            double temp = 0.0;
            double pwm = 0.0;
            bool ok_tgt = parse_field_value_in_scope(near_stats_line, heater, "target", target);
            bool ok_tmp = parse_field_value_in_scope(near_stats_line, heater, "temp", temp);
            bool ok_pwm = parse_field_value_in_scope(near_stats_line, heater, "pwm", pwm);
            if (ok_tgt || ok_tmp || ok_pwm) {
                oss << " Last stats before shutdown -> " << heater << ":";
                if (ok_tgt) oss << " target=" << target;
                if (ok_tmp) oss << " temp=" << temp;
                if (ok_pwm) oss << " pwm=" << pwm;
                oss << ".";
            }
        }
    }
    return oss.str();
}

std::string build_shutdown_analysis_cn(const std::string& reason, const std::string& near_stats_line) {
    std::ostringstream oss;
    if (!reason.empty()) {
        oss << "根因: " << reason << "。";
    } else {
        oss << "根因: 未知（日志中未提取到明确 shutdown 消息）。";
    }

    if (contains_icase(reason, "not heating at expected rate")) {
        oss << " 更可能是加热性能问题（加热棒/热敏、风冷干扰、PID 参数不匹配）。";
    } else if (contains_icase(reason, "timer too close")) {
        oss << " 更可能是 MCU 调度或主机时序延迟导致。";
    } else if (contains_icase(reason, "mcu") && contains_icase(reason, "shutdown")) {
        oss << " MCU 已进入 shutdown，建议重点查看通信时序与命令队列 dump。";
    }

    if (!near_stats_line.empty()) {
        std::size_t heater_pos = std::string::npos;
        if (contains_icase(reason, "Heater ")) {
            heater_pos = reason.find("Heater ");
        } else if (contains_icase(reason, "heater ")) {
            heater_pos = reason.find("heater ");
        }
        if (heater_pos != std::string::npos) {
            std::size_t name_begin = heater_pos + 7;
            std::size_t name_end = near_stats_line.size();
            for (std::size_t i = name_begin; i < reason.size(); ++i) {
                if (std::isspace(static_cast<unsigned char>(reason[i]))) {
                    name_end = i;
                    break;
                }
            }
            const std::string heater = reason.substr(name_begin, name_end - name_begin);
            double target = 0.0;
            double temp = 0.0;
            double pwm = 0.0;
            bool ok_tgt = parse_field_value_in_scope(near_stats_line, heater, "target", target);
            bool ok_tmp = parse_field_value_in_scope(near_stats_line, heater, "temp", temp);
            bool ok_pwm = parse_field_value_in_scope(near_stats_line, heater, "pwm", pwm);
            if (ok_tgt || ok_tmp || ok_pwm) {
                oss << " shutdown 前最后一次 Stats -> " << heater << ":";
                if (ok_tgt) oss << " target=" << target;
                if (ok_tmp) oss << " temp=" << temp;
                if (ok_pwm) oss << " pwm=" << pwm;
                oss << "。";
            }
        }
    }
    return oss.str();
}

void parse_timing_issue_events(const std::vector<std::string>& lines, ParsedStats& stats) {
    stats.timing_issue_events.clear();
    if (lines.empty()) return;

    auto match_issue = [](const std::string& s, std::string& out_type) -> bool {
        if (contains_icase(s, "Timer too close")) {
            out_type = "Timer too close";
            return true;
        }
        if (contains_icase(s, "Rescheduled timer in the past")) {
            out_type = "Rescheduled timer in the past";
            return true;
        }
        if (contains_icase(s, "Missed scheduling of next")) {
            out_type = "Missed scheduling of next";
            return true;
        }
        return false;
    };

    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string type;
        if (!match_issue(lines[i], type)) continue;
        TimingIssueEvent ev;
        ev.line_index = i;
        parse_wallclock_prefix_time(lines[i], ev.wall_t);
        ev.type = type;
        ev.line_text = lines[i];

        std::string near_stats_line;
        for (std::size_t j = i; j > 0; --j) {
            const std::size_t k = j - 1;
            if (lines[k].find("Stats ") != std::string::npos) {
                near_stats_line = lines[k];
                break;
            }
            if (i - k > 220) break;
        }
        const std::string en = build_timing_issue_analysis(type, near_stats_line);
        const std::string cn = build_timing_issue_analysis_cn(type, near_stats_line);
        ev.analysis = "EN: " + en + "\n中文: " + cn;
        stats.timing_issue_events.push_back(std::move(ev));
    }
}

void parse_shutdown_events(const std::vector<std::string>& lines, ParsedStats& stats) {
    stats.shutdown_events.clear();
    if (lines.empty()) return;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (line.find("Transition to shutdown state:") == std::string::npos) continue;

        ShutdownEvent ev;
        ev.line_index = i;
        parse_wallclock_prefix_time(line, ev.wall_t);
        ev.reason = extract_json_msg_field(line);
        if (ev.reason.empty()) ev.reason = "Transition to shutdown state";

        std::string near_stats_line;
        for (std::size_t j = i; j > 0; --j) {
            const std::size_t k = j - 1;
            if (lines[k].find("Stats ") != std::string::npos) {
                near_stats_line = lines[k];
                break;
            }
            if (i - k > 180) break;
        }
        const std::string en = build_shutdown_analysis(ev.reason, near_stats_line);
        const std::string cn = build_shutdown_analysis_cn(ev.reason, near_stats_line);
        ev.analysis = "EN: " + en + "\n中文: " + cn;

        const std::size_t dump_begin = i;
        const std::size_t dump_end = std::min(lines.size(), i + static_cast<std::size_t>(260));
        for (std::size_t k = dump_begin; k < dump_end; ++k) {
            const std::string& s = lines[k];
            const bool is_dump = s.find("Dumping ") != std::string::npos || s.find("clocksync state:") != std::string::npos ||
                                 s.find("Dumping serial stats:") != std::string::npos || s.find("MCU '") != std::string::npos ||
                                 s.find("Requested toolhead position at shutdown time") != std::string::npos ||
                                 s.find("virtual_sdcard:handle_shutdown") != std::string::npos || s.rfind("Sent ", 0) == 0 ||
                                 s.rfind("Receive:", 0) == 0;
            if (is_dump) {
                ev.dump_lines.push_back(s);
            }
        }
        stats.shutdown_events.push_back(std::move(ev));
    }
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
    std::size_t line_index = 0;
    while (std::getline(ifs, line)) {
        out_stats.raw_lines.push_back(line);
        double wall_t = std::numeric_limits<double>::quiet_NaN();
        parse_wallclock_prefix_time(line, wall_t);
        out_stats.raw_line_wall_t.push_back(wall_t);
        parse_stats_line(line, out_stats, line_index);
        line_index++;
    }

    parse_shutdown_events(out_stats.raw_lines, out_stats);
    parse_timing_issue_events(out_stats.raw_lines, out_stats);

    if (out_stats.total_stats_lines == 0) {
        out_error = "No 'Stats <time>:' lines found in file.";
        return false;
    }
    return true;
}

bool LogParser::parse_stats_line(const std::string& line, ParsedStats& stats, std::size_t line_index) const {
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
    if (line_index != static_cast<std::size_t>(-1)) {
        stats.stats_line_indices.push_back(line_index);
        stats.stats_line_wall_t.push_back(t);
    }
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
