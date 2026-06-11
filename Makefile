TARGET ?= native
APP_BASE := klipper_trace
APP := $(APP_BASE)$(if $(filter windows,$(TARGET)),.exe,)
BUILD_DIR := build/$(TARGET)
BIN_DIR := bin
THIRD_PARTY := third_party

IMGUI_DIR := $(THIRD_PARTY)/imgui
IMPLOT_DIR := $(THIRD_PARTY)/implot

SRC := \
	src/main.cpp \
	src/log_parser.cpp \
	src/viewer_app.cpp

IMGUI_CORE_SRC := \
	$(IMGUI_DIR)/imgui.cpp \
	$(IMGUI_DIR)/imgui_draw.cpp \
	$(IMGUI_DIR)/imgui_tables.cpp \
	$(IMGUI_DIR)/imgui_widgets.cpp \
	$(IMPLOT_DIR)/implot.cpp \
	$(IMPLOT_DIR)/implot_items.cpp

CXX ?= g++
WINDRES ?= x86_64-w64-mingw32-windres
CXXFLAGS := -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -MMD -MP
CPPFLAGS := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends -I$(IMPLOT_DIR) -Isrc
LDFLAGS :=

UNAME_S := $(shell uname -s)

ifeq ($(TARGET),windows)
	CXX := x86_64-w64-mingw32-g++
	LDFLAGS += -static -static-libgcc -static-libstdc++ -mwindows
	WIN_RC := resources/klipper_trace.rc
	WIN_RC_OBJ := $(BUILD_DIR)/resources/klipper_trace_res.o
	IMGUI_BACKEND_SRC := \
		$(IMGUI_DIR)/backends/imgui_impl_win32.cpp \
		$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
	LDLIBS := -lopengl32 -lgdi32 -ldwmapi -limm32
else
	IMGUI_BACKEND_SRC := \
		$(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
		$(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
endif

OBJ := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRC) $(IMGUI_CORE_SRC) $(IMGUI_BACKEND_SRC)) $(WIN_RC_OBJ)
DEP := $(OBJ:.o=.d)

ifeq ($(TARGET),native)
ifeq ($(UNAME_S),Linux)
	GLFW_CFLAGS := $(shell pkg-config --cflags glfw3 2>/dev/null)
	GLFW_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null)
	CPPFLAGS += $(GLFW_CFLAGS)
	LDLIBS := $(GLFW_LIBS) -lGL -ldl -lpthread
endif

ifeq ($(UNAME_S),Darwin)
	GLFW_CFLAGS := $(shell pkg-config --cflags glfw3 2>/dev/null)
	GLFW_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null)
	CPPFLAGS += $(GLFW_CFLAGS)
	LDLIBS := $(GLFW_LIBS) -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
endif
endif

.PHONY: all deps clean run

all: $(BIN_DIR)/$(APP)

deps: $(IMGUI_DIR)/imgui.h $(IMPLOT_DIR)/implot.h

$(IMGUI_DIR)/imgui.h:
	mkdir -p $(THIRD_PARTY)
	git clone --depth=1 https://github.com/ocornut/imgui.git $(IMGUI_DIR)

$(IMPLOT_DIR)/implot.h:
	mkdir -p $(THIRD_PARTY)
	git clone --depth=1 https://github.com/epezent/implot.git $(IMPLOT_DIR)

$(BIN_DIR)/$(APP): deps $(OBJ)
	mkdir -p $(BIN_DIR)
	$(CXX) $(LDFLAGS) $(OBJ) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/resources/klipper_trace_res.o: resources/klipper_trace.rc assets/klipper.ico
	mkdir -p $(dir $@)
	$(WINDRES) $< -O coff -o $@

run: all
	./$(BIN_DIR)/$(APP)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEP)
