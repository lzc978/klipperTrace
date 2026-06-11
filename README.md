# KlipperTrace (C++/ImGui)

一个轻量、跨平台（GNU GCC 工具链）的 Klipper 日志统计可视化工具。

## 功能

- 自适应解析 `Stats ...` 日志，不依赖固定字段。
- 支持按 `group`（如 `mcu:`, `nozzle_mcu:`）自动分类字段。
- GUI 可选择显示/隐藏任意字段。
- 时间轴（X 轴）支持缩放、平移（ImPlot 原生交互）。
- 性能友好：增量解析、紧凑数据结构、按需绘图。

## 构建依赖

- GNU Make
- g++
- git（用于自动拉取第三方库）
- Python3（可选，仅在某些系统用于 `pkg-config` 旁路）
- OpenGL 开发库
- GLFW3 开发库
- `pkg-config`

Ubuntu/WSL 示例：

```bash
sudo apt update
sudo apt install -y build-essential git pkg-config libgl1-mesa-dev libglfw3-dev
```

## 构建与运行

```bash
make
./bin/klipper_trace /path/to/klipper.log
```

如果不传日志路径，也可在 GUI 里手动输入并加载。

## 交叉编译 Windows 可执行文件（在 Linux/WSL）

```bash
sudo apt install -y g++-mingw-w64-x86-64-posix
make TARGET=windows
```

输出文件：

- `bin/klipper_trace.exe`

## 交互

- 左侧面板：字段过滤 + 分组树 + 勾选显隐。
- 中间图表：滚轮缩放，拖拽平移，框选放大（ImPlot 默认行为）。
- 顶部：重新加载日志、重置视图。

## 字段解析策略

- 只解析包含 `Stats <time>:` 的行。
- 识别 `group:` 切换（例如 `mcu:`、`extruder:`）。
- 解析所有 `key=value` 数值字段。
- 新字段自动进入对应 group，无需改代码。
