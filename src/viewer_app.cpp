#include "viewer_app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <sstream>

#include "imgui.h"
#include "implot.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#endif

namespace {
constexpr const char* kProjectReleasesUrl = "https://github.com/lzc978/klipperTrace/releases";

std::string trim_copy(std::string s) {
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    if (s.size() >= 2 && ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\''))) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

ImPlotPoint point_getter(int idx, void* data) {
    const auto* points = static_cast<const std::vector<SamplePoint>*>(data);
    const SamplePoint& p = (*points)[static_cast<std::size_t>(idx)];
    return {p.t, p.v};
}

int wallclock_axis_formatter(double value, char* buff, int size, void* user_data) {
    const bool show_date = (user_data != nullptr) ? (*static_cast<bool*>(user_data)) : false;
    const auto sec = static_cast<std::time_t>(value);
    const int ms = static_cast<int>(std::floor((value - std::floor(value)) * 1000.0));
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &sec);
#else
    localtime_r(&sec, &tmv);
#endif
    if (show_date) {
        return std::snprintf(buff, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    }
    return std::snprintf(buff, size, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

std::string now_stamp_for_file() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto tt = system_clock::to_time_t(now);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d_%02d%02d%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
                  tmv.tm_min, tmv.tm_sec);
    return buf;
}

std::string format_wallclock(double value, bool show_date, bool with_ms) {
    const auto sec = static_cast<std::time_t>(value);
    const int ms = static_cast<int>(std::floor((value - std::floor(value)) * 1000.0));
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &sec);
#else
    localtime_r(&sec, &tmv);
#endif
    char buf[96];
    if (show_date) {
        if (with_ms) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                          tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }
    } else {
        if (with_ms) {
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
        } else {
            std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        }
    }
    return buf;
}

std::string analysis_text_for_ui(const std::string& combined, bool zh) {
    const std::string en_prefix = "EN: ";
    const std::string cn_sep = "\n中文: ";
    const std::size_t en_pos = combined.find(en_prefix);
    const std::size_t cn_pos = combined.find(cn_sep);
    if (en_pos == std::string::npos || cn_pos == std::string::npos || cn_pos <= en_pos) {
        return combined;
    }
    if (zh) return combined;
    return combined.substr(en_pos + en_prefix.size(), cn_pos - (en_pos + en_prefix.size()));
}

ImVec4 stable_series_color(const std::string& key) {
    static const ImVec4 kPalette[] = {
        ImVec4(0.86f, 0.20f, 0.20f, 1.0f), ImVec4(0.20f, 0.45f, 0.86f, 1.0f), ImVec4(0.20f, 0.72f, 0.34f, 1.0f),
        ImVec4(0.83f, 0.52f, 0.15f, 1.0f), ImVec4(0.55f, 0.30f, 0.80f, 1.0f), ImVec4(0.16f, 0.65f, 0.70f, 1.0f),
        ImVec4(0.75f, 0.27f, 0.57f, 1.0f), ImVec4(0.35f, 0.35f, 0.35f, 1.0f), ImVec4(0.12f, 0.56f, 0.38f, 1.0f),
        ImVec4(0.76f, 0.34f, 0.18f, 1.0f), ImVec4(0.18f, 0.36f, 0.76f, 1.0f), ImVec4(0.62f, 0.18f, 0.18f, 1.0f),
    };
    const std::size_t h = std::hash<std::string>{}(key);
    return kPalette[h % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

std::vector<SamplePoint> smooth_points(const std::vector<SamplePoint>& src, int window) {
    if (window <= 1 || src.size() < 3) return src;
    std::vector<SamplePoint> out;
    out.resize(src.size());
    const int half = window / 2;
    for (std::size_t i = 0; i < src.size(); ++i) {
        int l = static_cast<int>(i) - half;
        int r = static_cast<int>(i) + half;
        if (l < 0) l = 0;
        if (r >= static_cast<int>(src.size())) r = static_cast<int>(src.size()) - 1;
        double sum = 0.0;
        int n = 0;
        for (int j = l; j <= r; ++j) {
            sum += src[static_cast<std::size_t>(j)].v;
            n++;
        }
        out[i] = src[i];
        out[i].v = (n > 0) ? (sum / static_cast<double>(n)) : src[i].v;
    }
    return out;
}

const char* tr(bool zh, const char* en, const char* cn) {
    return zh ? cn : en;
}

bool open_url_in_browser(const char* url) {
#ifdef _WIN32
    const HINSTANCE r = ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(r) > 32;
#else
    std::string cmd = "xdg-open \"";
    cmd += url;
    cmd += "\" >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

std::string field_cn_label(const std::string& field) {
    static const std::unordered_map<std::string, std::string> kFieldCn = {
        {"temp", "温度"},         {"target", "目标温度"}, {"pwm", "功率输出"},
        {"mcu_awake", "MCU唤醒占比"}, {"mcu_task_avg", "MCU任务平均耗时"},
        {"mcu_task_stddev", "MCU任务耗时标准差"}, {"bytes_write", "写字节数"},
        {"bytes_read", "读字节数"}, {"bytes_retransmit", "重传字节数"},
        {"bytes_invalid", "无效字节数"}, {"send_seq", "发送序列号"},
        {"receive_seq", "接收序列号"}, {"retransmit_seq", "重传序列号"},
        {"srtt", "平滑往返时延"}, {"rttvar", "时延抖动"},
        {"rto", "超时重传阈值"}, {"ready_bytes", "就绪字节数"},
        {"stalled_bytes", "阻塞字节数"}, {"freq", "频率"},
        {"adj", "校准频率"}, {"sysload", "系统负载"},
        {"cputime", "CPU时间"}, {"memavail", "可用内存"},
        {"print_time", "打印时间"}, {"buffer_time", "缓冲时间"},
        {"print_stall", "打印阻塞次数"}, {"sd_pos", "SD位置"},
    };
    auto it = kFieldCn.find(field);
    if (it == kFieldCn.end()) return {};
    return it->second;
}

std::string group_cn_label(const std::string& group) {
    static const std::unordered_map<std::string, std::string> kGroupCn = {
        {"mcu", "主控MCU"},
        {"nozzle_mcu", "喷头MCU"},
        {"rpi", "主机RPi"},
        {"extruder", "挤出机0"},
        {"extruder1", "挤出机1"},
        {"extruder2", "挤出机2"},
        {"extruder3", "挤出机3"},
        {"heater_bed", "热床"},
        {"print_stats", "打印统计"},
        {"global", "全局"},
    };
    auto it = kGroupCn.find(group);
    if (it == kGroupCn.end()) return {};
    return it->second;
}

}  // namespace

ViewerApp::ViewerApp() {
    log_path_buf_[0] = '\0';
    filter_buf_[0] = '\0';
    tail_cmd_buf_[0] = '\0';
    std::snprintf(csv_path_buf_, sizeof(csv_path_buf_), "%s", csv_path_.c_str());
    load_visibility_state();
}

ViewerApp::~ViewerApp() {
    stop_tail();
    save_visibility_state();
}

const char* ViewerApp::axis_name(YAxisClass axis) {
    switch (axis) {
        case YAxisClass::Temperature:
            return "Temperature";
        case YAxisClass::RatioLatency:
            return "Ratio/Latency";
        case YAxisClass::Bytes:
            return "Bytes";
        case YAxisClass::SequenceCounter:
            return "Seq/Counter";
        case YAxisClass::Frequency:
            return "Frequency";
        case YAxisClass::Time:
            return "Time";
        case YAxisClass::Memory:
            return "Memory";
        case YAxisClass::Position:
            return "Position/Misc";
        default:
            return "Misc";
    }
}

void ViewerApp::set_initial_log_path(std::string path) {
    log_path_ = std::move(path);
    std::snprintf(log_path_buf_, sizeof(log_path_buf_), "%s", log_path_.c_str());
}

void ViewerApp::load_log() {
    stop_tail();
    error_.clear();
    info_.clear();
    group_to_keys_.clear();
    group_order_.clear();
    stats_ = ParsedStats{};

    const std::string cleaned_path = trim_copy(log_path_buf_);
    if (cleaned_path.empty()) {
        error_ = "Please input a log file path first.";
        return;
    }
    log_path_ = cleaned_path;
    std::snprintf(log_path_buf_, sizeof(log_path_buf_), "%s", log_path_.c_str());

    std::string parse_error;
    ParsedStats parsed;
    if (!parser_.parse_file(log_path_, parsed, parse_error)) {
        error_ = parse_error;
        return;
    }

    stats_ = std::move(parsed);
    rebuild_groups();
    reset_visibility_defaults();
    load_visibility_state();
    if (!stats_.stats_line_indices.empty()) {
        select_log_line(stats_.stats_line_indices.front());
    } else if (!stats_.raw_lines.empty()) {
        select_log_line(0);
    } else {
        selected_log_line_ = static_cast<std::size_t>(-1);
    }
    selected_shutdown_index_ = 0;
    selected_timing_issue_index_ = 0;
    info_ = "Loaded: " + std::to_string(stats_.total_stats_lines) + " Stats lines, " +
            std::to_string(stats_.series_by_key.size()) + " series, " +
            std::to_string(stats_.shutdown_events.size()) + " shutdown event(s), " +
            std::to_string(stats_.timing_issue_events.size()) + " timing issue event(s).";

    if (auto_fit_on_load_) {
        request_fit_next_frame_ = true;
    }

    if (source_mode_ != SourceMode::File && realtime_enabled_) {
        start_tail();
    }
}

void ViewerApp::reset_visibility_defaults() {
    int shown = 0;
    for (const std::string& key : stats_.ordered_keys) {
        auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        it->second.visible = (shown < 8);
        if (it->second.visible) shown++;
    }
}

void ViewerApp::rebuild_groups() {
    group_to_keys_.clear();
    group_order_.clear();
    for (const std::string& key : stats_.ordered_keys) {
        const auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        group_to_keys_[it->second.group].push_back(key);
    }
    for (auto& kv : group_to_keys_) {
        std::sort(kv.second.begin(), kv.second.end());
    }
    for (const auto& kv : group_to_keys_) {
        group_order_.push_back(kv.first);
    }
    std::sort(group_order_.begin(), group_order_.end());
}

void ViewerApp::apply_pending_tail_lines() {
    std::deque<std::string> local;
    {
        std::lock_guard<std::mutex> lock(tail_.mutex);
        if (tail_.pending_lines.empty()) return;
        local.swap(tail_.pending_lines);
    }

    bool added = false;
    int processed = 0;
    for (const auto& line : local) {
        if (processed++ > 5000) break;
        if (parser_.parse_stats_line(line, stats_)) {
            added = true;
        }
    }
    if (added) {
        rebuild_groups();
        load_visibility_state();
        info_ = "Realtime: " + std::to_string(stats_.total_stats_lines) + " Stats lines.";
    }
}

void ViewerApp::tail_file_worker(std::string path) {
    while (!tail_.stop.load()) {
        std::ifstream ifs(std::filesystem::u8path(path));
        if (!ifs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        ifs.seekg(0, std::ios::end);
        std::streamoff end_pos = ifs.tellg();
        std::streamoff start = static_cast<std::streamoff>(last_file_pos_);
        if (start > end_pos) start = 0;
        ifs.seekg(start, std::ios::beg);

        std::string line;
        while (!tail_.stop.load()) {
            bool has_line = false;
            while (std::getline(ifs, line)) {
                has_line = true;
                std::lock_guard<std::mutex> lock(tail_.mutex);
                tail_.pending_lines.push_back(line);
                if (tail_.pending_lines.size() > 20000) tail_.pending_lines.pop_front();
            }
            last_file_pos_ = static_cast<std::size_t>(ifs.tellg());
            if (!has_line) {
                ifs.clear();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }
    }
}

void ViewerApp::tail_command_worker(std::string command) {
#ifdef _WIN32
    FILE* fp = _popen(command.c_str(), "r");
#else
    FILE* fp = popen(command.c_str(), "r");
#endif
    if (!fp) {
        error_ = "Failed to start command: " + command;
        return;
    }

    char buf[4096];
    while (!tail_.stop.load()) {
        if (std::fgets(buf, sizeof(buf), fp) != nullptr) {
            std::string line = trim_copy(buf);
            if (!line.empty()) {
                std::lock_guard<std::mutex> lock(tail_.mutex);
                tail_.pending_lines.push_back(std::move(line));
                if (tail_.pending_lines.size() > 20000) tail_.pending_lines.pop_front();
            }
        } else {
            break;
        }
    }
#ifdef _WIN32
    _pclose(fp);
#else
    pclose(fp);
#endif
}

void ViewerApp::start_tail() {
    stop_tail();
    tail_.stop.store(false);
    tail_.running.store(true);

    if (source_mode_ == SourceMode::TailFile) {
        std::error_code ec;
        last_file_pos_ = static_cast<std::size_t>(std::filesystem::file_size(std::filesystem::u8path(log_path_), ec));
        if (ec) last_file_pos_ = 0;
        tail_.worker = std::thread([this]() { tail_file_worker(log_path_); });
        info_ = "Realtime tail enabled for local file.";
        return;
    }

    if (source_mode_ == SourceMode::TailCommand) {
        tail_command_ = trim_copy(tail_cmd_buf_);
        if (tail_command_.empty()) {
            error_ = "Tail command is empty.";
            tail_.running.store(false);
            return;
        }
        tail_.worker = std::thread([this]() { tail_command_worker(tail_command_); });
        info_ = "Realtime tail enabled for command stream.";
        return;
    }

    tail_.running.store(false);
}

void ViewerApp::stop_tail() {
    tail_.stop.store(true);
    if (tail_.worker.joinable()) tail_.worker.join();
    tail_.running.store(false);
}

void ViewerApp::save_visibility_state() {
    if (!visibility_dirty_ && !axis_map_dirty_) return;
    std::ofstream ofs(std::filesystem::u8path(state_path_), std::ios::trunc);
    if (!ofs) return;
    for (const auto& key : stats_.ordered_keys) {
        const auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        ofs << "V|" << key << "=" << (it->second.visible ? "1" : "0") << "\n";
    }
    for (const auto& kv : axis_override_by_key_) {
        ofs << "A|" << kv.first << "=" << static_cast<int>(kv.second) << "\n";
    }
    visibility_dirty_ = false;
    axis_map_dirty_ = false;
}

void ViewerApp::load_visibility_state() {
    std::ifstream ifs(std::filesystem::u8path(state_path_));
    if (!ifs) return;
    axis_override_by_key_.clear();

    std::string line;
    while (std::getline(ifs, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq == 0 || eq + 1 >= line.size()) continue;
        const std::string lhs = line.substr(0, eq);
        const std::string rhs = line.substr(eq + 1);
        if (lhs.size() > 2 && lhs[1] == '|') {
            const std::string key = lhs.substr(2);
            if (lhs[0] == 'V') {
                const bool v = !rhs.empty() && rhs[0] == '1';
                auto it = stats_.series_by_key.find(key);
                if (it != stats_.series_by_key.end()) it->second.visible = v;
            } else if (lhs[0] == 'A') {
                int a = std::atoi(rhs.c_str());
                if (a < 0 || a > static_cast<int>(YAxisClass::Position)) continue;
                axis_override_by_key_[key] = static_cast<YAxisClass>(a);
            }
        } else {
            // Backward compatibility with old format: key=0/1
            const std::string key = lhs;
            const bool v = !rhs.empty() && rhs[0] == '1';
            auto it = stats_.series_by_key.find(key);
            if (it != stats_.series_by_key.end()) it->second.visible = v;
        }
    }
}

void ViewerApp::export_csv() {
    csv_path_ = trim_copy(csv_path_buf_);
    if (csv_path_.empty()) {
        error_ = "CSV path is empty.";
        return;
    }

    std::ofstream ofs(std::filesystem::u8path(csv_path_), std::ios::trunc);
    if (!ofs) {
        error_ = "Cannot write CSV: " + csv_path_;
        return;
    }
    ofs << "time,series,value\n";
    std::size_t rows = 0;
    for (const auto& key : stats_.ordered_keys) {
        auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        const Series& s = it->second;
        if (!s.visible) continue;
        for (const auto& p : s.points) {
            ofs << p.t << "," << s.label << "," << p.v << "\n";
            rows++;
        }
    }
    info_ = "CSV exported: " + csv_path_ + " (" + std::to_string(rows) + " rows)";
}

bool ViewerApp::browse_log_file() {
#ifdef _WIN32
    char path_buf[1024] = {0};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = path_buf;
    ofn.nMaxFile = sizeof(path_buf);
    ofn.lpstrFilter = "Log Files\0*.log;*.txt\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileNameA(&ofn) == TRUE) {
        std::snprintf(log_path_buf_, sizeof(log_path_buf_), "%s", path_buf);
        return true;
    }
#endif
    return false;
}

YAxisClass ViewerApp::classify_series(const Series& s) const {
    auto ov = axis_override_by_key_.find(s.label);
    if (ov != axis_override_by_key_.end()) return ov->second;

    const std::string key = s.label;
    // Field-based priority first, then fallback by label patterns.
    if (s.field == "temp" || s.field == "target") {
        return YAxisClass::Temperature;
    }
    if (s.field == "pwm" || key.find("mcu_awake") != std::string::npos || key.find("mcu_task_") != std::string::npos ||
        key.find("srtt") != std::string::npos || key.find("rtt") != std::string::npos || key.find("rto") != std::string::npos ||
        key.find("sysload") != std::string::npos || key.find(".buffer_time") != std::string::npos) {
        return YAxisClass::RatioLatency;
    }
    if (key.find("bytes_") != std::string::npos || key.find("ready_bytes") != std::string::npos ||
        key.find("stalled_bytes") != std::string::npos) {
        return YAxisClass::Bytes;
    }
    if (key.find("_seq") != std::string::npos || key.find("print_stall") != std::string::npos || key.find("gcodein") != std::string::npos) {
        return YAxisClass::SequenceCounter;
    }
    if (key.find(".freq") != std::string::npos || key.find(".adj") != std::string::npos) {
        return YAxisClass::Frequency;
    }
    if (key.find("cputime") != std::string::npos || key.find("print_time") != std::string::npos) {
        return YAxisClass::Time;
    }
    if (key.find("memavail") != std::string::npos) {
        return YAxisClass::Memory;
    }
    if (key.find("sd_pos") != std::string::npos || key.find("measuring_wheel") != std::string::npos || key.find("addr") != std::string::npos) {
        return YAxisClass::Position;
    }
    return YAxisClass::SequenceCounter;
}

void ViewerApp::setup_axis_limits_from_visible() {}

std::size_t ViewerApp::find_nearest_stats_line_index(double t) const {
    if (stats_.stats_line_wall_t.empty() || stats_.stats_line_indices.empty()) {
        return static_cast<std::size_t>(-1);
    }
    const auto& ts = stats_.stats_line_wall_t;
    auto it = std::lower_bound(ts.begin(), ts.end(), t);
    std::size_t best_i = 0;
    if (it == ts.end()) {
        best_i = ts.size() - 1;
    } else if (it == ts.begin()) {
        best_i = 0;
    } else {
        const std::size_t i = static_cast<std::size_t>(it - ts.begin());
        const double d0 = std::abs(ts[i] - t);
        const double d1 = std::abs(ts[i - 1] - t);
        best_i = (d1 <= d0) ? (i - 1) : i;
    }
    if (best_i >= stats_.stats_line_indices.size()) return static_cast<std::size_t>(-1);
    return stats_.stats_line_indices[best_i];
}

std::size_t ViewerApp::find_nearest_raw_line_index_by_time(double t) const {
    if (stats_.raw_line_wall_t.empty()) return stats_.raw_lines.empty() ? static_cast<std::size_t>(-1) : 0;
    std::size_t best_idx = static_cast<std::size_t>(-1);
    double best_dist = 1e100;
    bool best_is_stats = true;
    int best_severity = -1;  // ERROR(2) > WARN(1) > other(0)
    for (std::size_t i = 0; i < stats_.raw_line_wall_t.size(); ++i) {
        const double ts = stats_.raw_line_wall_t[i];
        if (!std::isfinite(ts)) continue;
        const double d = std::abs(ts - t);
        const std::string& line = stats_.raw_lines[i];
        const bool is_stats = (line.find("Stats ") != std::string::npos);
        int severity = 0;
        if (line.find("[ERROR]") != std::string::npos) severity = 2;
        else if (line.find("[WARN]") != std::string::npos || line.find("[WARNING]") != std::string::npos) severity = 1;

        const bool better_dist = d < best_dist - 1e-9;
        const bool tie_dist = std::abs(d - best_dist) <= 1e-9;
        const bool better_kind = tie_dist && (best_is_stats && !is_stats);
        const bool better_severity = tie_dist && (best_is_stats == is_stats) && (severity > best_severity);
        if (better_dist || better_kind || better_severity) {
            best_dist = d;
            best_idx = i;
            best_is_stats = is_stats;
            best_severity = severity;
        }
    }
    if (best_idx != static_cast<std::size_t>(-1)) return best_idx;
    const std::size_t stats_idx = find_nearest_stats_line_index(t);
    if (stats_idx != static_cast<std::size_t>(-1)) return stats_idx;
    return stats_.raw_lines.empty() ? static_cast<std::size_t>(-1) : 0;
}

void ViewerApp::select_log_line(std::size_t idx) {
    if (idx == static_cast<std::size_t>(-1) || idx >= stats_.raw_lines.size()) return;
    selected_log_line_ = idx;
    if (idx < stats_.raw_line_wall_t.size() && std::isfinite(stats_.raw_line_wall_t[idx])) {
        selected_time_ = stats_.raw_line_wall_t[idx];
        return;
    }
    if (!stats_.stats_line_indices.empty() && !stats_.stats_line_wall_t.empty()) {
        auto it = std::lower_bound(stats_.stats_line_indices.begin(), stats_.stats_line_indices.end(), idx);
        if (it != stats_.stats_line_indices.end()) {
            const std::size_t pos = static_cast<std::size_t>(it - stats_.stats_line_indices.begin());
            if (pos < stats_.stats_line_wall_t.size()) {
                selected_time_ = stats_.stats_line_wall_t[pos];
            }
        }
    }
}

bool ViewerApp::capture_click_selection_on_axis(YAxisClass axis) {
    if (!ImPlot::IsPlotHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        return false;
    }
    (void)axis;
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
    if (!std::isfinite(mouse.x)) return false;
    selected_time_ = mouse.x;
    const std::size_t idx = find_nearest_raw_line_index_by_time(mouse.x);
    select_log_line(idx);
    return idx != static_cast<std::size_t>(-1);
}

bool ViewerApp::draw_shutdown_markers() {
    if (stats_.shutdown_events.empty()) return false;

    std::vector<double> xs_other;
    std::vector<double> xs_selected;
    xs_other.reserve(stats_.shutdown_events.size());
    xs_selected.reserve(1);
    for (std::size_t i = 0; i < stats_.shutdown_events.size(); ++i) {
        const auto& ev = stats_.shutdown_events[i];
        if (ev.wall_t <= 0.0) continue;
        if (i == selected_shutdown_index_) xs_selected.push_back(ev.wall_t);
        else xs_other.push_back(ev.wall_t);
    }

    if (!xs_other.empty()) {
        const ImPlotSpec spec(ImPlotProp_LineColor, ImVec4(1.0f, 0.35f, 0.2f, 0.35f), ImPlotProp_LineWeight, 1.0f);
        ImPlot::PlotInfLines("shutdown##others", xs_other.data(), static_cast<int>(xs_other.size()), spec);
    }
    if (!xs_selected.empty()) {
        const ImPlotSpec spec(ImPlotProp_LineColor, ImVec4(1.0f, 0.35f, 0.2f, 0.95f), ImPlotProp_LineWeight, 2.6f);
        ImPlot::PlotInfLines("shutdown##selected", xs_selected.data(), static_cast<int>(xs_selected.size()), spec);
    }

    if (!ImPlot::IsPlotHovered()) return false;
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
    const ImVec2 mp = ImPlot::PlotToPixels(mouse, ImAxis_X1, ImAxis_Y1);

    std::size_t best_idx = static_cast<std::size_t>(-1);
    float best_dx = 1e9f;
    for (std::size_t i = 0; i < stats_.shutdown_events.size(); ++i) {
        const auto& ev = stats_.shutdown_events[i];
        if (ev.wall_t <= 0.0) continue;
        const ImVec2 px = ImPlot::PlotToPixels(ImPlotPoint(ev.wall_t, mouse.y), ImAxis_X1, ImAxis_Y1);
        const float dx = std::abs(px.x - mp.x);
        if (dx < best_dx) {
            best_dx = dx;
            best_idx = i;
        }
    }

    if (best_idx == static_cast<std::size_t>(-1) || best_dx > 8.0f) return false;
    const auto& ev = stats_.shutdown_events[best_idx];

    ImGui::BeginTooltip();
    ImGui::Text("shutdown #%zu", best_idx + 1);
    ImGui::Separator();
    ImGui::TextUnformatted(ev.reason.c_str());
    ImGui::Text("line: %zu", ev.line_index + 1);
    ImGui::EndTooltip();

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selected_shutdown_index_ = best_idx;
        selected_time_ = ev.wall_t;
        select_log_line(ev.line_index);
        return true;
    }
    return false;
}

void ViewerApp::draw_hover_tooltip_for_axis(YAxisClass axis) {
    if (!ImPlot::IsPlotHovered()) return;
    const ImPlotPoint mouse = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);

    const Series* best_series = nullptr;
    const SamplePoint* best_point = nullptr;
    float best_px_dist = 1e9f;

    for (const auto& key : stats_.ordered_keys) {
        auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        const Series& s = it->second;
        if (!s.visible || s.points.empty()) continue;
        if (classify_series(s) != axis) continue;

        const auto lb = std::lower_bound(
            s.points.begin(), s.points.end(), mouse.x, [](const SamplePoint& p, double xval) { return p.t < xval; });

        auto check_candidate = [&](std::size_t idx) {
            if (idx >= s.points.size()) return;
            const SamplePoint& p = s.points[idx];
            const ImPlotPoint pt(p.t, p.v);
            const ImVec2 px = ImPlot::PlotToPixels(pt, ImAxis_X1, ImAxis_Y1);
            const ImVec2 mp = ImPlot::PlotToPixels(mouse, ImAxis_X1, ImAxis_Y1);
            const float dx = px.x - mp.x;
            const float dy = px.y - mp.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < best_px_dist) {
                best_px_dist = dist;
                best_series = &s;
                best_point = &p;
            }
        };

        if (lb != s.points.end()) check_candidate(static_cast<std::size_t>(lb - s.points.begin()));
        if (lb != s.points.begin()) check_candidate(static_cast<std::size_t>((lb - s.points.begin()) - 1));
    }

    if (best_series != nullptr && best_point != nullptr && best_px_dist <= 14.0f) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(best_series->label.c_str());
        ImGui::Separator();
        const std::string tstr = format_wallclock(best_point->t, x_axis_show_date_, true);
        ImGui::Text("time: %s", tstr.c_str());
        ImGui::Text("value: %.6f", best_point->v);
        ImGui::Text("stats: %.3f", best_point->stats_t);
        ImGui::EndTooltip();
    }
}

void ViewerApp::draw_axis_plot(YAxisClass axis, const char* title, const char* y_label, const ImVec2& size) {
    if (!ImPlot::BeginPlot(title, size, ImPlotFlags_NoMouseText)) {
        return;
    }

    bool has_any = false;
    double min_x = 0.0, max_x = 0.0, min_y = 0.0, max_y = 0.0;
    int visible_count = 0;
    auto update_mm = [&](double x, double y) {
        if (!has_any) {
            has_any = true;
            min_x = max_x = x;
            min_y = max_y = y;
        } else {
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
            if (y < min_y) min_y = y;
            if (y > max_y) max_y = y;
        }
    };
    for (const auto& key : stats_.ordered_keys) {
        auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        const Series& s = it->second;
        if (!s.visible || s.points.empty()) continue;
        if (classify_series(s) != axis) continue;
        for (const auto& p : s.points) update_mm(p.t, p.v);
        visible_count++;
    }

    ImPlot::SetupAxis(ImAxis_X1, "time");
    ImPlot::SetupAxisFormat(ImAxis_X1, wallclock_axis_formatter, &x_axis_show_date_);
    ImPlot::SetupAxis(ImAxis_Y1, y_label);
    if (has_any) {
        if (request_fit_next_frame_) {
            ImPlot::SetupAxisLimits(ImAxis_X1, min_x, max_x, ImGuiCond_Always);
            if (auto_fit_y_axes_) {
                double y_min = min_y;
                double y_max = max_y;
                if (axis == YAxisClass::Temperature && clamp_temp_axis_) {
                    y_min = std::max(0.0, std::min(min_y, 25.0));
                    y_max = std::max(300.0, max_y * 1.05);
                }
                if (axis == YAxisClass::RatioLatency && clamp_ratio_axis_) {
                    y_min = 0.0;
                    y_max = std::max(1.0, max_y * 1.1);
                }
                if (y_max <= y_min) y_max = y_min + 1.0;
                const double pad = (y_max - y_min) * 0.08;
                ImPlot::SetupAxisLimits(ImAxis_Y1, y_min - pad, y_max + pad, ImGuiCond_Always);
            }
        }
    }

    if (visible_count == 0) {
        ImPlot::PlotDummy("No visible series");
    } else {
        const std::string highlight = trim_copy(highlight_field_buf_);
        for (const auto& key : stats_.ordered_keys) {
            auto it = stats_.series_by_key.find(key);
            if (it == stats_.series_by_key.end()) continue;
            const Series& s = it->second;
            if (!s.visible || s.points.empty()) continue;
            if (classify_series(s) != axis) continue;

            const bool highlighted = (!highlight.empty() && s.label.find(highlight) != std::string::npos);
            const float line_weight = highlighted ? 4.0f : 2.8f;
            const ImVec4 line_color = stable_series_color(s.label);

            if (smooth_enabled_) {
                std::vector<SamplePoint> smooth = smooth_points(s.points, smooth_window_);
                const ImPlotSpec spec(ImPlotProp_LineColor, line_color, ImPlotProp_LineWeight, line_weight, ImPlotProp_Marker,
                                      ImPlotMarker_None);
                ImPlot::PlotLineG(s.label.c_str(), point_getter, (void*)&smooth, static_cast<int>(smooth.size()), spec);
            } else {
                const ImPlotSpec spec(ImPlotProp_LineColor, line_color, ImPlotProp_LineWeight, line_weight, ImPlotProp_Marker,
                                      ImPlotMarker_None);
                ImPlot::PlotLineG(s.label.c_str(), point_getter, (void*)&s.points, static_cast<int>(s.points.size()), spec);
            }
        }
    }

    const bool clicked_shutdown = draw_shutdown_markers();
    draw_hover_tooltip_for_axis(axis);
    if (!clicked_shutdown) {
        capture_click_selection_on_axis(axis);
    }
    ImPlot::EndPlot();
}

void ViewerApp::draw_top_bar() {
    apply_pending_tail_lines();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 3));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 4));

    ImGui::TextUnformatted(tr(use_chinese_ui_, "KlipperTrace", "KlipperTrace"));
    ImGui::SameLine();
    int lang_idx = use_chinese_ui_ ? 0 : 1;
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::Combo("##lang", &lang_idx, "中文\0English\0")) {
        use_chinese_ui_ = (lang_idx == 0);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(360.0f);
    const bool enter_pressed = ImGui::InputTextWithHint(
        "##logpath",
        tr(use_chinese_ui_, "Klipper host/MCU related log path only (supports quotes)", "仅支持 Klipper 上下位机相关日志路径（支持引号）"),
                                                         log_path_buf_, sizeof(log_path_buf_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "Browse", "浏览"))) browse_log_file();
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "Load", "加载"))) load_log();
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "Settings", "设置"))) ImGui::OpenPopup("ui_settings_popup");
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "Tools", "工具"))) {
        ImGui::OpenPopup("tools_popup");
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "About", "关于"))) {
        ImGui::OpenPopup("about_popup");
    }
    if (ImGui::BeginPopup("tools_popup")) {
        if (ImGui::MenuItem(tr(use_chinese_ui_, "Fit X", "X轴自适应"))) {
            request_fit_next_frame_ = true;
        }
        if (ImGui::MenuItem(tr(use_chinese_ui_, "Export CSV", "导出CSV"))) {
            export_csv();
        }
        if (ImGui::MenuItem(tr(use_chinese_ui_, "Export Screenshot", "导出截图"))) {
            screenshot_path_ = "screenshot_" + now_stamp_for_file() + ".ppm";
            screenshot_requested_ = true;
        }
        ImGui::EndPopup();
    }
    if (enter_pressed) load_log();

    if (ImGui::BeginPopup("about_popup")) {
        ImGui::TextUnformatted("KlipperTrace");
        ImGui::Separator();
        ImGui::TextUnformatted(tr(use_chinese_ui_, "GitHub Releases:", "GitHub 发布页："));
        ImGui::TextWrapped("%s", kProjectReleasesUrl);
        if (ImGui::Button(tr(use_chinese_ui_, "Open Link", "打开链接"))) {
            if (open_url_in_browser(kProjectReleasesUrl)) {
                info_ = tr(use_chinese_ui_, "Opened GitHub Releases in browser.", "已在浏览器打开 GitHub 发布页。");
            } else {
                error_ = tr(use_chinese_ui_, "Failed to open browser.", "打开浏览器失败。");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr(use_chinese_ui_, "Copy Link", "复制链接"))) {
            ImGui::SetClipboardText(kProjectReleasesUrl);
            info_ = tr(use_chinese_ui_, "Releases URL copied to clipboard.", "发布页链接已复制到剪贴板。");
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("ui_settings_popup")) {
        const char* modes[] = {"File", "Tail File", "Tail Command (SSH)"};
        int mode_i = static_cast<int>(source_mode_);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo(tr(use_chinese_ui_, "Source", "数据源"), &mode_i, modes, IM_ARRAYSIZE(modes))) {
            source_mode_ = static_cast<SourceMode>(mode_i);
        }
        bool rt = realtime_enabled_;
        if (ImGui::Checkbox(tr(use_chinese_ui_, "Realtime tail", "实时Tail"), &rt)) {
            realtime_enabled_ = rt;
            if (realtime_enabled_ && source_mode_ != SourceMode::File) start_tail();
            else stop_tail();
        }
        ImGui::Checkbox(tr(use_chinese_ui_, "Auto fit on load", "加载后自动适配"), &auto_fit_on_load_);
        ImGui::Checkbox(tr(use_chinese_ui_, "Auto fit Y", "Y轴自动缩放"), &auto_fit_y_axes_);
        ImGui::Checkbox(tr(use_chinese_ui_, "Clamp Temp to 0-300+", "温度轴钳制到0-300+"), &clamp_temp_axis_);
        ImGui::Checkbox(tr(use_chinese_ui_, "Clamp Ratio to 0-1+", "比例轴钳制到0-1+"), &clamp_ratio_axis_);
        ImGui::Checkbox(tr(use_chinese_ui_, "X show date", "X显示日期"), &x_axis_show_date_);
        ImGui::Checkbox(tr(use_chinese_ui_, "Smooth", "平滑"), &smooth_enabled_);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::SliderInt(tr(use_chinese_ui_, "window", "窗口"), &smooth_window_, 3, 21);
        if ((smooth_window_ % 2) == 0) smooth_window_ += 1;
        ImGui::SetNextItemWidth(360.0f);
        ImGui::InputText(tr(use_chinese_ui_, "CSV path", "CSV路径"), csv_path_buf_, sizeof(csv_path_buf_));
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputText(tr(use_chinese_ui_, "Highlight field", "高亮字段"), highlight_field_buf_, sizeof(highlight_field_buf_));
        if (source_mode_ == SourceMode::TailCommand) {
            ImGui::SetNextItemWidth(520.0f);
            ImGui::InputTextWithHint("##tailcmd", "ssh user@host \"tail -F /tmp/klippy.log\"", tail_cmd_buf_, sizeof(tail_cmd_buf_));
        }
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);

    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", error_.c_str());
    } else if (!info_.empty()) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", info_.c_str());
    }
}

void ViewerApp::draw_left_panel() {
    ImGui::TextUnformatted(tr(use_chinese_ui_, "Series", "字段序列"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", tr(use_chinese_ui_, "Filter by group or field", "按分组或字段过滤"), filter_buf_, sizeof(filter_buf_));

    if (ImGui::Button(tr(use_chinese_ui_, "Select All", "全选"))) {
        for (auto& kv : stats_.series_by_key) kv.second.visible = true;
        visibility_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr(use_chinese_ui_, "Clear All", "清空"))) {
        for (auto& kv : stats_.series_by_key) kv.second.visible = false;
        visibility_dirty_ = true;
    }
    ImGui::Separator();

    std::string filter = filter_buf_;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const std::string& group : group_order_) {
        const auto group_it = group_to_keys_.find(group);
        if (group_it == group_to_keys_.end()) continue;

        std::string group_label = group;
        if (use_chinese_ui_) {
            const std::string cn = group_cn_label(group);
            if (!cn.empty()) group_label += " (" + cn + ")";
        }
        std::string tree_id = group_label + "##" + group;
        if (ImGui::TreeNode(tree_id.c_str())) {
            for (const std::string& key : group_it->second) {
                auto it = stats_.series_by_key.find(key);
                if (it == stats_.series_by_key.end()) continue;
                Series& s = it->second;

                std::string lower_label = s.label;
                std::transform(lower_label.begin(), lower_label.end(), lower_label.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (!filter.empty() && lower_label.find(filter) == std::string::npos) continue;

                std::string display_label = s.label;
                if (use_chinese_ui_) {
                    const std::string cn = field_cn_label(s.field);
                    if (!cn.empty()) display_label += " (" + cn + ")";
                }
                display_label += "##" + s.label;
                if (ImGui::Checkbox(display_label.c_str(), &s.visible)) {
                    visibility_dirty_ = true;
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(tr(use_chinese_ui_, "Axis Mapping (Manual Override)", "坐标轴映射（手动覆盖）"), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted(tr(use_chinese_ui_, "Set per-series axis to avoid mixed units.", "为每个字段设置坐标轴，避免量纲混合。"));
        int shown = 0;
        for (const auto& key : stats_.ordered_keys) {
            auto it = stats_.series_by_key.find(key);
            if (it == stats_.series_by_key.end()) continue;
            const Series& s = it->second;
            if (!s.visible) continue;
            if (shown++ > 120) break;

            YAxisClass current = classify_series(s);
            int axis_idx = static_cast<int>(current);
            std::string combo_id = "##axis_" + s.label;
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::Combo(combo_id.c_str(), &axis_idx,
                             "Temperature\0Ratio/Latency\0Bytes\0Seq/Counter\0Frequency\0Time\0Memory\0Position/Misc\0")) {
                axis_override_by_key_[s.label] = static_cast<YAxisClass>(axis_idx);
                axis_map_dirty_ = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(s.label.c_str());
            if (axis_override_by_key_.find(s.label) != axis_override_by_key_.end()) {
                ImGui::SameLine();
                std::string clear_id = "Reset##" + s.label;
                if (ImGui::SmallButton(clear_id.c_str())) {
                    axis_override_by_key_.erase(s.label);
                    axis_map_dirty_ = true;
                }
            }
        }
    }
    save_visibility_state();
}

void ViewerApp::draw_plot_panel() {
    if (stats_.series_by_key.empty()) {
        ImGui::TextUnformatted("No data loaded. Please load a log file.");
        return;
    }

    const YAxisClass axis_order[] = {
        YAxisClass::Temperature,    YAxisClass::RatioLatency, YAxisClass::Bytes,      YAxisClass::SequenceCounter,
        YAxisClass::Frequency,      YAxisClass::Time,         YAxisClass::Memory,     YAxisClass::Position,
    };
    std::vector<YAxisClass> active_axes;
    active_axes.reserve(8);
    auto has_axis = [&](YAxisClass axis) {
        return std::find(active_axes.begin(), active_axes.end(), axis) != active_axes.end();
    };
    for (const auto& key : stats_.ordered_keys) {
        auto it = stats_.series_by_key.find(key);
        if (it == stats_.series_by_key.end()) continue;
        const Series& s = it->second;
        if (!s.visible || s.points.empty()) continue;
        const YAxisClass axis = classify_series(s);
        if (!has_axis(axis)) active_axes.push_back(axis);
    }

    std::sort(active_axes.begin(), active_axes.end(), [&](YAxisClass a, YAxisClass b) {
        auto rank = [&](YAxisClass x) {
            for (int i = 0; i < static_cast<int>(sizeof(axis_order) / sizeof(axis_order[0])); ++i) {
                if (axis_order[i] == x) return i;
            }
            return 999;
        };
        return rank(a) < rank(b);
    });

    int axis_count = static_cast<int>(active_axes.size());
    if (axis_count == 0) {
        ImGui::TextUnformatted("Select one or more series on the left.");
        return;
    }

    if (axis_count == 1) {
        YAxisClass axis = active_axes.front();
        if (axis == YAxisClass::Temperature) draw_axis_plot(axis, "Temperature", "temp (C)", ImVec2(-1, -1));
        if (axis == YAxisClass::RatioLatency) draw_axis_plot(axis, "Ratio/Latency", "ratio / latency", ImVec2(-1, -1));
        if (axis == YAxisClass::Bytes) draw_axis_plot(axis, "Bytes", "bytes", ImVec2(-1, -1));
        if (axis == YAxisClass::SequenceCounter) draw_axis_plot(axis, "Seq/Counter", "count", ImVec2(-1, -1));
        if (axis == YAxisClass::Frequency) draw_axis_plot(axis, "Frequency", "hz", ImVec2(-1, -1));
        if (axis == YAxisClass::Time) draw_axis_plot(axis, "Time", "seconds", ImVec2(-1, -1));
        if (axis == YAxisClass::Memory) draw_axis_plot(axis, "Memory", "memory", ImVec2(-1, -1));
        if (axis == YAxisClass::Position) draw_axis_plot(axis, "Position/Misc", "position", ImVec2(-1, -1));
    } else {
        ImGui::BeginChild("plot_scroll_list", ImVec2(-1, -1), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const float item_h = 260.0f;
        for (YAxisClass axis : active_axes) {
            if (axis == YAxisClass::Temperature) draw_axis_plot(axis, "Temperature", "temp (C)", ImVec2(-1, item_h));
            if (axis == YAxisClass::RatioLatency) draw_axis_plot(axis, "Ratio/Latency", "ratio / latency", ImVec2(-1, item_h));
            if (axis == YAxisClass::Bytes) draw_axis_plot(axis, "Bytes", "bytes", ImVec2(-1, item_h));
            if (axis == YAxisClass::SequenceCounter) draw_axis_plot(axis, "Seq/Counter", "count", ImVec2(-1, item_h));
            if (axis == YAxisClass::Frequency) draw_axis_plot(axis, "Frequency", "hz", ImVec2(-1, item_h));
            if (axis == YAxisClass::Time) draw_axis_plot(axis, "Time", "seconds", ImVec2(-1, item_h));
            if (axis == YAxisClass::Memory) draw_axis_plot(axis, "Memory", "memory", ImVec2(-1, item_h));
            if (axis == YAxisClass::Position) draw_axis_plot(axis, "Position/Misc", "position", ImVec2(-1, item_h));
        }
        ImGui::EndChild();
    }
    request_fit_next_frame_ = false;
}

void ViewerApp::draw_right_panel() {
    ImGui::TextUnformatted(tr(use_chinese_ui_, "Log Context", "日志上下文"));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderInt(tr(use_chinese_ui_, "Context lines", "上下文行数"), &context_radius_, 50, 100);

    if (!stats_.raw_lines.empty() && selected_log_line_ >= stats_.raw_lines.size()) {
        select_log_line(0);
    }

    if (!stats_.raw_lines.empty() && selected_log_line_ < stats_.raw_lines.size()) {
        const std::string ts = format_wallclock(selected_time_, true, true);
        ImGui::Text("%s: %s", tr(use_chinese_ui_, "Selected time", "选中时间"), ts.c_str());
        ImGui::Text("%s: %zu", tr(use_chinese_ui_, "Selected line", "选中行"), selected_log_line_ + 1);
    } else {
        ImGui::TextUnformatted(tr(use_chinese_ui_, "Click a point in any plot to locate source log line.",
                                  "点击图表中的点，可定位到原始日志行。"));
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(tr(use_chinese_ui_, "Shutdown Analysis", "Shutdown分析"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (stats_.shutdown_events.empty()) {
            ImGui::TextUnformatted(tr(use_chinese_ui_, "No shutdown events detected.", "未检测到shutdown事件。"));
        } else {
            int ev_idx = static_cast<int>(std::min(selected_shutdown_index_, stats_.shutdown_events.size() - 1));
            std::vector<std::string> labels;
            labels.reserve(stats_.shutdown_events.size());
            for (std::size_t i = 0; i < stats_.shutdown_events.size(); ++i) {
                const auto& ev = stats_.shutdown_events[i];
                labels.push_back("#" + std::to_string(i + 1) + " L" + std::to_string(ev.line_index + 1) + " " + ev.reason);
            }
            auto getter = [](void* data, int idx) -> const char* {
                auto* items = static_cast<std::vector<std::string>*>(data);
                if (idx < 0 || static_cast<std::size_t>(idx) >= items->size()) return "";
                return (*items)[static_cast<std::size_t>(idx)].c_str();
            };
            if (ImGui::Combo("##shutdown_select", &ev_idx, getter, &labels, static_cast<int>(labels.size()))) {
                selected_shutdown_index_ = static_cast<std::size_t>(ev_idx);
                select_log_line(stats_.shutdown_events[selected_shutdown_index_].line_index);
            }
            selected_shutdown_index_ = static_cast<std::size_t>(ev_idx);

            const auto& ev = stats_.shutdown_events[selected_shutdown_index_];
            const std::string ui_analysis = analysis_text_for_ui(ev.analysis, use_chinese_ui_);
            ImGui::TextWrapped("%s", ui_analysis.c_str());
            if (ImGui::Button(tr(use_chinese_ui_, "Jump to shutdown line", "跳转到shutdown行"))) {
                select_log_line(ev.line_index);
            }
            ImGui::Separator();
            ImGui::TextUnformatted(tr(use_chinese_ui_, "Dump excerpt", "Dump摘录"));
            ImGui::BeginChild("shutdown_dump", ImVec2(0, 180), true);
            for (const auto& s : ev.dump_lines) {
                ImGui::TextUnformatted(s.c_str());
            }
            ImGui::EndChild();
        }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(tr(use_chinese_ui_, "Timing/Jitter Analysis", "时序/抖动分析"), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (stats_.timing_issue_events.empty()) {
            ImGui::TextUnformatted(tr(use_chinese_ui_, "No timing issue event detected.",
                                      "未检测到 time too close / scheduling 类事件。"));
        } else {
            int idx = static_cast<int>(std::min(selected_timing_issue_index_, stats_.timing_issue_events.size() - 1));
            std::vector<std::string> labels;
            labels.reserve(stats_.timing_issue_events.size());
            for (std::size_t i = 0; i < stats_.timing_issue_events.size(); ++i) {
                const auto& ev = stats_.timing_issue_events[i];
                labels.push_back("#" + std::to_string(i + 1) + " L" + std::to_string(ev.line_index + 1) + " " + ev.type);
            }
            auto getter = [](void* data, int i) -> const char* {
                auto* items = static_cast<std::vector<std::string>*>(data);
                if (i < 0 || static_cast<std::size_t>(i) >= items->size()) return "";
                return (*items)[static_cast<std::size_t>(i)].c_str();
            };
            if (ImGui::Combo("##timing_issue_select", &idx, getter, &labels, static_cast<int>(labels.size()))) {
                selected_timing_issue_index_ = static_cast<std::size_t>(idx);
                const auto& ev = stats_.timing_issue_events[selected_timing_issue_index_];
                selected_time_ = ev.wall_t;
                select_log_line(ev.line_index);
            }
            selected_timing_issue_index_ = static_cast<std::size_t>(idx);

            const auto& ev = stats_.timing_issue_events[selected_timing_issue_index_];
            const std::string ui_analysis = analysis_text_for_ui(ev.analysis, use_chinese_ui_);
            ImGui::TextWrapped("%s", ui_analysis.c_str());
            if (ImGui::Button(tr(use_chinese_ui_, "Jump to timing issue line", "跳转到时序问题行"))) {
                selected_time_ = ev.wall_t;
                select_log_line(ev.line_index);
            }
            ImGui::SameLine();
            ImGui::Text("%s: %zu", tr(use_chinese_ui_, "line", "行"), ev.line_index + 1);
            ImGui::BeginChild("timing_issue_line", ImVec2(0, 86), true, ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(ev.line_text.c_str());
            ImGui::EndChild();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted(tr(use_chinese_ui_, "Selected Full Log Context", "选中完整日志上下文"));
    ImGui::BeginChild("log_context", ImVec2(0, 360), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (stats_.raw_lines.empty()) {
        ImGui::TextUnformatted(tr(use_chinese_ui_, "No raw log lines available.", "没有可显示的原始日志。"));
    } else if (selected_log_line_ >= stats_.raw_lines.size()) {
        ImGui::TextUnformatted(tr(use_chinese_ui_, "No line selected.", "尚未选择日志行。"));
    } else {
        const std::size_t begin = (selected_log_line_ > static_cast<std::size_t>(context_radius_))
                                      ? (selected_log_line_ - static_cast<std::size_t>(context_radius_))
                                      : 0;
        const std::size_t end =
            std::min(stats_.raw_lines.size(), selected_log_line_ + static_cast<std::size_t>(context_radius_) + 1);
        for (std::size_t i = begin; i < end; ++i) {
            if (i == selected_log_line_) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
                ImGui::Text("%8zu > %s", i + 1, stats_.raw_lines[i].c_str());
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("%8zu   %s", i + 1, stats_.raw_lines[i].c_str());
            }
        }
    }
    ImGui::EndChild();
}

void ViewerApp::draw_ui() {
    draw_top_bar();
    ImGui::Separator();

    if (ImGui::BeginTable("main_layout", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("SeriesPanel", ImGuiTableColumnFlags_WidthFixed, 320.0f);
        ImGui::TableSetupColumn("PlotPanel", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("RightPanel", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginChild("series_panel", ImVec2(0.0f, 0.0f), false);
        draw_left_panel();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("plot_panel", ImVec2(0.0f, 0.0f), false);
        draw_plot_panel();
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(2);
        ImGui::BeginChild("right_panel", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        draw_right_panel();
        ImGui::EndChild();

        ImGui::EndTable();
    }
}

bool ViewerApp::consume_screenshot_request(std::string& out_path) {
    if (!screenshot_requested_) return false;
    screenshot_requested_ = false;
    out_path = screenshot_path_;
    return true;
}
