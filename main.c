#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h> // For PathCombine

#define SINGLE_INSTANCE_MUTEX_NAME "ECHWorkerClient_Mutex_Unique_ID"
#define IDI_APP_ICON 101 // 假设您的资源文件中有此ID

typedef BOOL (WINAPI *SetProcessDPIAwareFunc)(void);

#define APP_VERSION "1.5"
#define APP_TITLE "ECH workers 客户端 v" APP_VERSION

#define MAX_URL_LEN 8192
#define MAX_SMALL_LEN 2048
#define MAX_CMD_LEN 32768

#define WM_TRAYICON (WM_USER + 1)
#define WM_APPEND_LOG (WM_USER + 2) 

#define ID_TRAY_ICON 9001
#define ID_TRAY_OPEN 9002
#define ID_TRAY_EXIT 9003

HFONT hFontUI = NULL;    
HFONT hFontLog = NULL;   
HBRUSH hBrushLog = NULL;

int g_dpi = 96;
int g_scale = 100;
int g_totalNodeCount = 0;

int Scale(int x) {
    return (x * g_scale) / 100;
}

// 控件ID定义
#define ID_CONFIG_NAME_EDIT 1000
#define ID_SERVER_EDIT      1001
#define ID_LISTEN_EDIT      1002
#define ID_TOKEN_EDIT       1003
#define ID_IP_EDIT          1004
#define ID_DNS_EDIT         1005
#define ID_ECH_EDIT         1006
#define ID_START_BTN        1010
#define ID_STOP_BTN         1011
#define ID_CLEAR_LOG_BTN    1012
#define ID_LOG_EDIT         1013
#define ID_SAVE_CONFIG_BTN  1014
#define ID_LOAD_CONFIG_BTN  1015
// 订阅相关新ID
#define ID_SUBSCRIBE_URL_EDIT 1016
#define ID_FETCH_SUB_BTN    1017
#define ID_NODE_LIST        1018
#define ID_ADD_SUB_BTN      1019
#define ID_DEL_SUB_BTN      1020
#define ID_SUB_LIST         1021
#define ID_CLEAR_NODE_BTN   1022

HWND hMainWindow;
HWND hConfigNameEdit, hServerEdit, hListenEdit, hTokenEdit, hIpEdit, hDnsEdit, hEchEdit;
HWND hStartBtn, hStopBtn, hLogEdit, hSaveConfigBtn, hLoadConfigBtn;
HWND hSubscribeUrlEdit, hFetchSubBtn, hNodeList;
HWND hAddSubBtn, hDelSubBtn, hSubList;
HWND hGroupSub, hGroup1, hGroup2; // 新增群组框句柄

PROCESS_INFORMATION processInfo;
HANDLE hLogPipe = NULL;
HANDLE hLogThread = NULL;
BOOL isProcessRunning = FALSE;
NOTIFYICONDATA nid;

typedef struct {
    char configName[MAX_SMALL_LEN];
    char dns[MAX_SMALL_LEN];     
    char ech[MAX_SMALL_LEN];     
    char server[MAX_URL_LEN];    
    char ip[MAX_SMALL_LEN];      
    char listen[MAX_SMALL_LEN];  
    char token[MAX_URL_LEN];     
} Config;

Config currentConfig = {
    "默认配置", "dns.alidns.com/dns-query", "cloudflare-ech.com", "example.com:443", "", "127.0.0.1:30000", ""
};

// --- Function Prototypes ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
void CreateLabelAndEdit(HWND parent, const char* labelText, int x, int y, int w, int h, int editId, HWND* outEdit, BOOL numberOnly);
void StartProcess();
void StopProcess();
void AppendLog(const char* text);
void AppendLogAsync(const char* text);
DWORD WINAPI LogReaderThread(LPVOID lpParam);
void SaveConfig();
void LoadConfig();
void SaveConfigToFile();
void LoadConfigFromFile();
void GetControlValues();
void SetControlValues();
void InitTrayIcon(HWND hwnd);
void ShowTrayIcon();
void RemoveTrayIcon();
void FetchAllSubscriptions();
void ProcessSingleSubscription(const char* url);
void ParseSubscriptionData(const char* data);
void AddSubscription();
void DelSubscription();
void SaveSubscriptionList();
void LoadSubscriptionList();
void SaveNodeConfig(int nodeIndex);
void LoadNodeList();
void SaveNodeList();
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart);
void ClearNodeList();
char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
BOOL IsUTF8File(const char* fileName);
char* base64_decode(const char* input, size_t* out_len);
BOOL is_base64_encoded(const char* data);

// --- WinMain ---

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    
    HANDLE hMutex = CreateMutex(NULL, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (hMutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindow("ECHWorkerClient", NULL); 
        if (hExistingWnd) {
            // 尝试恢复窗口
            PostMessage(hExistingWnd, WM_TRAYICON, ID_TRAY_ICON, WM_LBUTTONUP);
        }
        CloseHandle(hMutex);
        return 0; 
    }
    
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (hUser32) {
        SetProcessDPIAwareFunc setDPIAware = (SetProcessDPIAwareFunc)(void*)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (setDPIAware) setDPIAware();
        FreeLibrary(hUser32);
    }
    
    HDC hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    g_scale = (g_dpi * 100) / 96;
    ReleaseDC(NULL, hdc);
    
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icex);

    hFontUI = CreateFont(Scale(19), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Microsoft YaHei UI");

    hFontLog = CreateFont(Scale(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");

    hBrushLog = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "ECHWorkerClient";
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); 
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APP_ICON));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClass(&wc)) return 1;

    // 修改：设置初始窗口大小为 800x700，并添加可调节大小样式
    int winWidth = Scale(800);
    int winHeight = Scale(700); 
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // WS_OVERLAPPEDWINDOW 包含 WS_CAPTION, WS_SYSMENU, WS_MINIMIZEBOX, WS_MAXIMIZEBOX, 和 WS_THICKFRAME（可调节大小）
    // WS_CLIPCHILDREN 避免子控件重绘闪烁
    DWORD winStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN; 

    hMainWindow = CreateWindowEx(
        0, "ECHWorkerClient", APP_TITLE, 
        winStyle,
        (screenW - winWidth) / 2, (screenH - winHeight) / 2, 
        winWidth, winHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hMainWindow) return 1;

    InitTrayIcon(hMainWindow);

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB) {
            IsDialogMessage(hMainWindow, &msg);
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    
    CloseHandle(hMutex); 
    return (int)msg.wParam;
}

// --- WindowProc ---

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            LoadConfig();
            LoadSubscriptionList();
            SetControlValues();
            LoadNodeList();
            break;

        case WM_SIZE:
        {
            // --- 动态布局逻辑 (根据 800x700 比例和 DPI 缩放) ---
            
            int margin = Scale(20);
            int lineHeight = Scale(30);
            int lineGap = Scale(10);
            int editH = Scale(26);
            int btnW = Scale(120);
            int btnH = Scale(38);
            
            int newWidth = LOWORD(lParam);
            int newHeight = HIWORD(lParam);
            int groupW = newWidth - (margin * 2);

            // --- 1. 订阅管理区域 (Top Section) ---
            int curY = margin;
            int groupSubH = Scale(280); 
            int innerY = curY + Scale(25);
            
            // Group Box (hGroupSub) - Adjust width
            MoveWindow(hGroupSub, margin, curY, groupW, groupSubH, TRUE);

            // Subscription Link Edit (hSubscribeUrlEdit) - Adjust width
            MoveWindow(hSubscribeUrlEdit, 
                margin + Scale(100), innerY, groupW - Scale(180), editH, TRUE); 
                
            // Add Button (hAddSubBtn) - Adjust horizontal position
            MoveWindow(hAddSubBtn,
                margin + groupW - Scale(90), innerY, Scale(80), editH, TRUE);

            innerY += lineHeight + lineGap - Scale(5);

            // Sub List (hSubList) - Adjust width
            MoveWindow(hSubList, 
                margin + Scale(15), innerY, groupW - Scale(30), Scale(60), TRUE); 

            innerY += Scale(60) + Scale(5);

            // Del/Fetch/ClearNode Buttons
            // ID_DEL_SUB_BTN: 
            MoveWindow(GetDlgItem(hwnd, ID_DEL_SUB_BTN),
                margin + Scale(15), innerY, Scale(120), Scale(30), TRUE);

            // ID_FETCH_SUB_BTN: 
            MoveWindow(GetDlgItem(hwnd, ID_FETCH_SUB_BTN),
                margin + Scale(150), innerY, Scale(120), Scale(30), TRUE);
            
            // ID_CLEAR_NODE_BTN: 
            MoveWindow(GetDlgItem(hwnd, ID_CLEAR_NODE_BTN),
                margin + Scale(285), innerY, Scale(120), Scale(30), TRUE);
            
            innerY += Scale(35);

            // Node List Label
            // GetDlgItem(hwnd, ID_NODE_LIST - 1) is the label for Node List
            // Note: The label is a static control created before the listbox.

            // Config Name Label Start Y (Unscaled value 315)
            int configNameLabelY = Scale(315); 
            
            // 节点列表高度: 到下一个固定元素 (配置名称标签) 的顶部距离, 减去间隙
            int nodeHeight = configNameLabelY - (innerY + Scale(25)) - Scale(10); 
            if (nodeHeight < Scale(30)) nodeHeight = Scale(30);
            
            // Node List (hNodeList) - Adjust height and width
            MoveWindow(hNodeList, 
                margin + Scale(15), innerY + Scale(25), groupW - Scale(30), nodeHeight, TRUE); 
            
            // --- 2. 配置区域 (Middle Section) ---
            curY = configNameLabelY; 

            // Config Name Edit (hConfigNameEdit) - Adjust width
            MoveWindow(hConfigNameEdit,
                margin + Scale(90), curY, groupW - Scale(90) - margin, editH, TRUE); 

            curY += lineHeight + Scale(5);
            
            // Core Config Group (hGroup1) - Adjust width
            int group1H = Scale(80);
            MoveWindow(hGroup1, margin, curY, groupW, group1H, TRUE);
            
            int labelW = Scale(150);
            int editW_Config = groupW - labelW - Scale(15); 
            int innerY_Config = curY + Scale(25);
            
            // Server Edit (hServerEdit)
            MoveWindow(hServerEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);
            innerY_Config += lineHeight + Scale(10);
            
            // Listen Edit (hListenEdit)
            MoveWindow(hListenEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);

            curY += group1H + Scale(15);

            // Advanced Config Group (hGroup2) - Adjust width
            int group2H = Scale(155);
            MoveWindow(hGroup2, margin, curY, groupW, group2H, TRUE);
            innerY_Config = curY + Scale(25);

            // Token Edit (hTokenEdit)
            MoveWindow(hTokenEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);
            innerY_Config += lineHeight + lineGap;
            
            // IP Edit (hIpEdit)
            MoveWindow(hIpEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);
            innerY_Config += lineHeight + lineGap;

            // ECH Edit (hEchEdit)
            MoveWindow(hEchEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);
            innerY_Config += lineHeight + lineGap;

            // DNS Edit (hDnsEdit)
            MoveWindow(hDnsEdit, margin + labelW, innerY_Config, editW_Config, editH, TRUE);
                
            // Find the Y position for the button row, relative to the window bottom, 
            // or relative to the start of the log area.
            int logLabelY = newHeight - Scale(170); // Log Label Y, fixed relative to bottom
            int buttonRowY = logLabelY - Scale(38) - Scale(15); // Button Row Y
            
            // --- 3. 按钮行 ---
            
            // Start Button (hStartBtn)
            MoveWindow(hStartBtn, margin, buttonRowY, btnW, btnH, TRUE);
            // Stop Button (hStopBtn)
            MoveWindow(hStopBtn, margin + btnW + btnW/6, buttonRowY, btnW, btnH, TRUE);
            // Save Button (hSaveConfigBtn)
            MoveWindow(hSaveConfigBtn, margin + (btnW + btnW/6) * 2, buttonRowY, btnW, btnH, TRUE);
            // Load Button (hLoadConfigBtn)
            MoveWindow(hLoadConfigBtn, margin + (btnW + btnW/6) * 3, buttonRowY, btnW, btnH, TRUE);
            
            // Clear Log Button (ID_CLEAR_LOG_BTN) - Reposition right-aligned
            MoveWindow(GetDlgItem(hwnd, ID_CLEAR_LOG_BTN),
                newWidth - margin - btnW, buttonRowY, btnW, btnH, TRUE); 
                
            // --- 4. 日志区域 (Bottom Section) ---
            
            // Log Label
            MoveWindow(GetDlgItem(hwnd, ID_LOG_EDIT - 1), // Log Label is before hLogEdit
                margin, logLabelY, Scale(100), Scale(20), TRUE); 

            // Log Edit Y: Log Label Y + Label Height + Gap
            int logEditY = logLabelY + Scale(25); 
            // 日志框高度: 从 Log Edit Y 到窗口底部的距离, 减去底部边距
            int logHeight = newHeight - logEditY - margin;
            if (logHeight < Scale(30)) logHeight = Scale(30); 

            MoveWindow(hLogEdit, 
                margin, logEditY, 
                groupW, logHeight, TRUE);
        }
        break;

        case WM_SYSCOMMAND:
            if (wParam == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE);
                ShowTrayIcon();
                return 0;
            }
            break;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
                RemoveTrayIcon();
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, ID_TRAY_OPEN, "打开主窗口");
                InsertMenu(hMenu, 1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "退出");
                SetForegroundWindow(hwnd);
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                PostMessage(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);
            }
            break;

        case WM_APPEND_LOG: {
            // 异步日志追加
            int len = GetWindowTextLength(hLogEdit);
            SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
            SendMessage(hLogEdit, EM_REPLACESEL, FALSE, lParam);
            free((void*)lParam);
            break;
        }

        case WM_CTLCOLORSTATIC: {
            // 日志框颜色
            if ((HWND)lParam == hLogEdit) {
                SetBkColor((HDC)wParam, RGB(255, 255, 255));
                return (LRESULT)hBrushLog;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_START_BTN:
                    StartProcess();
                    break;
                case ID_STOP_BTN:
                    StopProcess();
                    break;
                case ID_CLEAR_LOG_BTN:
                    SetWindowText(hLogEdit, "");
                    break;
                case ID_SAVE_CONFIG_BTN:
                    SaveConfigToFile();
                    break;
                case ID_LOAD_CONFIG_BTN:
                    LoadConfigFromFile();
                    SetControlValues();
                    break;
                case ID_ADD_SUB_BTN:
                    AddSubscription();
                    break;
                case ID_DEL_SUB_BTN:
                    DelSubscription();
                    break;
                case ID_FETCH_SUB_BTN:
                    FetchAllSubscriptions();
                    break;
                case ID_CLEAR_NODE_BTN:
                    ClearNodeList();
                    break;
                case ID_NODE_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int index = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (index != LB_ERR) {
                            SaveNodeConfig(index);
                        }
                    } else if (HIWORD(wParam) == LBN_DBLCLK) {
                        int index = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (index != LB_ERR) {
                            LoadNodeConfigByIndex(index, TRUE);
                            // 自动启动
                            if (!isProcessRunning) {
                                StartProcess();
                            } else {
                                AppendLog("[提示] 代理已运行，配置已更新。\r\n");
                            }
                        }
                    }
                    break;
                case ID_TRAY_OPEN:
                    PostMessage(hwnd, WM_TRAYICON, ID_TRAY_ICON, WM_LBUTTONUP);
                    break;
                case ID_TRAY_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            ShowTrayIcon();
            return 0; // 阻止默认关闭，最小化到托盘

        case WM_DESTROY:
            StopProcess();
            RemoveTrayIcon();
            DeleteObject(hFontUI);
            DeleteObject(hFontLog);
            DeleteObject(hBrushLog);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- Control Creation and Helpers ---

void CreateLabelAndEdit(HWND parent, const char* labelText, int x, int y, int w, int h, int editId, HWND* outEdit, BOOL numberOnly) {
    // Label width is fixed at Scale(130) for better alignment
    int labelW = Scale(130);
    int labelX = x;
    int editX = x + labelW - Scale(15); 
    int editW = w - labelW + Scale(15); 

    HWND hLabel = CreateWindow("STATIC", labelText, WS_VISIBLE | WS_CHILD | SS_RIGHT, 
        labelX, y + Scale(3), labelW, Scale(20), parent, NULL, NULL, NULL);
    SendMessage(hLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    *outEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | (numberOnly ? ES_NUMBER : 0), 
        editX, y, editW, h, parent, (HMENU)editId, NULL, NULL);
    SendMessage(*outEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(*outEdit, EM_SETLIMITTEXT, MAX_URL_LEN, 0);
}


void CreateControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int winW = rect.right;
    int margin = Scale(20);
    int groupW = winW - (margin * 2);
    int lineHeight = Scale(30);
    int lineGap = Scale(10);
    int editH = Scale(26);
    int curY = margin;

    // ----- 订阅管理区域 -----
    int groupSubH = Scale(280); 
    hGroupSub = CreateWindow("BUTTON", "订阅管理", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, groupSubH, hwnd, NULL, NULL, NULL);
    SendMessage(hGroupSub, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(25);
    
    // 1. 输入框行：Label + Edit + 添加按钮
    HWND hSubLabel = CreateWindow("STATIC", "订阅链接:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hSubLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hSubscribeUrlEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 
        margin + Scale(100), innerY, groupW - Scale(180), editH, hwnd, (HMENU)ID_SUBSCRIBE_URL_EDIT, NULL, NULL);
    SendMessage(hSubscribeUrlEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hSubscribeUrlEdit, EM_SETLIMITTEXT, MAX_URL_LEN, 0);

    hAddSubBtn = CreateWindow("BUTTON", "添加", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + groupW - Scale(90), innerY, Scale(80), editH, hwnd, (HMENU)ID_ADD_SUB_BTN, NULL, NULL);
    SendMessage(hAddSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY += lineHeight + lineGap - Scale(5);

    // 2. 订阅列表框
    hSubList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        margin + Scale(15), innerY, groupW - Scale(30), Scale(60), hwnd, (HMENU)ID_SUB_LIST, NULL, NULL);
    SendMessage(hSubList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY += Scale(60) + Scale(5);

    // 3. 操作按钮行
    hDelSubBtn = CreateWindow("BUTTON", "删除选中订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(15), innerY, Scale(120), Scale(30), hwnd, (HMENU)ID_DEL_SUB_BTN, NULL, NULL);
    SendMessage(hDelSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hFetchSubBtn = CreateWindow("BUTTON", "更新所有订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(150), innerY, Scale(120), Scale(30), hwnd, (HMENU)ID_FETCH_SUB_BTN, NULL, NULL);
    SendMessage(hFetchSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    CreateWindow("BUTTON", "清空节点列表", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(285), innerY, Scale(120), Scale(30), hwnd, (HMENU)ID_CLEAR_NODE_BTN, NULL, NULL);
    
    innerY += Scale(35);

    // 4. 节点列表
    HWND hNodeLabel = CreateWindow("STATIC", "节点列表(单击查看/双击启用):", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(200), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        margin + Scale(15), innerY + Scale(25), groupW - Scale(30), Scale(90), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += groupSubH + Scale(15);
    
    // ----- 下方配置区域 -----
    HWND hConfigLabel = CreateWindow("STATIC", "配置名称:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin, curY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConfigLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hConfigNameEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + Scale(90), curY, Scale(200), editH, hwnd, (HMENU)ID_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hConfigNameEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hConfigNameEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);

    curY += lineHeight + Scale(5);

    int group1H = Scale(80);
    hGroup1 = CreateWindow("BUTTON", "核心配置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY_Config = curY + Scale(25);

    CreateLabelAndEdit(hwnd, "服务地址:", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_SERVER_EDIT, &hServerEdit, FALSE);
    innerY_Config += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "监听地址:", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_LISTEN_EDIT, &hListenEdit, FALSE);

    curY += group1H + Scale(15);

    int group2H = Scale(155);
    hGroup2 = CreateWindow("BUTTON", "高级选项 (可选)", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group2H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup2, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    innerY_Config = curY + Scale(25);

    CreateLabelAndEdit(hwnd, "身份令牌:", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_TOKEN_EDIT, &hTokenEdit, FALSE);
    innerY_Config += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "优选IP(域名):", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_IP_EDIT, &hIpEdit, FALSE);
    innerY_Config += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "ECH域名:", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_ECH_EDIT, &hEchEdit, FALSE);
    innerY_Config += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "DNS服务器(仅域名):", margin + Scale(15), innerY_Config, groupW - Scale(30), editH, ID_DNS_EDIT, &hDnsEdit, FALSE);

    // 按钮和日志区域的初始位置将由 WM_SIZE 消息在窗口创建完成后第一次调整。
    // 这里仅创建控件。
    int btnW = Scale(120);
    int btnH = Scale(38);
    int btnGap = Scale(20);
    int startX = margin;
    int buttonRowY = Scale(615); 
    
    hStartBtn = CreateWindow("BUTTON", "启动代理", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        startX, buttonRowY, btnW, btnH, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "停止", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + btnW + btnGap, buttonRowY, btnW, btnH, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    hSaveConfigBtn = CreateWindow("BUTTON", "保存配置", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 2, buttonRowY, btnW, btnH, hwnd, (HMENU)ID_SAVE_CONFIG_BTN, NULL, NULL);
    SendMessage(hSaveConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hLoadConfigBtn = CreateWindow("BUTTON", "加载配置", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 3, buttonRowY, btnW, btnH, hwnd, (HMENU)ID_LOAD_CONFIG_BTN, NULL, NULL);
    SendMessage(hLoadConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        rect.right - margin - btnW, buttonRowY, btnW, btnH, hwnd, (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int logLabelY = Scale(665);

    HWND hLogLabel = CreateWindow("STATIC", "运行日志:", WS_VISIBLE | WS_CHILD, 
        margin, logLabelY, Scale(100), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hLogLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int logEditY = logLabelY + Scale(25);

    hLogEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 
        margin, logEditY, winW - (margin * 2), Scale(130), hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}


// --- Process Management ---

void StartProcess() {
    if (isProcessRunning) {
        AppendLog("[警告] 代理已在运行中。\r\n");
        return;
    }

    GetControlValues();

    char cmdLine[MAX_CMD_LEN];
    // 假设可执行文件名为 ech-worker-client.exe
    // 根据实际情况构建命令行参数
    _snprintf(cmdLine, MAX_CMD_LEN, 
        "ech-worker-client.exe"
        " -s \"%s\""
        " -l \"%s\""
        " -e \"%s\""
        " -d \"%s\""
        " -t \"%s\""
        " -i \"%s\"",
        currentConfig.server, currentConfig.listen, currentConfig.ech, 
        currentConfig.dns, currentConfig.token, currentConfig.ip);

    AppendLog("--------------------------\r\n");
    AppendLog("[命令] ");
    AppendLog(cmdLine);
    AppendLog("\r\n");
    
    // 创建管道
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hPipeRead, hPipeWrite;
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0)) {
        AppendLog("[错误] 创建管道失败。\r\n");
        return;
    }

    // 子进程启动信息
    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdError = hPipeWrite;
    si.hStdOutput = hPipeWrite;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; // 隐藏子进程窗口

    ZeroMemory(&processInfo, sizeof(processInfo));

    // 启动子进程
    if (!CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &processInfo)) {
        AppendLog("[错误] 启动进程失败。错误码: ");
        char errBuf[256];
        sprintf(errBuf, "%lu\r\n", GetLastError());
        AppendLog(errBuf);
        CloseHandle(hPipeRead);
        CloseHandle(hPipeWrite);
        return;
    }

    // 关闭写句柄，启动读取线程
    CloseHandle(hPipeWrite);
    hLogPipe = hPipeRead;
    hLogThread = CreateThread(NULL, 0, LogReaderThread, NULL, 0, NULL);
    
    // 更新UI状态
    isProcessRunning = TRUE;
    EnableWindow(hStartBtn, FALSE);
    EnableWindow(hStopBtn, TRUE);
    AppendLog("[代理] 启动成功。\r\n");
}

void StopProcess() {
    if (!isProcessRunning) {
        AppendLog("[警告] 代理未在运行中。\r\n");
        return;
    }

    // 尝试优雅关闭
    if (GenerateConsoleCtrlEvent(CTRL_C_EVENT, processInfo.dwProcessId)) {
        AppendLog("[代理] 尝试发送 Ctrl+C 信号...\r\n");
        if (WaitForSingleObject(processInfo.hProcess, 5000) == WAIT_TIMEOUT) {
            AppendLog("[代理] 进程未响应，强制终止。\r\n");
            TerminateProcess(processInfo.hProcess, 0);
        }
    } else {
        AppendLog("[代理] 发送 Ctrl+C 失败，强制终止。\r\n");
        TerminateProcess(processInfo.hProcess, 0);
    }

    // 关闭句柄和线程
    if (hLogThread) {
        // 由于日志线程可能正在 ReadFile 阻塞，强制关闭管道使其退出
        CloseHandle(hLogPipe); 
        hLogPipe = NULL;
        WaitForSingleObject(hLogThread, 1000); 
        CloseHandle(hLogThread);
        hLogThread = NULL;
    }

    CloseHandle(processInfo.hProcess);
    CloseHandle(processInfo.hThread);

    // 更新UI状态
    isProcessRunning = FALSE;
    EnableWindow(hStartBtn, TRUE);
    EnableWindow(hStopBtn, FALSE);
    AppendLog("[代理] 已停止。\r\n");
}

DWORD WINAPI LogReaderThread(LPVOID lpParam) {
    (void)lpParam;
    char buffer[4096];
    DWORD bytesRead;

    while (hLogPipe != NULL) {
        if (ReadFile(hLogPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            // 将读取到的日志发送给主线程处理
            AppendLogAsync(buffer);
        } else {
            // ReadFile 失败或管道关闭，线程退出
            break;
        }
    }
    
    if (hLogPipe) CloseHandle(hLogPipe);
    hLogPipe = NULL;

    // 如果进程仍在运行，但管道断开，则说明进程可能已异常退出
    if (isProcessRunning) {
        AppendLog("[错误] 日志读取管道断开。代理可能已异常退出。\r\n");
        isProcessRunning = FALSE; // 更新状态
        PostMessage(hMainWindow, WM_COMMAND, ID_STOP_BTN, 0); // 触发停止流程以更新UI
    }

    return 0;
}


// --- Log & UI Helpers ---

void AppendLog(const char* text) {
    // 主线程调用，直接追加
    int len = GetWindowTextLength(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void AppendLogAsync(const char* text) {
    // 跨线程调用，需要复制字符串并在主线程中释放
    char* copy = strdup(text);
    if (copy) {
        PostMessage(hMainWindow, WM_APPEND_LOG, 0, (LPARAM)copy);
    }
}

void InitTrayIcon(HWND hwnd) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APP_ICON));
    if (!nid.hIcon) nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(nid.szTip, APP_TITLE);
}

void ShowTrayIcon() {
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}


// --- Config Management ---

void GetControlValues() {
    GetWindowText(hConfigNameEdit, currentConfig.configName, MAX_SMALL_LEN);
    GetWindowText(hServerEdit, currentConfig.server, MAX_URL_LEN);
    GetWindowText(hListenEdit, currentConfig.listen, MAX_SMALL_LEN);
    GetWindowText(hTokenEdit, currentConfig.token, MAX_URL_LEN);
    GetWindowText(hIpEdit, currentConfig.ip, MAX_SMALL_LEN);
    GetWindowText(hDnsEdit, currentConfig.dns, MAX_SMALL_LEN);
    GetWindowText(hEchEdit, currentConfig.ech, MAX_SMALL_LEN);
}

void SetControlValues() {
    SetWindowText(hConfigNameEdit, currentConfig.configName);
    SetWindowText(hServerEdit, currentConfig.server);
    SetWindowText(hListenEdit, currentConfig.listen);
    SetWindowText(hTokenEdit, currentConfig.token);
    SetWindowText(hIpEdit, currentConfig.ip);
    SetWindowText(hDnsEdit, currentConfig.dns);
    SetWindowText(hEchEdit, currentConfig.ech);
}

void SaveConfigToFile() {
    GetControlValues();
    char fileName[MAX_PATH];
    char configNameGBK[MAX_SMALL_LEN];
    char appPath[MAX_PATH];
    
    GetModuleFileName(NULL, appPath, MAX_PATH);
    PathRemoveFileSpec(appPath);
    
    // 创建 config 目录
    char configDir[MAX_PATH];
    PathCombine(configDir, appPath, "config");
    CreateDirectory(configDir, NULL);

    char *gbkName = UTF8ToGBK(currentConfig.configName);
    if (!gbkName) {
        AppendLog("[错误] 配置名称编码转换失败。\r\n");
        return;
    }
    _snprintf(configNameGBK, MAX_SMALL_LEN, "%s.ini", gbkName);
    free(gbkName);

    PathCombine(fileName, configDir, configNameGBK);

    if (WritePrivateProfileString("Config", "ConfigName", currentConfig.configName, fileName) &&
        WritePrivateProfileString("Config", "Server", currentConfig.server, fileName) &&
        WritePrivateProfileString("Config", "Listen", currentConfig.listen, fileName) &&
        WritePrivateProfileString("Config", "Token", currentConfig.token, fileName) &&
        WritePrivateProfileString("Config", "IP", currentConfig.ip, fileName) &&
        WritePrivateProfileString("Config", "DNS", currentConfig.dns, fileName) &&
        WritePrivateProfileString("Config", "ECH", currentConfig.ech, fileName))
    {
        AppendLog("[配置] 成功保存配置: ");
        AppendLog(currentConfig.configName);
        AppendLog("\r\n");
    } else {
        AppendLog("[错误] 保存配置失败。\r\n");
    }
}

void LoadConfigFromFile() {
    char configName[MAX_SMALL_LEN] = "默认配置";
    if (GetWindowText(hConfigNameEdit, configName, MAX_SMALL_LEN) == 0) {
        MessageBox(hMainWindow, "请输入要加载的配置名称。", "提示", MB_OK | MB_ICONWARNING);
        return;
    }

    char fileName[MAX_PATH];
    char configNameGBK[MAX_SMALL_LEN];
    char appPath[MAX_PATH];
    
    GetModuleFileName(NULL, appPath, MAX_PATH);
    PathRemoveFileSpec(appPath);
    
    char configDir[MAX_PATH];
    PathCombine(configDir, appPath, "config");

    char *gbkName = UTF8ToGBK(configName);
    if (!gbkName) {
        AppendLog("[错误] 配置名称编码转换失败。\r\n");
        return;
    }
    _snprintf(configNameGBK, MAX_SMALL_LEN, "%s.ini", gbkName);
    free(gbkName);

    PathCombine(fileName, configDir, configNameGBK);

    if (GetFileAttributes(fileName) == INVALID_FILE_ATTRIBUTES) {
        AppendLog("[错误] 配置不存在或无法访问: ");
        AppendLog(configName);
        AppendLog("\r\n");
        return;
    }

    // Load config from file
    GetPrivateProfileString("Config", "ConfigName", configName, currentConfig.configName, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Config", "Server", "example.com:443", currentConfig.server, MAX_URL_LEN, fileName);
    GetPrivateProfileString("Config", "Listen", "127.0.0.1:30000", currentConfig.listen, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Config", "Token", "", currentConfig.token, MAX_URL_LEN, fileName);
    GetPrivateProfileString("Config", "IP", "", currentConfig.ip, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Config", "DNS", "dns.alidns.com/dns-query", currentConfig.dns, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Config", "ECH", "cloudflare-ech.com", currentConfig.ech, MAX_SMALL_LEN, fileName);
    
    // SetControlValues() will be called in WM_COMMAND handler
    AppendLog("[配置] 成功加载配置: ");
    AppendLog(currentConfig.configName);
    AppendLog("\r\n");
}

void LoadConfig() {
    // 从程序根目录的默认配置文件加载（如果存在）
    char fileName[MAX_PATH];
    GetModuleFileName(NULL, fileName, MAX_PATH);
    PathRemoveFileSpec(fileName);
    PathCombine(fileName, fileName, "default.ini");

    if (GetFileAttributes(fileName) != INVALID_FILE_ATTRIBUTES) {
        GetPrivateProfileString("Config", "ConfigName", currentConfig.configName, currentConfig.configName, MAX_SMALL_LEN, fileName);
        GetPrivateProfileString("Config", "Server", currentConfig.server, currentConfig.server, MAX_URL_LEN, fileName);
        GetPrivateProfileString("Config", "Listen", currentConfig.listen, currentConfig.listen, MAX_SMALL_LEN, fileName);
        GetPrivateProfileString("Config", "Token", currentConfig.token, currentConfig.token, MAX_URL_LEN, fileName);
        GetPrivateProfileString("Config", "IP", currentConfig.ip, currentConfig.ip, MAX_SMALL_LEN, fileName);
        GetPrivateProfileString("Config", "DNS", currentConfig.dns, currentConfig.dns, MAX_SMALL_LEN, fileName);
        GetPrivateProfileString("Config", "ECH", currentConfig.ech, currentConfig.ech, MAX_SMALL_LEN, fileName);
        AppendLog("[配置] 成功加载默认配置。\r\n");
    }
}

// --- Subscription/Node Management ---

void AddSubscription() {
    char url[MAX_URL_LEN];
    GetWindowText(hSubscribeUrlEdit, url, MAX_URL_LEN);
    
    if (strlen(url) < 10) {
        MessageBox(hMainWindow, "请输入有效的订阅链接。", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    // 检查是否已存在
    int count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char existingUrl[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)existingUrl);
        if (strcmp(url, existingUrl) == 0) {
            MessageBox(hMainWindow, "订阅链接已存在。", "提示", MB_OK | MB_ICONINFORMATION);
            SetWindowText(hSubscribeUrlEdit, "");
            return;
        }
    }

    SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
    SetWindowText(hSubscribeUrlEdit, "");
    SaveSubscriptionList();
    AppendLog("[订阅] 添加成功。\r\n");
}

void DelSubscription() {
    int index = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
    if (index == LB_ERR) {
        MessageBox(hMainWindow, "请选择要删除的订阅链接。", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    SendMessage(hSubList, LB_DELETESTRING, index, 0);
    SaveSubscriptionList();
    AppendLog("[订阅] 删除成功。\r\n");
}

void SaveSubscriptionList() {
    char fileName[MAX_PATH];
    GetModuleFileName(NULL, fileName, MAX_PATH);
    PathRemoveFileSpec(fileName);
    PathCombine(fileName, fileName, "subscriptions.txt");
    
    FILE* fp = fopen(fileName, "w");
    if (!fp) {
        AppendLog("[错误] 无法保存订阅列表。\r\n");
        return;
    }
    
    int count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        fprintf(fp, "%s\n", url);
    }
    
    fclose(fp);
}

void LoadSubscriptionList() {
    char fileName[MAX_PATH];
    GetModuleFileName(NULL, fileName, MAX_PATH);
    PathRemoveFileSpec(fileName);
    PathCombine(fileName, fileName, "subscriptions.txt");

    FILE* fp = fopen(fileName, "r");
    if (!fp) return;
    
    char url[MAX_URL_LEN];
    while (fgets(url, MAX_URL_LEN, fp)) {
        size_t len = strlen(url);
        if (len > 0 && url[len - 1] == '\n') url[len - 1] = '\0';
        if (len > 1 && url[len - 2] == '\r') url[len - 2] = '\0';
        if (strlen(url) > 0) {
            SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
        }
    }
    
    fclose(fp);
}

void ClearNodeList() {
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    g_totalNodeCount = 0;
    AppendLog("[节点] 节点列表已清空。\r\n");
    // 清空 nodes 目录下的文件
    char path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    PathRemoveFileSpec(path);
    PathCombine(path, path, "nodes\\*");
    
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(path, &ffd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char filePath[MAX_PATH];
                PathCombine(filePath, path, ffd.cFileName);
                DeleteFile(filePath);
            }
        } while (FindNextFile(hFind, &ffd) != 0);
        FindClose(hFind);
        AppendLog("[节点] nodes 目录下旧配置文件已清除。\r\n");
    }
}

void SaveNodeConfig(int nodeIndex) {
    // 保存当前配置到临时文件，以便用户切换时可以恢复
    Config tempConfig;
    GetControlValues();
    memcpy(&tempConfig, &currentConfig, sizeof(Config));

    char nodeName[MAX_URL_LEN];
    SendMessage(hNodeList, LB_GETTEXT, nodeIndex, (LPARAM)nodeName);

    char fileName[MAX_PATH];
    char appPath[MAX_PATH];
    GetModuleFileName(NULL, appPath, MAX_PATH);
    PathRemoveFileSpec(appPath);
    
    char nodesDir[MAX_PATH];
    PathCombine(nodesDir, appPath, "nodes");
    CreateDirectory(nodesDir, NULL);

    char nodeFileName[256];
    _snprintf(nodeFileName, 256, "%d.ini", nodeIndex);
    PathCombine(fileName, nodesDir, nodeFileName);

    if (WritePrivateProfileString("Node", "ConfigName", tempConfig.configName, fileName) &&
        WritePrivateProfileString("Node", "Server", tempConfig.server, fileName) &&
        WritePrivateProfileString("Node", "Listen", tempConfig.listen, fileName) &&
        WritePrivateProfileString("Node", "Token", tempConfig.token, fileName) &&
        WritePrivateProfileString("Node", "IP", tempConfig.ip, fileName) &&
        WritePrivateProfileString("Node", "DNS", tempConfig.dns, fileName) &&
        WritePrivateProfileString("Node", "ECH", tempConfig.ech, fileName) &&
        WritePrivateProfileString("Node", "NodeName", nodeName, fileName))
    {
        AppendLog("[节点] 当前配置已保存到临时文件。\r\n");
    }
}

void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart) {
    char fileName[MAX_PATH];
    char appPath[MAX_PATH];
    GetModuleFileName(NULL, appPath, MAX_PATH);
    PathRemoveFileSpec(appPath);
    
    char nodesDir[MAX_PATH];
    PathCombine(nodesDir, appPath, "nodes");

    char nodeFileName[256];
    _snprintf(nodeFileName, 256, "%d.ini", nodeIndex);
    PathCombine(fileName, nodesDir, nodeFileName);

    if (GetFileAttributes(fileName) == INVALID_FILE_ATTRIBUTES) {
        AppendLog("[错误] 节点配置不存在。\r\n");
        return;
    }

    // Load node config
    GetPrivateProfileString("Node", "ConfigName", "节点配置", currentConfig.configName, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Node", "Server", "example.com:443", currentConfig.server, MAX_URL_LEN, fileName);
    GetPrivateProfileString("Node", "Listen", "127.0.0.1:30000", currentConfig.listen, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Node", "Token", "", currentConfig.token, MAX_URL_LEN, fileName);
    GetPrivateProfileString("Node", "IP", "", currentConfig.ip, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Node", "DNS", "dns.alidns.com/dns-query", currentConfig.dns, MAX_SMALL_LEN, fileName);
    GetPrivateProfileString("Node", "ECH", "cloudflare-ech.com", currentConfig.ech, MAX_SMALL_LEN, fileName);

    SetControlValues();
    
    char nodeName[MAX_URL_LEN];
    GetPrivateProfileString("Node", "NodeName", "Unknown Node", nodeName, MAX_URL_LEN, fileName);

    AppendLog("[节点] ");
    AppendLog(nodeName);
    AppendLog(autoStart ? " 已启用。\r\n" : " 配置已加载。\r\n");
}


// --- Network and Parsing ---

void ProcessSingleSubscription(const char* url) {
    HINTERNET hInternet = InternetOpen("ECHWorkerClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        AppendLog("[错误] 无法初始化网络连接。\r\n");
        return;
    }

    AppendLog("[订阅] 正在获取: ");
    AppendLog(url);
    AppendLog("\r\n");

    HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hConnect) {
        AppendLog("[错误] 无法连接到订阅地址。\r\n");
        InternetCloseHandle(hInternet);
        return;
    }

    DWORD bufSize = 32768;
    char* buffer = (char*)malloc(bufSize);
    if (!buffer) {
        AppendLog("[错误] 内存分配失败。\r\n");
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return;
    }
    buffer[0] = '\0';
    
    DWORD totalRead = 0;
    DWORD bytesRead = 0;
    char tempBuf[4096];
    
    while (InternetReadFile(hConnect, tempBuf, sizeof(tempBuf) - 1, &bytesRead) && bytesRead > 0) {
        tempBuf[bytesRead] = 0;
        if (totalRead + bytesRead < bufSize - 1) {
            strcat(buffer, tempBuf);
            totalRead += bytesRead;
        } else {
            // 简单处理：如果数据超过初始缓冲区大小，尝试重新分配
            AppendLog("[警告] 订阅内容过大，可能被截断。\r\n");
            break;
        }
    }
    
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (totalRead > 0) {
        ParseSubscriptionData(buffer);
    } else {
        AppendLog("[订阅] 获取数据为空。\r\n");
    }
    
    free(buffer);
}

void FetchAllSubscriptions() {
    int subCount = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    if (subCount == 0) {
        MessageBox(hMainWindow, "请先添加订阅链接", "提示", MB_OK);
        return;
    }
    
    AppendLog("--------------------------\r\n");
    AppendLog("[订阅] 开始更新所有订阅...\r\n");
    
    // 1. 清空当前节点列表UI
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    
    // 2. 重置全局节点计数器
    g_totalNodeCount = 0;
    
    // 3. 清空 nodes 目录下的旧文件
    char path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    PathRemoveFileSpec(path);
    PathCombine(path, path, "nodes");
    
    // 确保 nodes 目录存在
    CreateDirectory(path, NULL); 
    
    char searchPath[MAX_PATH];
    PathCombine(searchPath, path, "*.ini");
    
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile(searchPath, &ffd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char filePath[MAX_PATH];
                PathCombine(filePath, path, ffd.cFileName);
                DeleteFile(filePath);
            }
        } while (FindNextFile(hFind, &ffd) != 0);
        FindClose(hFind);
    }
    
    // 4. 遍历所有订阅链接并处理
    for (int i = 0; i < subCount; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        ProcessSingleSubscription(url);
    }
    
    AppendLog("--------------------------\r\n");
    char finishMsg[256];
    _snprintf(finishMsg, 256, "[订阅] 更新完成。总计 %d 个节点。\r\n", g_totalNodeCount);
    AppendLog(finishMsg);
}

void ParseSubscriptionData(const char* data) {
    char* decodedData = NULL;
    size_t decodedLen = 0;
    
    // 1. 检查是否为 Base64 编码
    if (is_base64_encoded(data)) {
        decodedData = base64_decode(data, &decodedLen);
    } else {
        // 如果不是 Base64，假设已经是明文
        decodedData = strdup(data);
        if (decodedData) decodedLen = strlen(decodedData);
    }
    
    if (!decodedData || decodedLen == 0) {
        AppendLog("[解析] 订阅数据解码失败或内容为空。\r\n");
        return;
    }

    // 2. 将数据从 UTF-8 转换为 GBK（因为 WinAPI 默认使用 GBK 存储文件）
    char* gbkData = UTF8ToGBK(decodedData);
    if (!gbkData) {
        AppendLog("[解析] UTF-8 到 GBK 转换失败。\r\n");
        free(decodedData);
        return;
    }

    // 3. 逐行解析 Vmess/Vless/Trojan 链接
    char* line = strtok(gbkData, "\n\r");
    char nodePrefix[64];
    
    while (line) {
        if (strstr(line, "vmess://") == line || strstr(line, "vless://") == line || strstr(line, "trojan://") == line) {
            
            // 4. URL 解码（处理链接中的备注/名称）
            char* urlEncodedPart = strrchr(line, '#');
            if (urlEncodedPart) {
                // 找到 # 后的部分
                char* decodedName = URLDecode(urlEncodedPart + 1);
                
                // 将原链接中的名称替换为解码后的名称
                char tempLine[MAX_URL_LEN];
                *urlEncodedPart = '\0'; // 暂时截断 # 及之后的部分
                _snprintf(tempLine, MAX_URL_LEN, "%s#%s", line, decodedName ? decodedName : "Unknown Node");
                
                // 5. 提取节点名称 (这里只做显示用，实际配置解析需要更复杂的逻辑)
                char* nodeName = decodedName ? decodedName : "Unknown Node";
                
                // 6. 保存节点配置 (这里仅保存一个简单的节点名称，完整功能需要解析整个链接并生成配置文件)
                char fileName[MAX_PATH];
                char appPath[MAX_PATH];
                GetModuleFileName(NULL, appPath, MAX_PATH);
                PathRemoveFileSpec(appPath);
                
                char nodesDir[MAX_PATH];
                PathCombine(nodesDir, appPath, "nodes");
                CreateDirectory(nodesDir, NULL);

                char nodeFileName[256];
                _snprintf(nodeFileName, 256, "%d.ini", g_totalNodeCount);
                PathCombine(fileName, nodesDir, nodeFileName);
                
                // 简单示例：将节点名称和原始链接保存为 ini 文件
                WritePrivateProfileString("Node", "NodeName", nodeName, fileName);
                WritePrivateProfileString("Node", "Link", tempLine, fileName); 
                // 实际应解析 link，提取 server, port, uuid/password, alpn/sni 等，并保存到文件

                // 7. 更新 UI 列表
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nodeName);
                g_totalNodeCount++;
                
                if (decodedName) free(decodedName);
            }
        }
        line = strtok(NULL, "\n\r");
    }
    
    free(decodedData);
    free(gbkData);
}

// --- Encoding/Decoding Functions ---

char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str) return strdup("");
    
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    WCHAR* wStr = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (!wStr) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wStr, len);

    int gbkLen = WideCharToMultiByte(CP_ACP, 0, wStr, -1, NULL, 0, NULL, NULL);
    char* gbkStr = (char*)malloc(gbkLen);
    if (!gbkStr) { free(wStr); return NULL; }
    WideCharToMultiByte(CP_ACP, 0, wStr, -1, gbkStr, gbkLen, NULL, NULL);

    free(wStr);
    return gbkStr;
}

char* GBKToUTF8(const char* gbkStr) {
    if (!gbkStr) return strdup("");
    
    int len = MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, NULL, 0);
    WCHAR* wStr = (WCHAR*)malloc(len * sizeof(WCHAR));
    if (!wStr) return NULL;
    MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, wStr, len);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wStr, -1, NULL, 0, NULL, NULL);
    char* utf8Str = (char*)malloc(utf8Len);
    if (!utf8Str) { free(wStr); return NULL; }
    WideCharToMultiByte(CP_UTF8, 0, wStr, -1, utf8Str, utf8Len, NULL, NULL);

    free(wStr);
    return utf8Str;
}

char* URLDecode(const char* str) {
    size_t len = strlen(str);
    char* decoded = (char*)malloc(len + 1);
    if (!decoded) return NULL;
    char* dest = decoded;
    
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%') {
            if (i + 2 < len) {
                char hex[3] = {str[i+1], str[i+2], '\0'};
                *dest++ = (char)strtol(hex, NULL, 16);
                i += 2;
            } else {
                *dest++ = str[i];
            }
        } else if (str[i] == '+') {
            *dest++ = ' ';
        } else {
            *dest++ = str[i];
        }
    }
    *dest = '\0';
    return decoded;
}

BOOL is_base64_encoded(const char* data) {
    if (!data) return FALSE;
    size_t len = strlen(data);
    if (len % 4 != 0) return FALSE;

    for (size_t i = 0; i < len; i++) {
        if (!isalnum(data[i]) && data[i] != '+' && data[i] != '/' && data[i] != '=') {
            return FALSE;
        }
    }
    return TRUE;
}

// 简化的 base64_decode (需要一个完整的实现，这里提供一个骨架)
// 实际生产环境应使用更健壮的库或实现
char* base64_decode(const char* input, size_t* out_len) {
    // 警告: 这是一个简化的/不完整的 Base64 解码实现骨架。
    // 在生产环境中，应使用如 Mbed TLS, OpenSSL 或 Windows Cryptography API 提供的 Base64 解码函数。
    // 由于 WinAPI 缺乏直接的公共 Base64 解码函数，这里仅为占位。
    
    int len = MultiByteToWideChar(CP_UTF8, 0, input, -1, NULL, 0);
    if (len == 0) return NULL;
    
    // Windows API Cryptographic functions (需要引入 Wincrypt.h 和 lib Advapi32.lib)
    // 假设 Advapi32.lib 已链接

    DWORD dwSkip = 0;
    DWORD dwFlags = CRYPT_STRING_BASE64;
    
    // 1. 获取所需缓冲区大小
    DWORD dwDestLen = 0;
    if (!CryptStringToBinary(input, 0, dwFlags, NULL, &dwDestLen, &dwSkip, NULL)) {
        // 如果失败，返回原始数据或 NULL
        *out_len = 0;
        return strdup(input); // 简单返回原始数据
    }

    // 2. 分配缓冲区
    BYTE* pbDest = (BYTE*)malloc(dwDestLen + 1);
    if (!pbDest) {
        *out_len = 0;
        return NULL;
    }

    // 3. 执行解码
    if (!CryptStringToBinary(input, 0, dwFlags, pbDest, &dwDestLen, &dwSkip, NULL)) {
        free(pbDest);
        *out_len = 0;
        return strdup(input); // 解码失败，返回原始数据
    }

    pbDest[dwDestLen] = '\0';
    *out_len = dwDestLen;
    return (char*)pbDest;
}
