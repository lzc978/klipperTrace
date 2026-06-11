#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <GL/gl.h>
#include <windows.h>

#include "imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#else
#include "imgui_impl_glfw.h"
#include <GLFW/glfw3.h>
#endif

#include "viewer_app.h"

namespace {

#ifdef _WIN32
constexpr int kAppIconId = 101;
#endif

#ifdef _WIN32
void apply_windows_bw_theme() {
    ImGui::StyleColorsLight();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = ImVec4(0.05f, 0.05f, 0.05f, 1.00f);
    c[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.97f, 0.97f, 0.97f, 1.00f);
    c[ImGuiCol_FrameBg] = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    c[ImGuiCol_Button] = ImVec4(0.86f, 0.86f, 0.86f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.78f, 0.78f, 0.78f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
    c[ImGuiCol_Header] = ImVec4(0.82f, 0.82f, 0.82f, 1.00f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.62f, 0.62f, 0.62f, 1.00f);
    c[ImGuiCol_Border] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
}
#else
void apply_windows_bw_theme() {
    ImGui::StyleColorsLight();
}
#endif

#ifdef _WIN32
bool save_framebuffer_ppm(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        const unsigned char* row = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3;
        ofs.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(width) * 3);
    }
    return true;
}
#else
bool save_framebuffer_ppm(const std::string& path, int width, int height) {
    if (width <= 0 || height <= 0) return false;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        const unsigned char* row = pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 3;
        ofs.write(reinterpret_cast<const char*>(row), static_cast<std::streamsize>(width) * 3);
    }
    return true;
}
#endif

#ifdef _WIN32
struct WGLWindowData {
    HDC hdc = nullptr;
};

static HGLRC g_hglrc = nullptr;
static WGLWindowData g_window_data;
static int g_width = 1400;
static int g_height = 860;

bool create_device_wgl(HWND hwnd, WGLWindowData* data) {
    HDC hdc = ::GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;

    const int pf = ::ChoosePixelFormat(hdc, &pfd);
    if (pf == 0) {
        ::ReleaseDC(hwnd, hdc);
        return false;
    }
    if (::SetPixelFormat(hdc, pf, &pfd) == FALSE) {
        ::ReleaseDC(hwnd, hdc);
        return false;
    }
    ::ReleaseDC(hwnd, hdc);

    data->hdc = ::GetDC(hwnd);
    if (!g_hglrc) {
        g_hglrc = wglCreateContext(data->hdc);
    }
    return g_hglrc != nullptr;
}

void cleanup_device_wgl(HWND hwnd, WGLWindowData* data) {
    wglMakeCurrent(nullptr, nullptr);
    if (data->hdc) {
        ::ReleaseDC(hwnd, data->hdc);
        data->hdc = nullptr;
    }
}

LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

    switch (msg) {
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) {
                g_width = LOWORD(lparam);
                g_height = HIWORD(lparam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wparam, lparam);
}
#else
void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    const char* glsl_version = "#version 130";
    HINSTANCE hinst = GetModuleHandle(nullptr);
    HICON app_icon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(kAppIconId), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    WNDCLASSEXW wc = {sizeof(wc), CS_OWNDC, wnd_proc, 0L, 0L, hinst, app_icon, nullptr, nullptr, nullptr, L"KlipperTrace", app_icon};
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"KlipperTrace", WS_OVERLAPPEDWINDOW, 100, 100, g_width,
                                g_height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!create_device_wgl(hwnd, &g_window_data)) {
        cleanup_device_wgl(hwnd, &g_window_data);
        ::DestroyWindow(hwnd);
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }
    wglMakeCurrent(g_window_data.hdc, g_hglrc);
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);
#else
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return 1;
    }

#if __APPLE__
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1400, 860, "KlipperTrace", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
#endif

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
#ifdef _WIN32
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());
#endif
    apply_windows_bw_theme();

#ifdef _WIN32
    ImGui_ImplWin32_InitForOpenGL(hwnd);
#else
    ImGui_ImplGlfw_InitForOpenGL(window, true);
#endif
    ImGui_ImplOpenGL3_Init(glsl_version);

    ViewerApp app;
    if (argc > 1) {
        app.set_initial_log_path(argv[1]);
    }

#ifdef _WIN32
    bool done = false;
    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(g_width), static_cast<float>(g_height)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("MainWindow", nullptr, flags);
        app.draw_ui();
        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, g_width, g_height);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::string screenshot_path;
        if (app.consume_screenshot_request(screenshot_path)) {
            save_framebuffer_ppm(screenshot_path, g_width, g_height);
        }
        ::SwapBuffers(g_window_data.hdc);
    }
#else
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(display_w), static_cast<float>(display_h)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("MainWindow", nullptr, flags);
        app.draw_ui();
        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        std::string screenshot_path;
        if (app.consume_screenshot_request(screenshot_path)) {
            save_framebuffer_ppm(screenshot_path, display_w, display_h);
        }
        glfwSwapBuffers(window);
    }
#endif

    ImGui_ImplOpenGL3_Shutdown();
#ifdef _WIN32
    ImGui_ImplWin32_Shutdown();
#else
    ImGui_ImplGlfw_Shutdown();
#endif
    ImPlot::DestroyContext();
    ImGui::DestroyContext();

#ifdef _WIN32
    cleanup_device_wgl(hwnd, &g_window_data);
    if (g_hglrc) {
        wglDeleteContext(g_hglrc);
        g_hglrc = nullptr;
    }
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
#else
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
    return 0;
}
