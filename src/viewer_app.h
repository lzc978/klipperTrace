#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "log_parser.h"
#include "model.h"

struct ImVec2;

enum class YAxisClass {
    Temperature = 0,
    RatioLatency = 1,
    Bytes = 2,
    SequenceCounter = 3,
    Frequency = 4,
    Time = 5,
    Memory = 6,
    Position = 7,
};

class ViewerApp {
public:
    ViewerApp();
    ~ViewerApp();
    void set_initial_log_path(std::string path);
    void draw_ui();
    bool consume_screenshot_request(std::string& out_path);

private:
    enum class SourceMode {
        File = 0,
        TailFile = 1,
        TailCommand = 2,
    };

    struct TailState {
        std::atomic<bool> running{false};
        std::atomic<bool> stop{false};
        std::thread worker;
        std::mutex mutex;
        std::deque<std::string> pending_lines;
    };

    void draw_top_bar();
    void draw_left_panel();
    void draw_plot_panel();
    void draw_right_panel();
    void draw_axis_plot(YAxisClass axis, const char* title, const char* y_label, const ImVec2& size);
    void load_log();
    void reset_visibility_defaults();
    void rebuild_groups();
    void apply_pending_tail_lines();
    void start_tail();
    void stop_tail();
    void tail_file_worker(std::string path);
    void tail_command_worker(std::string command);
    void save_visibility_state();
    void load_visibility_state();
    void export_csv();
    bool browse_log_file();
    static const char* axis_name(YAxisClass axis);
    YAxisClass classify_series(const Series& s) const;
    void setup_axis_limits_from_visible();
    void draw_hover_tooltip_for_axis(YAxisClass axis);
    bool capture_click_selection_on_axis(YAxisClass axis);
    bool draw_shutdown_markers();
    std::size_t find_nearest_raw_line_index_by_time(double t) const;
    std::size_t find_nearest_stats_line_index(double t) const;
    void select_log_line(std::size_t idx);

    LogParser parser_;
    ParsedStats stats_;
    std::string log_path_;
    std::string tail_command_;
    std::string error_;
    std::string info_;
    std::string state_path_ = "viewer_state.ini";
    std::string csv_path_ = "export_stats.csv";
    std::string screenshot_path_;
    bool screenshot_requested_ = false;
    bool visibility_dirty_ = false;
    bool axis_map_dirty_ = false;
    bool auto_fit_y_axes_ = true;
    bool clamp_temp_axis_ = false;
    bool clamp_ratio_axis_ = false;
    bool x_axis_show_date_ = false;
    bool use_chinese_ui_ = true;
    bool smooth_enabled_ = false;
    int smooth_window_ = 5;
    char log_path_buf_[1024];
    char tail_cmd_buf_[1024];
    char csv_path_buf_[1024];
    char highlight_field_buf_[128] = "extruder.temp";
    char filter_buf_[128];
    bool auto_fit_on_load_ = true;
    bool request_fit_next_frame_ = false;
    bool realtime_enabled_ = false;
    SourceMode source_mode_ = SourceMode::File;
    std::size_t last_file_pos_ = 0;
    TailState tail_;
    std::unordered_map<std::string, std::vector<std::string>> group_to_keys_;
    std::vector<std::string> group_order_;
    std::unordered_map<std::string, YAxisClass> axis_override_by_key_;
    std::size_t selected_log_line_ = static_cast<std::size_t>(-1);
    double selected_time_ = 0.0;
    std::size_t selected_shutdown_index_ = 0;
    std::size_t selected_timing_issue_index_ = 0;
    int context_radius_ = 80;
};
