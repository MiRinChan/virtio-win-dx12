// SPDX-License-Identifier: MIT
// Measures mode selection, vblank timing, and paced presentation in the interactive desktop.
// displayprobe [--rate 60|120|144] [--seconds N] [--keep]
// The original mode is restored unless --keep is set.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static LRESULT CALLBACK window_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcW(h, m, w, l);
}

struct ModeRestore {
    DEVMODEW mode = {};
    bool changed = false;
    bool keep = false;
    ~ModeRestore()
    {
        if (changed && !keep)
            std::printf("restore=%ld\n", ChangeDisplaySettingsExW(nullptr, &mode, nullptr, 0, nullptr));
    }
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    unsigned rate = 0;
    double seconds = 5;
    ModeRestore restore;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--rate") && i + 1 < argc)
            rate = std::strtoul(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--keep"))
            restore.keep = true;
        else
            return 2;
    }
    if ((rate && rate != 60 && rate != 120 && rate != 144) || seconds < 1 || seconds > 30)
        return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    restore.mode.dmSize = sizeof(DEVMODEW);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &restore.mode))
        return 1;
    std::printf("original=%lux%lu@%lu\n", restore.mode.dmPelsWidth, restore.mode.dmPelsHeight,
                restore.mode.dmDisplayFrequency);
    bool found = false;
    DEVMODEW selected = {};
    for (DWORD i = 0;; ++i) {
        DEVMODEW mode = {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(nullptr, i, &mode))
            break;
        if (mode.dmPelsWidth != restore.mode.dmPelsWidth || mode.dmPelsHeight != restore.mode.dmPelsHeight ||
            mode.dmBitsPerPel != restore.mode.dmBitsPerPel)
            continue;
        std::printf("mode=%lux%lu@%lu\n", mode.dmPelsWidth, mode.dmPelsHeight, mode.dmDisplayFrequency);
        if (mode.dmDisplayFrequency == rate) {
            selected = mode;
            found = true;
        }
    }
    if (!rate)
        return 0;
    if (!found) {
        std::printf("FAIL requested mode is absent\n");
        return 1;
    }
    LONG status = ChangeDisplaySettingsExW(nullptr, &selected, nullptr, CDS_TEST, nullptr);
    std::printf("mode_test=%ld\n", status);
    if (status != DISP_CHANGE_SUCCESSFUL)
        return 1;
    status = ChangeDisplaySettingsExW(nullptr, &selected, nullptr, 0, nullptr);
    std::printf("mode_apply=%ld\n", status);
    if (status != DISP_CHANGE_SUCCESSFUL)
        return 1;
    restore.changed = true;
    Sleep(3000);
    DEVMODEW current = {};
    current.dmSize = sizeof(current);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current);
    std::printf("selected=%lux%lu@%lu\n", current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency);
    if (current.dmDisplayFrequency != rate)
        return 1;

    IDXGIFactory* factory = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11Texture2D* buffer = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    HWND window = nullptr;
    HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
    if (SUCCEEDED(hr)) hr = factory->EnumAdapters(0, &adapter);
    if (SUCCEEDED(hr)) hr = adapter->EnumOutputs(0, &output);
    if (SUCCEEDED(hr)) hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                             D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) {
        std::printf("FAIL device=0x%08lx\n", hr);
        if (output) output->Release();
        if (adapter) adapter->Release();
        if (factory) factory->Release();
        return 1;
    }
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    // WaitForVBlank measures kernel notifications, independently of renderer throughput.
    std::vector<double> intervals;
    LARGE_INTEGER previous, start, now;
    for (unsigned i = 0; i < rate / 2; ++i)
        output->WaitForVBlank();
    hr = output->WaitForVBlank();
    QueryPerformanceCounter(&previous);
    start = previous;
    for (unsigned i = 0; SUCCEEDED(hr) && i < rate * 2; ++i) {
        hr = output->WaitForVBlank();
        QueryPerformanceCounter(&now);
        intervals.push_back(1000.0 * (now.QuadPart - previous.QuadPart) / frequency.QuadPart);
        previous = now;
    }
    const double vblank_hz = intervals.empty() ? 0 :
        intervals.size() * static_cast<double>(frequency.QuadPart) / (previous.QuadPart - start.QuadPart);
    std::sort(intervals.begin(), intervals.end());
    std::printf("vblank_hr=0x%08lx samples=%zu measured_hz=%.3f p50_ms=%.3f p95_ms=%.3f\n", hr,
                intervals.size(), vblank_hz, intervals.empty() ? 0 : intervals[intervals.size() / 2],
                intervals.empty() ? 0 : intervals[(intervals.size() - 1) * 95 / 100]);
    bool ok = SUCCEEDED(hr) && std::abs(vblank_hz - rate) / rate < 0.05;
    DWM_TIMING_INFO timing = {};
    timing.cbSize = sizeof(timing);
    hr = DwmGetCompositionTimingInfo(nullptr, &timing);
    std::printf("dwm_timing_hr=0x%08lx refresh=%u/%u qpc_period=%llu\n", hr,
                timing.rateRefresh.uiNumerator, timing.rateRefresh.uiDenominator, timing.qpcRefreshPeriod);
    ok = ok && SUCCEEDED(hr) && timing.rateRefresh.uiDenominator &&
        std::abs(static_cast<double>(timing.rateRefresh.uiNumerator) / timing.rateRefresh.uiDenominator - rate) < 1;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"vgd-displayprobe";
    RegisterClassW(&wc);
    window = CreateWindowW(wc.lpszClassName, L"vgd refresh probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                            160, 160, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferDesc.Width = 640;
    desc.BufferDesc.Height = 480;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = factory->CreateSwapChain(device, &desc, &swap);
    if (SUCCEEDED(hr)) hr = swap->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&buffer));
    if (SUCCEEDED(hr)) hr = device->CreateRenderTargetView(buffer, nullptr, &target);
    const HRESULT swap_hr = hr;
    unsigned frames = 0;
    QueryPerformanceCounter(&start);
    now = start;
    FILETIME present_start;
    GetSystemTimePreciseAsFileTime(&present_start);
    std::printf("present_start_filetime=%llu\n",
                (static_cast<unsigned long long>(present_start.dwHighDateTime) << 32) | present_start.dwLowDateTime);
    while (SUCCEEDED(hr) && static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart < seconds) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const float color[] = {static_cast<float>((frames % 120) / 120.0), 0.3f, 0.6f, 1};
        context->ClearRenderTargetView(target, color);
        hr = swap->Present(1, 0);
        if (SUCCEEDED(hr)) ++frames;
        QueryPerformanceCounter(&now);
    }
    const double elapsed = static_cast<double>(now.QuadPart - start.QuadPart) / frequency.QuadPart;
    FILETIME present_end;
    GetSystemTimePreciseAsFileTime(&present_end);
    std::printf("present_end_filetime=%llu\n",
                (static_cast<unsigned long long>(present_end.dwHighDateTime) << 32) | present_end.dwLowDateTime);
    DWM_TIMING_INFO end = {};
    end.cbSize = sizeof(end);
    const HRESULT end_hr = DwmGetCompositionTimingInfo(nullptr, &end);
    std::printf("swap_hr=0x%08lx present_hr=0x%08lx frames=%u seconds=%.3f present_fps=%.3f "
                "dwm_frames=%llu dwm_refreshes=%llu\n", swap_hr, hr, frames, elapsed,
                elapsed ? frames / elapsed : 0,
                SUCCEEDED(end_hr) ? end.cFrame - timing.cFrame : 0,
                SUCCEEDED(end_hr) ? end.cRefresh - timing.cRefresh : 0);
    ok = ok && SUCCEEDED(hr) && frames > 0;
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &current);
    std::printf("end_mode=%lux%lu@%lu\n", current.dmPelsWidth, current.dmPelsHeight, current.dmDisplayFrequency);
    ok = ok && current.dmDisplayFrequency == rate;
    if (target) target->Release();
    if (buffer) buffer->Release();
    if (swap) swap->Release();
    DestroyWindow(window);
    context->Release();
    device->Release();
    output->Release();
    adapter->Release();
    factory->Release();
    std::printf("%s mode and vblank timing\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
