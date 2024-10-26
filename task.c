#ifdef UNICODE
#define CreateWindowEx  CreateWindowExW
#endif
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <stdio.h>
#include <tlhelp32.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "gdi32.lib")    
#pragma comment(lib, "user32.lib")   

#define MAX_PROCESS_COUNT 5
#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 150
#define TIMER_ID 1
#define TIMER_INTERVAL 1000  // 1秒更新一次

// 进程信息结构体
typedef struct {
    WCHAR name[MAX_PATH];
    double cpuUsage;
} ProcessInfo;

// 全局变量
HINSTANCE g_hInstance;
HWND g_hWnd;
ProcessInfo g_processes[MAX_PROCESS_COUNT];
BOOL g_isDragging = FALSE;
POINT g_dragOffset;

// 函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void UpdateProcessList();
void DrawProcessInfo(HWND hwnd);

double GetProcessCpuUsage(HANDLE hProcess) {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD numProcessors = sysInfo.dwNumberOfProcessors;

    FILETIME now, creation_time, exit_time, kernel_time, user_time;
    static FILETIME last_kernel_time = {0}, last_user_time = {0};
    static ULONGLONG last_time = 0;
    
    GetSystemTimeAsFileTime(&now);
    if (!GetProcessTimes(hProcess, &creation_time, &exit_time, &kernel_time, &user_time)) {
        return 0.0;
    }
    
    ULONGLONG system_time = ((ULONGLONG)now.dwHighDateTime << 32) | now.dwLowDateTime;
    ULONGLONG k_time = ((ULONGLONG)kernel_time.dwHighDateTime << 32) | kernel_time.dwLowDateTime;
    ULONGLONG u_time = ((ULONGLONG)user_time.dwHighDateTime << 32) | user_time.dwLowDateTime;
    
    if (last_time == 0) {
        last_kernel_time = kernel_time;
        last_user_time = user_time;
        last_time = system_time;
        return 0.0;
    }
    
    ULONGLONG kernel_diff = k_time - (((ULONGLONG)last_kernel_time.dwHighDateTime << 32) | last_kernel_time.dwLowDateTime);
    ULONGLONG user_diff = u_time - (((ULONGLONG)last_user_time.dwHighDateTime << 32) | last_user_time.dwLowDateTime);
    ULONGLONG time_diff = system_time - last_time;
    
    if (time_diff == 0) return 0.0;
    
    // 考虑CPU核心数，并将结果限制在0-100之间
    double cpu_usage = ((kernel_diff + user_diff) * 100.0) / (time_diff * numProcessors);
    cpu_usage = (cpu_usage < 0.0) ? 0.0 : (cpu_usage > 100.0) ? 100.0 : cpu_usage;
    
    last_kernel_time = kernel_time;
    last_user_time = user_time;
    last_time = system_time;
    
    return cpu_usage;
}

// 更新进程列表的函数也需要调整阈值
void UpdateProcessList() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    
    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);
    
    ProcessInfo tempProcesses[1024];
    int processCount = 0;
    
    if (Process32FirstW(snapshot, &pe32)) {
        do {
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
            if (hProcess != NULL) {
                double cpuUsage = GetProcessCpuUsage(hProcess);
                if (cpuUsage > 0.1) {  // 只显示CPU使用率大于0.1%的进程
                    wcscpy_s(tempProcesses[processCount].name, MAX_PATH, pe32.szExeFile);
                    tempProcesses[processCount].cpuUsage = cpuUsage;
                    processCount++;
                }
                CloseHandle(hProcess);
            }
        } while (Process32NextW(snapshot, &pe32) && processCount < 1024);
    }
    
    CloseHandle(snapshot);
    
    // 按CPU使用率排序
    for (int i = 0; i < processCount - 1; i++) {
        for (int j = 0; j < processCount - i - 1; j++) {
            if (tempProcesses[j].cpuUsage < tempProcesses[j + 1].cpuUsage) {
                ProcessInfo temp = tempProcesses[j];
                tempProcesses[j] = tempProcesses[j + 1];
                tempProcesses[j + 1] = temp;
            }
        }
    }
    
    // 清空旧数据
    memset(g_processes, 0, sizeof(g_processes));
    
    // 更新全局进程信息（只取前5个）
    for (int i = 0; i < MAX_PROCESS_COUNT && i < processCount; i++) {
        g_processes[i] = tempProcesses[i];
    }
}

void DrawProcessInfo(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    
    // 创建白色背景并清除旧内容
    RECT rect;
    GetClientRect(hwnd, &rect);
    
    // 创建实心画刷并填充背景
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));  // 黑色背景
    FillRect(hdc, &rect, hBrush);
    DeleteObject(hBrush);
    
    // 设置文本属性
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));  // 白色文字
    
    // 显示进程信息
    WCHAR buffer[256];
    int y = 5;
    for (int i = 0; i < MAX_PROCESS_COUNT; i++) {
        if (wcslen(g_processes[i].name) > 0) {
            swprintf(buffer, sizeof(buffer), L"%s: %.1f%%", 
                    g_processes[i].name, g_processes[i].cpuUsage);
            TextOutW(hdc, 5, y, buffer, wcslen(buffer));
            y += 25;
        }
    }
    
    EndPaint(hwnd, &ps);
}



// 窗口过程
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            // 设置窗口透明
            SetLayeredWindowAttributes(hwnd, 0, 200, LWA_ALPHA);  // 透明度为200/255
            
            // 创建定时器
            SetTimer(hwnd, TIMER_ID, TIMER_INTERVAL, NULL);
            return 0;
        }
        
        case WM_PAINT:
            DrawProcessInfo(hwnd);
            return 0;
            
        case WM_TIMER:
            if (wParam == TIMER_ID) {
                UpdateProcessList();
                InvalidateRect(hwnd, NULL, TRUE);
            }
            return 0;
            
        case WM_LBUTTONDOWN: {
            g_isDragging = TRUE;
            SetCapture(hwnd);
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            RECT rect;
            ClientToScreen(hwnd, &pt);
            GetWindowRect(hwnd, &rect);
            g_dragOffset.x = pt.x - rect.left;
            g_dragOffset.y = pt.y - rect.top;
            return 0;
        }
        
        case WM_MOUSEMOVE: {
            if (g_isDragging) {
                POINT pt = {LOWORD(lParam), HIWORD(lParam)};
                ClientToScreen(hwnd, &pt);
                SetWindowPos(hwnd, NULL, 
                            pt.x - g_dragOffset.x, 
                            pt.y - g_dragOffset.y,
                            0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            return 0;
        }
        
        case WM_LBUTTONUP: {
            g_isDragging = FALSE;
            ReleaseCapture();
            return 0;
        }
        
        case WM_RBUTTONUP: {
            // 右键点击退出程序
            PostQuitMessage(0);
            return 0;
        }
        
        case WM_DESTROY:
            KillTimer(hwnd, TIMER_ID);
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;
    
    // 注册窗口类
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"CPUMonitor";
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassEx(&wc);
    
    // 创建窗口
    g_hWnd = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW,  // 扩展窗口样式
        L"CPUMonitor",                      // 窗口类名
        L"CPU Monitor",                     // 窗口标题
        WS_POPUP,                          // 窗口样式
        CW_USEDEFAULT, CW_USEDEFAULT,      // 初始位置
        WINDOW_WIDTH, WINDOW_HEIGHT,        // 窗口大小
        NULL, NULL, hInstance, NULL
    );
    
    if (g_hWnd == NULL) {
        return 0;
    }
    
    // 显示窗口
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    
    // 消息循环
    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    return 0;
}