#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "Advapi32.lib") // 用于 CryptStringToBinary
#pragma comment(lib, "crypt32.lib")  // 用于 CryptStringToBinary

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shlwapi.h> // For PathCombine
#include <wincrypt.h> // 用于 CryptStringToBinary
#include <shlobj.h>   // For SHCreateDirectoryEx
#include <ctype.h>    // For isspace, isalnum

#define SINGLE_INSTANCE_MUTEX_NAME "ECHWorkerClient_Mutex_Unique_ID"
#define IDI_APP_ICON 101 // 假设您的资源文件中有此ID

typedef BOOL (WINAPI *SetProcessDPIAwareFunc)(void);

#define APP_VERSION "1.2"
#define APP_TITLE "ECH workers 客户端 v" APP_VERSION

#define MAX_URL_LEN 8192
#define MAX_SMALL_LEN 2048
#define MAX_CMD_LEN 32768
#define MAX_NODES 512 // 节点列表最大容量

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

typedef struct {
    char name[MAX_SMALL_LEN];
    char server[MAX_URL_LEN];
} NodeConfig;

NodeConfig g_nodes[MAX_NODES]; // 全局节点数组


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
void SaveNodeList(); // 占位函数
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart);
void ClearNodeList();
char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
BOOL IsUTF8File(const char* fileName);
char* base64_decode(const char* input, size_t* out_len); 
BOOL is_base64_encoded(const char* data);
void ProcessNodeUrl(const char* nodeUrl);

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

    // 修改：设置初始窗口大小为 800x700
    int winWidth = Scale(800);
    int winHeight = Scale(700); 
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

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
            // --- 动态布局逻辑 ---
            
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
                
            // Find the Y position for the button row, relative to the window bottom
            int logLabelY = newHeight - Scale(170); 
            int buttonRowY = logLabelY - Scale(38) - Scale(15); 
            
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
            MoveWindow(GetDlgItem(hwnd, ID_LOG_EDIT - 1), 
                margin, logLabelY, Scale(100), Scale(20), TRUE); 

            // Log Edit Y: Log Label Y + Label Height + Gap
            int logEditY = logLabelY + Scale(25); 
            // 日志框高度
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

    // 按钮和日志区域的初始位置
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
    SaveConfig(); // 保存当前配置到默认 config.ini

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
        AppendLog("[错误] 启动进程失败。请检查 ech-worker-client.exe 是否存在于当前目录。错误码: ");
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
        if (hLogPipe) CloseHandle(hLogPipe); 
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
        // 确保在主线程中执行 StopProcess
        PostMessage(hMainWindow, WM_COMMAND, ID_STOP_BTN, 0); 
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

    if (strlen(currentConfig.configName) == 0) {
        MessageBox(hMainWindow, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    char safeName[240];
    strncpy(safeName, currentConfig.configName, sizeof(safeName) - 1);
    safeName[sizeof(safeName) - 1] = '\0';
    
    // 替换文件名中的非法字符
    for (int i = 0; safeName[i] != '\0'; i++) {
        if (safeName[i] == '/' || safeName[i] == '\\' || safeName[i] == ':' || 
            safeName[i] == '*' || safeName[i] == '?' || safeName[i] == '"' || 
            safeName[i] == '<' || safeName[i] == '>' || safeName[i] == '|') {
            safeName[i] = '_';
        }
    }
    
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "%s.ini", safeName);
    
    OPENFILENAME ofn;
    char szFile[MAX_PATH];
    snprintf(szFile, sizeof(szFile), "%s", fileName);

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "配置(*.ini)\0*.ini\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "ini";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY;

    if (GetSaveFileName(&ofn) == TRUE) {
        FILE* f = fopen(szFile, "w");
        if (!f) {
            MessageBox(hMainWindow, "保存配置失败", "错误", MB_OK | MB_ICONERROR);
            return;
        }

        fprintf(f, "[ECHTunnel]\n");
        fprintf(f, "configName=%s\n", currentConfig.configName);
        fprintf(f, "server=%s\n", currentConfig.server);
        fprintf(f, "listen=%s\n", currentConfig.listen);
        fprintf(f, "token=%s\n", currentConfig.token);
        fprintf(f, "ip=%s\n", currentConfig.ip);
        fprintf(f, "dns=%s\n", currentConfig.dns);
        fprintf(f, "ech=%s\n", currentConfig.ech);
        
        fclose(f);
        char msg[512];
        snprintf(msg, sizeof(msg), "配置已保存到: %s", szFile);
        MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
        AppendLog("[配置] 已保存配置文件\r\n");
    }
}

void LoadConfigFromFile() {
    OPENFILENAME ofn;
    char szFile[MAX_PATH] = "";
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "配置(*.ini)\0*.ini\0所有文件(*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;

    if (GetOpenFileName(&ofn) == TRUE) {
        char line[MAX_URL_LEN];
        FILE* f = fopen(szFile, "r");
        if (!f) {
            MessageBox(hMainWindow, "打开配置失败", "错误", MB_OK | MB_ICONERROR);
            return;
        }

        Config newConfig = {
            "默认配置", "dns.alidns.com/dns-query", "cloudflare-ech.com", "example.com:443", "", "127.0.0.1:30000", ""
        };

        while (fgets(line, sizeof(line), f)) {
            char* val = strchr(line, '=');
            if (!val) continue;
            *val++ = 0;
            if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;
            if (val[strlen(val)-1] == '\r') val[strlen(val)-1] = 0;

            size_t key_len = strlen(line);
            while (key_len > 0 && isspace(line[key_len - 1])) {
                line[key_len - 1] = '\0';
                key_len--;
            }

            if (!strcmp(line, "configName")) strncpy(newConfig.configName, val, sizeof(newConfig.configName) - 1);
            else if (!strcmp(line, "server")) strncpy(newConfig.server, val, sizeof(newConfig.server) - 1);
            else if (!strcmp(line, "listen")) strncpy(newConfig.listen, val, sizeof(newConfig.listen) - 1);
            else if (!strcmp(line, "token")) strncpy(newConfig.token, val, sizeof(newConfig.token) - 1);
            else if (!strcmp(line, "ip")) strncpy(newConfig.ip, val, sizeof(newConfig.ip) - 1);
            else if (!strcmp(line, "dns")) strncpy(newConfig.dns, val, sizeof(newConfig.dns) - 1);
            else if (!strcmp(line, "ech")) strncpy(newConfig.ech, val, sizeof(newConfig.ech) - 1);
        }
        fclose(f);
        
        currentConfig = newConfig;
        char msg[512];
        snprintf(msg, sizeof(msg), "配置已加载: %s", currentConfig.configName);
        MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
        AppendLog("[配置] 已加载配置文件\r\n");
    }
}

void SaveConfig() {
    // 自动保存当前配置到程序目录下的 config.ini (默认配置文件)
    FILE* f = fopen("config.ini", "w");
    if (!f) return;
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\n", 
        currentConfig.configName, currentConfig.server, currentConfig.listen, 
        currentConfig.token, currentConfig.ip, currentConfig.dns, currentConfig.ech);
    fclose(f);
}

void LoadConfig() {
    // 自动从程序目录下的 config.ini 加载配置
    FILE* f = fopen("config.ini", "r");
    if (!f) return;
    char line[MAX_URL_LEN];
    Config newConfig = currentConfig;
    
    while (fgets(line, sizeof(line), f)) {
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;
        if (val[strlen(val)-1] == '\r') val[strlen(val)-1] = 0;
        
        size_t key_len = strlen(line);
        while (key_len > 0 && isspace(line[key_len - 1])) {
            line[key_len - 1] = '\0';
            key_len--;
        }

        if (!strcmp(line, "configName")) strncpy(newConfig.configName, val, sizeof(newConfig.configName) - 1);
        else if (!strcmp(line, "server")) strncpy(newConfig.server, val, sizeof(newConfig.server) - 1);
        else if (!strcmp(line, "listen")) strncpy(newConfig.listen, val, sizeof(newConfig.listen) - 1);
        else if (!strcmp(line, "token")) strncpy(newConfig.token, val, sizeof(newConfig.token) - 1);
        else if (!strcmp(line, "ip")) strncpy(newConfig.ip, val, sizeof(newConfig.ip) - 1);
        else if (!strcmp(line, "dns")) strncpy(newConfig.dns, val, sizeof(newConfig.dns) - 1);
        else if (!strcmp(line, "ech")) strncpy(newConfig.ech, val, sizeof(newConfig.ech) - 1);
    }
    fclose(f);
    currentConfig = newConfig;
}

// --- Subscription/Node Management ---

void AddSubscription() {
    char url[MAX_URL_LEN];
    GetWindowText(hSubscribeUrlEdit, url, MAX_URL_LEN);
    
    if (strlen(url) < 10) {
        MessageBox(hMainWindow, "请输入有效的订阅链接。", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
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
    SendMessage(hSubList, LB_RESETCONTENT, 0, 0);
    
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
    
    // 清空 nodes 目录下的文件
    SHCreateDirectoryEx(NULL, "nodes", NULL); // 确保目录存在
    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA("nodes\\*.ini", &ffd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                char filePath[MAX_PATH];
                snprintf(filePath, MAX_PATH, "nodes\\%s", ffd.cFileName);
                DeleteFileA(filePath);
            }
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
        AppendLog("[节点] nodes 目录下旧配置文件已清除。\r\n");
    }
    
    AppendLog("[节点] 节点列表已清空。\r\n");
}

void SaveNodeConfig(int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= g_totalNodeCount) return;

    // 1. 创建 nodes 目录
    SHCreateDirectoryEx(NULL, "nodes", NULL);

    // 2. 构造文件名 (e.g., nodes/000.ini, nodes/001.ini)
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/%03d.ini", nodeIndex);

    // 3. 写入配置
    FILE* f = fopen(fileName, "w");
    if (!f) {
        AppendLog("[错误] 无法保存节点配置文件\r\n");
        return;
    }

    // 确保获取最新的全局配置作为默认值
    GetControlValues(); 

    fprintf(f, "[ECHTunnel]\n");
    // 写入节点名称和服务器地址
    fprintf(f, "configName=%s\n", g_nodes[nodeIndex].name);
    fprintf(f, "server=%s\n", g_nodes[nodeIndex].server);
    // 写入默认配置（取自全局 currentConfig）
    fprintf(f, "listen=%s\n", currentConfig.listen);
    fprintf(f, "token=%s\n", currentConfig.token);
    fprintf(f, "ip=%s\n", currentConfig.ip);
    fprintf(f, "dns=%s\n", currentConfig.dns);
    fprintf(f, "ech=%s\n", currentConfig.ech);

    fclose(f);
    // AppendLog("[节点] 当前配置已保存到临时文件。\r\n"); // 频繁调用会刷屏，禁用
}

void LoadNodeList() {
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    g_totalNodeCount = 0;

    // 1. 查找 nodes 目录下的所有 .ini 文件
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA("nodes/*.ini", &findData);

    if (hFind == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char fileName[MAX_PATH];
            snprintf(fileName, sizeof(fileName), "nodes/%s", findData.cFileName);
            
            // 2. 读取配置文件以获取节点名称和服务器地址
            FILE* f = fopen(fileName, "r");
            if (f) {
                char line[MAX_URL_LEN];
                char name[MAX_SMALL_LEN] = "未知节点";
                char server[MAX_URL_LEN] = "";
                
                while (fgets(line, sizeof(line), f)) {
                    char* val = strchr(line, '=');
                    if (!val) continue;
                    *val++ = 0;
                    if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;
                    if (val[strlen(val)-1] == '\r') val[strlen(val)-1] = 0;

                    size_t key_len = strlen(line);
                    while (key_len > 0 && isspace(line[key_len - 1])) {
                        line[key_len - 1] = '\0';
                        key_len--;
                    }
                    
                    if (!strcmp(line, "configName")) strncpy(name, val, sizeof(name) - 1);
                    else if (!strcmp(line, "server")) strncpy(server, val, sizeof(server) - 1);
                }
                fclose(f);

                if (server[0] != '\0' && g_totalNodeCount < MAX_NODES) {
                    // 3. 存储到全局数组并添加到 ListBox
                    strncpy(g_nodes[g_totalNodeCount].name, name, sizeof(g_nodes[0].name) - 1);
                    strncpy(g_nodes[g_totalNodeCount].server, server, sizeof(g_nodes[0].server) - 1);
                    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)name);
                    g_totalNodeCount++;
                }
            }
        }
    } while (FindNextFileA(hFind, &findData));

    FindClose(hFind);
    // AppendLogAsync("[系统] 节点列表加载完成.\r\n"); // 启动时调用，不需提示
}

void SaveNodeList() {
    // 节点配置已在 ParseSubscriptionData 和 SaveNodeConfig 中处理，此函数无需操作。
}

void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart) {
    if (nodeIndex < 0 || nodeIndex >= g_totalNodeCount) return;

    // 1. 构造文件名
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/%03d.ini", nodeIndex);

    // 2. 读取配置文件，覆盖 currentConfig
    FILE* f = fopen(fileName, "r");
    if (!f) {
        AppendLog("[错误] 节点文件不存在或无法打开.\r\n");
        return;
    }

    char line[MAX_URL_LEN];
    Config tempConfig = currentConfig; 

    while (fgets(line, sizeof(line), f)) {
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;
        if (val[strlen(val)-1] == '\r') val[strlen(val)-1] = 0;

        size_t key_len = strlen(line);
        while (key_len > 0 && isspace(line[key_len - 1])) {
            line[key_len - 1] = '\0';
            key_len--;
        }
        
        if (!strcmp(line, "configName")) strncpy(tempConfig.configName, val, sizeof(tempConfig.configName) - 1);
        else if (!strcmp(line, "server")) strncpy(tempConfig.server, val, sizeof(tempConfig.server) - 1);
        else if (!strcmp(line, "listen")) strncpy(tempConfig.listen, val, sizeof(tempConfig.listen) - 1);
        else if (!strcmp(line, "token")) strncpy(tempConfig.token, val, sizeof(tempConfig.token) - 1);
        else if (!strcmp(line, "ip")) strncpy(tempConfig.ip, val, sizeof(tempConfig.ip) - 1);
        else if (!strcmp(line, "dns")) strncpy(tempConfig.dns, val, sizeof(tempConfig.dns) - 1);
        else if (!strcmp(line, "ech")) strncpy(tempConfig.ech, val, sizeof(tempConfig.ech) - 1);
    }
    fclose(f);

    currentConfig = tempConfig;
    SetControlValues(); // 更新 UI

    AppendLogAsync("[节点] ");
    AppendLogAsync(currentConfig.configName);
    AppendLogAsync(autoStart ? " 已启用。\r\n" : " 配置已加载。\r\n");

    if (autoStart) {
        if (isProcessRunning) StopProcess();
        SaveConfig(); // 保存当前配置到默认 ini
        StartProcess(); // 启动代理
    }
}

// --- Network and Parsing ---

void ProcessNodeUrl(const char* nodeUrl) {
    if (g_totalNodeCount >= MAX_NODES) {
        AppendLog("[警告] 节点列表已满，跳过.\r\n");
        return;
    }

    char tempUrl[MAX_URL_LEN];
    strncpy(tempUrl, nodeUrl, sizeof(tempUrl) - 1);
    tempUrl[sizeof(tempUrl) - 1] = '\0';

    char* name = "未知节点";
    char* server_port = "";
    char* decoded_name = NULL;
    
    // 1. 提取 #name 部分
    char* hash = strchr(tempUrl, '#');
    if (hash) {
        *hash = '\0';
        name = hash + 1;
        
        // URL Decode 名称
        decoded_name = URLDecode(name);
        if (decoded_name) {
            name = decoded_name;
        }
    }

    // 2. 提取协议后的地址部分
    char* protoEnd = strstr(tempUrl, "://");
    if (!protoEnd) {
        AppendLogAsync("[解析] 错误: 无效的节点URL格式: ");
        AppendLogAsync(nodeUrl);
        AppendLogAsync("\r\n");
        if (decoded_name) free(decoded_name); 
        return;
    }
    
    char* addrStart = protoEnd + 3;

    // 3. 寻找 server:port，跳过可能的 uuid/userinfo
    char* atSign = strchr(addrStart, '@');
    if (atSign) {
        server_port = atSign + 1;
    } else {
        server_port = addrStart;
    }

    // 4. 寻找第一个非地址字符 (通常是 ? 或 /)
    char* paramStart = strpbrk(server_port, "?/");
    if (paramStart) {
        *paramStart = '\0';
    }

    // 5. 存储节点配置
    strncpy(g_nodes[g_totalNodeCount].name, name, sizeof(g_nodes[0].name) - 1);
    strncpy(g_nodes[g_totalNodeCount].server, server_port, sizeof(g_nodes[0].server) - 1);
    g_nodes[g_totalNodeCount].name[sizeof(g_nodes[0].name) - 1] = '\0';
    g_nodes[g_totalNodeCount].server[sizeof(g_nodes[0].server) - 1] = '\0';

    // 6. 保存节点配置文件并添加到 ListBox
    SaveNodeConfig(g_totalNodeCount); 
    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)g_nodes[g_totalNodeCount].name);
    
    g_totalNodeCount++;
    
    if (decoded_name) free(decoded_name);
}

void ParseSubscriptionData(const char* data) {
    AppendLog("[解析] 开始解析订阅数据...\r\n");

    char* content = NULL;
    size_t content_len = 0;
    
    // 1. 检查并尝试 Base64 解码
    char* decoded_content = base64_decode(data, &content_len);
    
    if (decoded_content && content_len > 0) {
        content = decoded_content;
    } else {
        // 如果解码失败，尝试作为纯文本
        if (decoded_content) free(decoded_content);
        content = strdup(data);
        if (!content) return;
    }

    // 2. 将内容从 UTF-8 转换为 GBK（用于 strtok，因为 WinAPI 默认是 GBK）
    char* gbkContent = UTF8ToGBK(content);
    if (!gbkContent) {
        AppendLog("[解析] 编码转换失败。\r\n");
        free(content);
        return;
    }

    // 3. 按行分割并解析每个节点 URL
    char* line = strtok(gbkContent, "\n\r");
    int nodesAdded = 0;
    while (line != NULL) {
        // 移除行首尾空格
        while (isspace(*line)) line++;
        size_t len = strlen(line);
        while (len > 0 && isspace(line[len - 1])) line[--len] = 0;
        
        if (len > 0 && strstr(line, "://")) {
            // 找到一个有效的节点 URL
            ProcessNodeUrl(line);
            nodesAdded++;
        }
        line = strtok(NULL, "\n\r");
    }

    free(content);
    free(gbkContent);

    char msg[128];
    snprintf(msg, sizeof(msg), "[解析] 新增节点: %d, 总节点数: %d\r\n", nodesAdded, g_totalNodeCount);
    AppendLog(msg);
}

void ProcessSingleSubscription(const char* url) {
    if (g_totalNodeCount >= MAX_NODES) {
        AppendLog("[警告] 节点列表已满，跳过订阅.\r\n");
        return;
    }

    AppendLogAsync("[订阅] 正在获取: ");
    AppendLogAsync(url);
    AppendLogAsync("...\r\n");
    
    HINTERNET hInternet = NULL;
    HINTERNET hConnect = NULL;
    
    hInternet = InternetOpenA("ECHWorkerClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        AppendLog("[错误] WinINet 初始化失败.\r\n");
        return;
    }
    
    hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_PRAGMA_NOCACHE, 0);
    if (!hConnect) {
        AppendLog("[错误] 无法打开订阅链接。\r\n");
        InternetCloseHandle(hInternet);
        return;
    }
    
    DWORD bufSize = 512 * 1024; // 512KB 缓冲区
    char* buffer = (char*)malloc(bufSize);
    if (!buffer) {
        AppendLog("[错误] 内存分配失败.\r\n");
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return;
    }
    buffer[0] = '\0';
    
    DWORD totalRead = 0;
    DWORD bytesRead;
    char tempBuf[4096]; 

    while (InternetReadFile(hConnect, tempBuf, sizeof(tempBuf) - 1, &bytesRead) && bytesRead > 0) {
        tempBuf[bytesRead] = 0;
        if (totalRead + bytesRead < bufSize - 1) {
            strcat(buffer, tempBuf);
            totalRead += bytesRead;
        } else {
            char* newBuffer = (char*)realloc(buffer, bufSize + 512 * 1024);
            if (newBuffer) {
                buffer = newBuffer;
                bufSize += 512 * 1024;
                strcat(buffer, tempBuf);
                totalRead += bytesRead;
            } else {
                AppendLog("[警告] 数据过大，截断.\r\n");
                break; 
            }
        }
    }
    
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (totalRead > 0) {
        ParseSubscriptionData(buffer);
    } else {
        AppendLog("[订阅] 获取数据为空\r\n");
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
    
    // 3. 清理 nodes 目录下的旧文件，确保从零开始
    ClearNodeList();

    // 4. 遍历订阅列表并获取数据
    for (int i = 0; i < subCount; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        if (strlen(url) > 0) {
            ProcessSingleSubscription(url);
        }
    }

    AppendLog("[订阅] 所有订阅更新完成.\r\n");
    AppendLog("--------------------------\r\n");
}


// --- Encoding/Decoding Functions ---

char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str) return NULL;
    int lenW = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (lenW == 0) return NULL;

    wchar_t* wideStr = (wchar_t*)malloc(lenW * sizeof(wchar_t));
    if (!wideStr) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, lenW);

    int lenA = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (lenA == 0) {
        free(wideStr);
        return NULL;
    }

    char* gbkStr = (char*)malloc(lenA);
    if (!gbkStr) {
        free(wideStr);
        return NULL;
    }

    WideCharToMultiByte(CP_ACP, 0, wideStr, -1, gbkStr, lenA, NULL, NULL);

    free(wideStr);
    return gbkStr;
}

char* GBKToUTF8(const char* gbkStr) {
    if (!gbkStr) return NULL;
    int lenW = MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, NULL, 0);
    if (lenW == 0) return NULL;

    wchar_t* wideStr = (wchar_t*)malloc(lenW * sizeof(wchar_t));
    if (!wideStr) return NULL;

    MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, wideStr, lenW);

    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wStr, -1, NULL, 0, NULL, NULL);
    if (utf8Len == 0) {
        free(wideStr);
        return NULL;
    }

    char* utf8Str = (char*)malloc(utf8Len);
    if (!utf8Str) {
        free(wideStr);
        return NULL;
    }

    WideCharToMultiByte(CP_UTF8, 0, wStr, -1, utf8Str, utf8Len, NULL, NULL);

    free(wStr);
    return utf8Str;
}

char* URLDecode(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str);
    char* decoded = (char*)malloc(len + 1);
    if (!decoded) return NULL;

    size_t i, j = 0;
    for (i = 0; i < len; i++) {
        if (str[i] == '%') {
            if (i + 2 < len) {
                char hex[3] = {str[i+1], str[i+2], '\0'};
                char* endptr;
                long val = strtol(hex, &endptr, 16);
                if (*endptr == '\0') {
                    decoded[j++] = (char)val;
                    i += 2;
                } else {
                    // Invalid hex code, treat as literal character
                    decoded[j++] = str[i];
                }
            } else {
                decoded[j++] = str[i];
            }
        } else if (str[i] == '+') {
            decoded[j++] = ' ';
        } else {
            decoded[j++] = str[i];
        }
    }
    decoded[j] = '\0';
    return decoded;
}

BOOL is_base64_encoded(const char* data) {
    // 检查是否包含非 Base64 字符
    if (!data) return FALSE;
    
    for (const char* p = data; *p; p++) {
        if (isspace(*p)) continue;
        if (!isalnum(*p) && *p != '+' && *p != '/' && *p != '=' && *p != '-' && *p != '_') {
            return FALSE;
        }
    }
    
    // 尝试解码标准 Base64，如果成功，则认为是 Base64
    DWORD dwLen = 0;
    if (CryptStringToBinaryA(data, 0, CRYPT_STRING_BASE64, NULL, &dwLen, NULL, NULL)) {
        return TRUE;
    }
    // 尝试解码 URL-safe Base64
    if (CryptStringToBinaryA(data, 0, CRYPT_STRING_BASE64URL, NULL, &dwLen, NULL, NULL)) {
        return TRUE;
    }

    return FALSE;
}

char* base64_decode(const char* input, size_t* out_len) {
    if (!input || !out_len) return NULL;
    
    DWORD dwDestLen = 0;
    
    // 尝试标准 Base64 解码，获取所需大小
    if (!CryptStringToBinaryA(input, 0, CRYPT_STRING_BASE64, NULL, &dwDestLen, NULL, NULL)) {
        // 尝试 URL-safe Base64 解码
        if (!CryptStringToBinaryA(input, 0, CRYPT_STRING_BASE64URL, NULL, &dwDestLen, NULL, NULL)) {
            *out_len = 0;
            return NULL;
        }
    }

    // 分配缓冲区
    BYTE* pbDest = (BYTE*)malloc(dwDestLen + 1); 
    if (!pbDest) {
        *out_len = 0;
        return NULL;
    }

    // 再次尝试解码并写入缓冲区
    if (CryptStringToBinaryA(input, 0, CRYPT_STRING_BASE64, pbDest, &dwDestLen, NULL, NULL) ||
        CryptStringToBinaryA(input, 0, CRYPT_STRING_BASE64URL, pbDest, &dwDestLen, NULL, NULL)) {
        pbDest[dwDestLen] = '\0'; 
        *out_len = dwDestLen;
        return (char*)pbDest;
    }

    free(pbDest);
    *out_len = 0;
    return NULL;
}

// 占位函数：检查文件是否为 UTF8
BOOL IsUTF8File(const char* fileName) {
    // 假设 Base64 解码后的内容通常是 UTF-8
    return TRUE; 
}
