#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SINGLE_INSTANCE_MUTEX_NAME "ECHWorkerClient_Mutex_Unique_ID"
#define IDI_APP_ICON 101 

typedef BOOL (WINAPI *SetProcessDPIAwareFunc)(void);

#define APP_VERSION "1.5" // 版本号升级
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
int g_totalNodeCount = 0; // 全局变量：用于多订阅合并计算节点总数

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

HWND hMainWindow;
HWND hConfigNameEdit, hServerEdit, hListenEdit, hTokenEdit, hIpEdit, hDnsEdit, hEchEdit;
HWND hStartBtn, hStopBtn, hLogEdit, hSaveConfigBtn, hLoadConfigBtn;
HWND hSubscribeUrlEdit, hFetchSubBtn, hNodeList;
HWND hAddSubBtn, hDelSubBtn, hSubList; // 新增句柄

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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void CreateControls(HWND hwnd);
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

// 订阅相关函数更新
void FetchAllSubscriptions(); // 更新所有订阅
void ProcessSingleSubscription(const char* url); // 处理单个URL
void ParseSubscriptionData(const char* data); // 解析数据(不重置列表)
void AddSubscription();
void DelSubscription();
void SaveSubscriptionList();
void LoadSubscriptionList();

void SaveNodeConfig(int nodeIndex);
void LoadNodeList();
void SaveNodeList();
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart);
char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
BOOL IsUTF8File(const char* fileName);
char* base64_decode(const char* input, size_t* out_len);
BOOL is_base64_encoded(const char* data);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    
    HANDLE hMutex = CreateMutex(NULL, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (hMutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExistingWnd = FindWindow("ECHWorkerClient", NULL); 
        if (hExistingWnd) {
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

    // 稍微增加窗口高度以容纳新控件
    int winWidth = Scale(1000);
    int winHeight = Scale(750); 
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    // [Modified] Use WS_OVERLAPPEDWINDOW to allow resizing and maximizing
    DWORD winStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;

     // DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

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

void InitTrayIcon(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            LoadConfig();
            LoadSubscriptionList(); // 加载订阅列表
            SetControlValues();
            LoadNodeList();
            break;

        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_MINIMIZE) {
                ShowWindow(hwnd, SW_HIDE); 
                ShowTrayIcon();            
                return 0;                  
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                RemoveTrayIcon();
            } 
            else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                if (hMenu) {
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, "打开界面");
                    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "退出程序");
                    SetForegroundWindow(hwnd); 
                    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                    DestroyMenu(hMenu);
                }
            }
            break;

        case WM_APPEND_LOG: {
            char* logText = (char*)lParam;
            if (logText) {
                AppendLog(logText);
                free(logText);
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl = (HWND)lParam;
            int ctrlId = GetDlgCtrlID(hCtrl);
            if (ctrlId == ID_LOG_EDIT) {
                SetBkColor(hdcStatic, RGB(255, 255, 255)); 
                SetBkMode(hdcStatic, OPAQUE);              
                return (LRESULT)hBrushLog;                 
            }
            SetBkMode(hdcStatic, TRANSPARENT);             
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_TRAY_OPEN:
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                    RemoveTrayIcon();
                    break;
                
                case ID_TRAY_EXIT:
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;

                case ID_START_BTN:
                    if (!isProcessRunning) {
                        GetControlValues();
                        if (strlen(currentConfig.server) == 0) {
                            MessageBox(hwnd, "请输入服务地址", "提示", MB_OK | MB_ICONWARNING);
                            SetFocus(hServerEdit);
                            break;
                        }
                        if (strlen(currentConfig.listen) == 0) {
                            MessageBox(hwnd, "请输入监听地址 (127.0.0.1:...)", "提示", MB_OK | MB_ICONWARNING);
                            SetFocus(hListenEdit);
                            break;
                        }
                        SaveConfig();
                        StartProcess();
                    }
                    break;

                case ID_STOP_BTN:
                    if (isProcessRunning) StopProcess();
                    break;

                case ID_CLEAR_LOG_BTN:
                    SetWindowText(hLogEdit, "");
                    break;

                case ID_SAVE_CONFIG_BTN:
                    GetControlValues();
                    SaveConfigToFile();
                    break;

                case ID_LOAD_CONFIG_BTN:
                    LoadConfigFromFile();
                    SetControlValues();
                    break;
                
                // 新增按钮事件
                case ID_ADD_SUB_BTN:
                    AddSubscription();
                    break;

                case ID_DEL_SUB_BTN:
                    DelSubscription();
                    break;

                case ID_FETCH_SUB_BTN:
                    FetchAllSubscriptions();
                    break;

                case ID_NODE_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            LoadNodeConfigByIndex(sel, FALSE);
                        }
                    } else if (HIWORD(wParam) == LBN_DBLCLK) {
                        int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            LoadNodeConfigByIndex(sel, TRUE);
                        }
                    }
                    break;
            }
            break;

        case WM_CLOSE:
            if (isProcessRunning) StopProcess();
            RemoveTrayIcon();
            GetControlValues();
            SaveConfig();
            DestroyWindow(hwnd);
            break;

        case WM_DESTROY:
            RemoveTrayIcon();
            if (hFontUI) DeleteObject(hFontUI);
            if (hFontLog) DeleteObject(hFontLog);
            if (hBrushLog) DeleteObject(hBrushLog);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void CreateLabelAndEdit(HWND parent, const char* labelText, int x, int y, int w, int h, int editId, HWND* outEdit, BOOL numberOnly) {
    HWND hStatic = CreateWindow("STATIC", labelText, WS_VISIBLE | WS_CHILD | SS_LEFT, 
        x, y + Scale(3), Scale(140), Scale(20), parent, NULL, NULL, NULL);
    SendMessage(hStatic, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    DWORD style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (numberOnly) style |= ES_NUMBER | ES_CENTER;

    *outEdit = CreateWindow("EDIT", "", style, 
        x + Scale(150), y, w - Scale(150), h, parent, (HMENU)(intptr_t)editId, NULL, NULL);
    SendMessage(*outEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(*outEdit, EM_SETLIMITTEXT, (editId == ID_SERVER_EDIT || editId == ID_TOKEN_EDIT || editId == ID_SUBSCRIBE_URL_EDIT) ? MAX_URL_LEN : MAX_SMALL_LEN, 0);
}

void CreateControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int winW = rect.right;
    int margin = Scale(20);
    int groupW = winW - (margin * 2);
    int lineHeight = Scale(22);
    int lineGap = Scale(10);
    int editH = Scale(20);
    int curY = margin;

    // ----- 订阅管理区域重构 -----
    int groupSubH = Scale(280); // 增加高度以容纳列表
    HWND hGroupSub = CreateWindow("BUTTON", "订阅管理", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, groupSubH, hwnd, NULL, NULL, NULL);
    SendMessage(hGroupSub, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(25);
    
    // 1. 输入框行：Label + Edit + 添加按钮
    HWND hSubLabel = CreateWindow("STATIC", "订阅链接:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hSubLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hSubscribeUrlEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 
        margin + Scale(100), innerY, groupW - Scale(200), editH, hwnd, (HMENU)ID_SUBSCRIBE_URL_EDIT, NULL, NULL);
    SendMessage(hSubscribeUrlEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);

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
    
    innerY += Scale(35);

    // 4. 节点列表
    HWND hNodeLabel = CreateWindow("STATIC", "节点列表(单击查看/双击启用):", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(200), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        margin + Scale(15), innerY + Scale(25), groupW - Scale(30), Scale(90), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += groupSubH + Scale(15);
    int group1H = Scale(100);
    HWND hGroup1 = CreateWindow("BUTTON", "核心配置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY = curY + Scale(25);
    HWND hConfigLabel = CreateWindow("STATIC", "配置名称:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConfigLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hConfigNameEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + Scale(105), innerY, Scale(200), editH, hwnd, (HMENU)ID_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hConfigNameEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hConfigNameEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);

    innerY += lineHeight + lineGap;
    CreateLabelAndEdit(hwnd, "服务地址:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_SERVER_EDIT, &hServerEdit, FALSE);
    innerY += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "监听地址:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_LISTEN_EDIT, &hListenEdit, FALSE);

    curY += group1H + Scale(15);

    int group2H = Scale(155);
    HWND hGroup2 = CreateWindow("BUTTON", "高级选项 (可选)", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group2H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup2, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    innerY = curY + Scale(25);

    CreateLabelAndEdit(hwnd, "身份令牌:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_TOKEN_EDIT, &hTokenEdit, FALSE);
    innerY += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "优选IP(域名):", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_IP_EDIT, &hIpEdit, FALSE);
    innerY += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "ECH域名:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_ECH_EDIT, &hEchEdit, FALSE);
    innerY += lineHeight + lineGap;

    CreateLabelAndEdit(hwnd, "DNS服务器(仅域名):", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_DNS_EDIT, &hDnsEdit, FALSE);

    curY += group2H + Scale(15);

    int btnW = Scale(120);
    int btnH = Scale(38);
    int btnGap = Scale(20);
    int startX = margin;

    hStartBtn = CreateWindow("BUTTON", "启动代理", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        startX, curY, btnW, btnH, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "停止", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + btnW + btnGap, curY, btnW, btnH, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    hSaveConfigBtn = CreateWindow("BUTTON", "保存配置", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 2, curY, btnW, btnH, hwnd, (HMENU)ID_SAVE_CONFIG_BTN, NULL, NULL);
    SendMessage(hSaveConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hLoadConfigBtn = CreateWindow("BUTTON", "加载配置", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 3, curY, btnW, btnH, hwnd, (HMENU)ID_LOAD_CONFIG_BTN, NULL, NULL);
    SendMessage(hLoadConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        rect.right - margin - btnW, curY, btnW, btnH, hwnd, (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH + Scale(15);

    HWND hLogLabel = CreateWindow("STATIC", "运行日志:", WS_VISIBLE | WS_CHILD, 
        margin, curY, Scale(100), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hLogLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    curY += Scale(25);

    hLogEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 
        margin, curY, winW - (margin * 2), Scale(60), hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}

// ============ 订阅列表文件管理 ============

void SaveSubscriptionList() {
    FILE* f = fopen("subscriptions.txt", "w");
    if (!f) return;
    
    int count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char url[MAX_URL_LEN];
        int len = SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        if (len > 0) {
            fprintf(f, "%s\n", url);
        }
    }
    fclose(f);
}

void LoadSubscriptionList() {
    FILE* f = fopen("subscriptions.txt", "r");
    if (!f) return;
    
    SendMessage(hSubList, LB_RESETCONTENT, 0, 0);
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = 0;
        }
        if (len > 0) {
            SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)line);
        }
    }
    fclose(f);
}

void AddSubscription() {
    char url[MAX_URL_LEN];
    GetWindowText(hSubscribeUrlEdit, url, sizeof(url));
    if (strlen(url) == 0) return;
    
    // 检查是否重复
    if (SendMessage(hSubList, LB_FINDSTRINGEXACT, -1, (LPARAM)url) != LB_ERR) {
        MessageBox(hMainWindow, "该订阅链接已存在", "提示", MB_OK);
        return;
    }
    
    SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
    SetWindowText(hSubscribeUrlEdit, ""); // 清空输入框
    SaveSubscriptionList();
}

void DelSubscription() {
    int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    
    SendMessage(hSubList, LB_DELETESTRING, sel, 0);
    SaveSubscriptionList();
}

// ============ 第二部分：进程管理、配置保存加载、编码转换 ============

void GetControlValues() {
    char buf[MAX_URL_LEN];
    GetWindowText(hConfigNameEdit, currentConfig.configName, sizeof(currentConfig.configName));
    GetWindowText(hServerEdit, buf, sizeof(buf));
    strcpy(currentConfig.server, buf);

    GetWindowText(hListenEdit, buf, sizeof(buf));
    strcpy(currentConfig.listen, buf);

    GetWindowText(hTokenEdit, currentConfig.token, sizeof(currentConfig.token));
    GetWindowText(hIpEdit, currentConfig.ip, sizeof(currentConfig.ip));
    GetWindowText(hDnsEdit, currentConfig.dns, sizeof(currentConfig.dns));
    GetWindowText(hEchEdit, currentConfig.ech, sizeof(currentConfig.ech));
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

void StartProcess() {
    char cmdLine[MAX_CMD_LEN];
    char exePath[MAX_PATH] = "ech-workers.exe";
    
    if (GetFileAttributes(exePath) == INVALID_FILE_ATTRIBUTES) {
        AppendLog("错误: 找不到 ech-workers.exe 文件!\r\n");
        return;
    }
    
    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\"", exePath);
    
    // 添加 -f 参数（服务地址）
    if (strlen(currentConfig.server) > 0) {
        strcat(cmdLine, " -f ");
        strcat(cmdLine, currentConfig.server);
    }
    
    // 添加 -l 参数（监听地址），去掉 proxy:// 前缀
    if (strlen(currentConfig.listen) > 0) {
        char listenAddr[MAX_SMALL_LEN];
        strcpy(listenAddr, currentConfig.listen);
        
        // 去掉 socks5:// 或 proxy:// 前缀
        char* actualAddr = listenAddr;
        if (strncmp(listenAddr, "socks5://", 9) == 0) {
            actualAddr = listenAddr + 9;
        } else if (strncmp(listenAddr, "proxy://", 8) == 0) {
            actualAddr = listenAddr + 8;
        } else if (strncmp(listenAddr, "http://", 7) == 0) {
            actualAddr = listenAddr + 7;
        }
        
        strcat(cmdLine, " -l ");
        strcat(cmdLine, actualAddr);
    }
    
    // 添加 -token 参数
    if (strlen(currentConfig.token) > 0) {
        strcat(cmdLine, " -token ");
        strcat(cmdLine, currentConfig.token);
    }
    
    // 添加 -ip 参数（优选IP）
    if (strlen(currentConfig.ip) > 0) {
        strcat(cmdLine, " -ip ");
        strcat(cmdLine, currentConfig.ip);
    }
    
    // 添加 -dns 参数（如果不是默认值）
    if (strlen(currentConfig.dns) > 0 && strcmp(currentConfig.dns, "dns.alidns.com/dns-query") != 0) {
        strcat(cmdLine, " -dns ");
        strcat(cmdLine, currentConfig.dns);
    }
    
    // 检测IP格式DNS，自动添加 -insecure-dns
    if (strlen(currentConfig.dns) > 0) {
        char* firstChar = currentConfig.dns;
        if (*firstChar >= '0' && *firstChar <= '9') {
            strcat(cmdLine, " -insecure-dns");
            AppendLog("[提示] 检测到IP格式DNS,已自动跳过TLS证书验证\r\n");
        }
    }
    
    // 添加 -ech 参数（如果不是默认值）
    if (strlen(currentConfig.ech) > 0 && strcmp(currentConfig.ech, "cloudflare-ech.com") != 0) {
        strcat(cmdLine, " -ech ");
        strcat(cmdLine, currentConfig.ech);
    }

    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return;

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.wShowWindow = SW_HIDE;

    if (CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, 0, NULL, NULL, &si, &processInfo)) {
        CloseHandle(hWrite);
        hLogPipe = hRead;
        hLogThread = CreateThread(NULL, 0, LogReaderThread, NULL, 0, NULL);
        isProcessRunning = TRUE;
        EnableWindow(hStartBtn, FALSE);
        EnableWindow(hStopBtn, TRUE);
        EnableWindow(hServerEdit, FALSE);
        EnableWindow(hListenEdit, FALSE);
        AppendLog("[系统] 进程已启动...\r\n");
    } else {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        AppendLog("[错误] 启动失败,请检查配置。\r\n");
    }
}
void StopProcess() {
    isProcessRunning = FALSE;

    if (hLogPipe) {
        CloseHandle(hLogPipe);
        hLogPipe = NULL;
    }

    if (processInfo.hProcess) {
        TerminateProcess(processInfo.hProcess, 0);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        processInfo.hProcess = NULL;
    }

    if (hLogThread) {
        if (WaitForSingleObject(hLogThread, 500) == WAIT_TIMEOUT) {
            TerminateThread(hLogThread, 0);
        }
        CloseHandle(hLogThread);
        hLogThread = NULL;
    }
    
    if (IsWindow(hMainWindow)) {
        EnableWindow(hStartBtn, TRUE);
        EnableWindow(hStopBtn, FALSE);
        EnableWindow(hServerEdit, TRUE);
        EnableWindow(hListenEdit, TRUE);
        AppendLog("[系统] 进程已停止。\r\n");
    }
}

void AppendLogAsync(const char* text) {
    if (!text) return;
    char* msgCopy = strdup(text); 
    if (msgCopy) {
        if (!PostMessage(hMainWindow, WM_APPEND_LOG, 0, (LPARAM)msgCopy)) {
            free(msgCopy);
        }
    }
}

DWORD WINAPI LogReaderThread(LPVOID lpParam) {
    (void)lpParam;
    char buf[1024];
    DWORD read;
    
    while (isProcessRunning && hLogPipe) {
        if (ReadFile(hLogPipe, buf, sizeof(buf)-1, &read, NULL) && read > 0) {
            buf[read] = 0;
            
            // Go 程序输出的是 UTF-8，需要转换为 GBK 显示
            char* gbkText = UTF8ToGBK(buf);
            if (gbkText) {
                AppendLogAsync(gbkText);
                free(gbkText);
            } else {
                AppendLogAsync(buf);
            }
        } else {
            break; 
        }
    }
    return 0;
}
void AppendLog(const char* text) {
    if (!IsWindow(hLogEdit)) return;
    int len = GetWindowTextLength(hLogEdit);
    SendMessage(hLogEdit, EM_SETSEL, len, len);
    SendMessage(hLogEdit, EM_REPLACESEL, FALSE, (LPARAM)text);
}

void SaveConfig() {
    FILE* f = fopen("config.ini", "w");
    if (!f) return;
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\n",
        currentConfig.configName, currentConfig.server, currentConfig.listen, currentConfig.token, 
        currentConfig.ip, currentConfig.dns, currentConfig.ech);
    fclose(f);
}

void LoadConfig() {
    FILE* f = fopen("config.ini", "r");
    if (!f) return;
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;

        if (!strcmp(line, "configName")) strcpy(currentConfig.configName, val);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, val);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, val);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, val);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, val);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, val);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, val);
    }
    fclose(f);
}

void SaveConfigToFile() {
    if (strlen(currentConfig.configName) == 0) {
        MessageBox(hMainWindow, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    char safeName[240];
    strncpy(safeName, currentConfig.configName, sizeof(safeName) - 1);
    safeName[sizeof(safeName) - 1] = '\0';
    
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "%s.ini", safeName);
    
    FILE* f = fopen(fileName, "w");
    if (!f) {
        MessageBox(hMainWindow, "保存配置失败", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\n",
        safeName, currentConfig.server, currentConfig.listen, currentConfig.token, 
        currentConfig.ip, currentConfig.dns, currentConfig.ech);
    fclose(f);
    
    char msg[512];
    snprintf(msg, sizeof(msg), "配置已保存到: %s", fileName);
    MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
    AppendLog("[配置] 已保存配置文件\r\n");
}

void LoadConfigFromFile() {
    OPENFILENAME ofn;
    char fileName[MAX_PATH] = "";
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFilter = "配置文件 (*.ini)\0*.ini\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "ini";
    
    if (!GetOpenFileName(&ofn)) {
        return;
    }
    
    FILE* f = fopen(fileName, "r");
    if (!f) {
        MessageBox(hMainWindow, "无法打开配置文件", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        if (val[strlen(val)-1] == '\n') val[strlen(val)-1] = 0;

        if (!strcmp(line, "configName")) strcpy(currentConfig.configName, val);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, val);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, val);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, val);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, val);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, val);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, val);
    }
    fclose(f);
    
    BOOL wasRunning = isProcessRunning;
    
    if (wasRunning) {
        AppendLog("[配置] 检测到进程运行中,正在停止...\r\n");
        StopProcess();
        Sleep(500);
    }
    
    MessageBox(hMainWindow, "配置已加载", "成功", MB_OK | MB_ICONINFORMATION);
    AppendLog("[配置] 已加载配置文件\r\n");
    
    if (wasRunning) {
        AppendLog("[配置] 正在使用新配置重启进程...\r\n");
        Sleep(200);
        StartProcess();
    }
}

// UTF-8 转 GBK
char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str || strlen(utf8Str) == 0) return strdup("");
    
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (wideLen == 0) return strdup(utf8Str);
    
    wchar_t* wideStr = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (!wideStr) return strdup(utf8Str);
    
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideLen);
    
    int gbkLen = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (gbkLen == 0) {
        free(wideStr);
        return strdup(utf8Str);
    }
    
    char* gbkStr = (char*)malloc(gbkLen);
    if (!gbkStr) {
        free(wideStr);
        return strdup(utf8Str);
    }
    
    WideCharToMultiByte(CP_ACP, 0, wideStr, -1, gbkStr, gbkLen, NULL, NULL);
    free(wideStr);
    
    return gbkStr;
}

// GBK 转 UTF-8
char* GBKToUTF8(const char* gbkStr) {
    if (!gbkStr || strlen(gbkStr) == 0) return strdup("");
    
    int wideLen = MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, NULL, 0);
    if (wideLen == 0) return strdup(gbkStr);
    
    wchar_t* wideStr = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (!wideStr) return strdup(gbkStr);
    
    MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, wideStr, wideLen);
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (utf8Len == 0) {
        free(wideStr);
        return strdup(gbkStr);
    }
    
    char* utf8Str = (char*)malloc(utf8Len);
    if (!utf8Str) {
        free(wideStr);
        return strdup(gbkStr);
    }
    
    WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, utf8Str, utf8Len, NULL, NULL);
    free(wideStr);
    
    return utf8Str;
}

// URL 解码
char* URLDecode(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* decoded = (char*)malloc(len + 1);
    if (!decoded) return NULL;
    
    size_t i = 0, j = 0;
    while (i < len) {
        if (str[i] == '%' && i + 2 < len) {
            char hex[3] = {str[i+1], str[i+2], 0};
            decoded[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
        } else if (str[i] == '+') {
            decoded[j++] = ' ';
            i++;
        } else {
            decoded[j++] = str[i++];
        }
    }
    decoded[j] = '\0';
    
    return decoded;
}

// 检测文件编码
BOOL IsUTF8File(const char* fileName) {
    FILE* f = fopen(fileName, "rb");
    if (!f) return FALSE;
    
    unsigned char bom[3];
    size_t read = fread(bom, 1, 3, f);
    fclose(f);
    
    if (read == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
        return TRUE;
    }
    
    return FALSE;
}

// 保存节点配置
void SaveNodeConfig(int nodeIndex) {
    CreateDirectory("nodes", NULL);
    
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    
    FILE* f = fopen(fileName, "wb");
    if (!f) return;
    
    fputc(0xEF, f);
    fputc(0xBB, f);
    fputc(0xBF, f);
    
    char* utf8ConfigName = GBKToUTF8(currentConfig.configName);
    char* utf8Server = GBKToUTF8(currentConfig.server);
    char* utf8Token = GBKToUTF8(currentConfig.token);
    char* utf8Ip = GBKToUTF8(currentConfig.ip);
    char* utf8Dns = GBKToUTF8(currentConfig.dns);
    char* utf8Ech = GBKToUTF8(currentConfig.ech);
    
    fprintf(f, "[ECHTunnel]\r\n");
    fprintf(f, "configName=%s\r\n", utf8ConfigName ? utf8ConfigName : currentConfig.configName);
    fprintf(f, "server=%s\r\n", utf8Server ? utf8Server : currentConfig.server);
    fprintf(f, "listen=%s\r\n", currentConfig.listen);
    fprintf(f, "token=%s\r\n", utf8Token ? utf8Token : currentConfig.token);
    fprintf(f, "ip=%s\r\n", utf8Ip ? utf8Ip : currentConfig.ip);
    fprintf(f, "dns=%s\r\n", utf8Dns ? utf8Dns : currentConfig.dns);
    fprintf(f, "ech=%s\r\n", utf8Ech ? utf8Ech : currentConfig.ech);
    
    if (utf8ConfigName) free(utf8ConfigName);
    if (utf8Server) free(utf8Server);
    if (utf8Token) free(utf8Token);
    if (utf8Ip) free(utf8Ip);
    if (utf8Dns) free(utf8Dns);
    if (utf8Ech) free(utf8Ech);
    
    fclose(f);
}

// 加载节点配置
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart) {
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    
    BOOL isUTF8 = IsUTF8File(fileName);
    FILE* f = fopen(fileName, isUTF8 ? "rb" : "r");
    if (!f) {
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[节点] 配置文件不存在: %s\r\n", fileName);
        AppendLog(logMsg);
        return;
    }
    
    if (isUTF8) {
        fseek(f, 3, SEEK_SET);
    }
    
    char line[MAX_URL_LEN];
    while (fgets(line, sizeof(line), f)) {
        char* val = strchr(line, '=');
        if (!val) continue;
        *val++ = 0;
        
        size_t valLen = strlen(val);
        while (valLen > 0 && (val[valLen-1] == '\n' || val[valLen-1] == '\r')) {
            val[--valLen] = 0;
        }
        
        char* displayValue = val;
        char* convertedValue = NULL;
        if (isUTF8) {
            convertedValue = UTF8ToGBK(val);
            if (convertedValue) {
                displayValue = convertedValue;
            }
        }

        if (!strcmp(line, "configName")) strcpy(currentConfig.configName, displayValue);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, displayValue);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, displayValue);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, displayValue);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, displayValue);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, displayValue);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, displayValue);
        
        if (convertedValue) free(convertedValue);
    }
    fclose(f);
    
    SetControlValues();
    
    if (autoStart) {
        if (isProcessRunning) {
            char logMsg[512];
            snprintf(logMsg, sizeof(logMsg), "[节点] 正在切换到: %s\r\n", currentConfig.configName);
            AppendLog(logMsg);
            AppendLog("[节点] 停止当前进程...\r\n");
            StopProcess();
            Sleep(500);
            AppendLog("[节点] 启动新节点...\r\n");
            StartProcess();
        } else {
            char logMsg[512];
            snprintf(logMsg, sizeof(logMsg), "[节点] 启动节点: %s\r\n", currentConfig.configName);
            AppendLog(logMsg);
            StartProcess();
        }
    } else {
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[节点] 查看配置: %s\r\n", currentConfig.configName);
        AppendLog(logMsg);
    }
}

// 保存节点列表
void SaveNodeList() {
    CreateDirectory("nodes", NULL);
    
    FILE* f = fopen("nodes/nodelist.txt", "wb");
    if (!f) return;
    
    fputc(0xEF, f);
    fputc(0xBB, f);
    fputc(0xBF, f);
    
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char nodeName[MAX_SMALL_LEN];
        SendMessage(hNodeList, LB_GETTEXT, i, (LPARAM)nodeName);
        
        char* utf8Name = GBKToUTF8(nodeName);
        if (utf8Name) {
            fprintf(f, "%s\r\n", utf8Name);
            free(utf8Name);
        } else {
            fprintf(f, "%s\r\n", nodeName);
        }
    }
    fclose(f);
}

// 加载节点列表
void LoadNodeList() {
    BOOL isUTF8 = IsUTF8File("nodes/nodelist.txt");
    FILE* f = fopen("nodes/nodelist.txt", isUTF8 ? "rb" : "r");
    if (!f) return;
    
    if (isUTF8) {
        fseek(f, 3, SEEK_SET);
    }
    
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    
    char line[MAX_SMALL_LEN];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = 0;
        }
        
        if (len > 0) {
            if (isUTF8) {
                char* gbkName = UTF8ToGBK(line);
                if (gbkName) {
                    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)gbkName);
                    free(gbkName);
                } else {
                    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)line);
                }
            } else {
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)line);
            }
        }
    }
    fclose(f);
    
    int nodeCount = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    if (nodeCount > 0) {
        char logMsg[256];
        snprintf(logMsg, sizeof(logMsg), "[订阅] 已加载 %d 个节点\r\n", nodeCount);
        AppendLog(logMsg);
    }
}
// ============ 第三部分：Base64解码、订阅解析、网络获取 ============

// Base64 解码函数
char* base64_decode(const char* input, size_t* out_len) {
    static const unsigned char base64_table[256] = {
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 62, 64, 64, 64, 63,
        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 64, 64, 64, 64, 64, 64,
        64,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64, 64,
        64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64,
        64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64, 64
    };
    
    size_t in_len = strlen(input);
    if (in_len == 0) return NULL;
    
    size_t padding = 0;
    if (input[in_len - 1] == '=') padding++;
    if (in_len > 1 && input[in_len - 2] == '=') padding++;
    
    size_t output_len = (in_len / 4) * 3 - padding;
    char* output = (char*)malloc(output_len + 1);
    if (!output) return NULL;
    
    size_t j = 0;
    unsigned char block[4];
    size_t block_pos = 0;
    
    for (size_t i = 0; i < in_len; i++) {
        unsigned char c = (unsigned char)input[i];
        if (c == '=' || base64_table[c] == 64) {
            if (c != '=' && c != '\r' && c != '\n' && c != ' ') {
                free(output);
                return NULL;
            }
            continue;
        }
        
        block[block_pos++] = base64_table[c];
        
        if (block_pos == 4) {
            output[j++] = (block[0] << 2) | (block[1] >> 4);
            if (j < output_len) output[j++] = (block[1] << 4) | (block[2] >> 2);
            if (j < output_len) output[j++] = (block[2] << 6) | block[3];
            block_pos = 0;
        }
    }
    
    output[output_len] = '\0';
    if (out_len) *out_len = output_len;
    return output;
}

// 检查字符串是否为 Base64 编码
BOOL is_base64_encoded(const char* data) {
    if (!data || strlen(data) == 0) return FALSE;
    
    if (strstr(data, "ech://") || strstr(data, "ECH://") || 
        strstr(data, "\r\n") || strstr(data, "\n")) {
        return FALSE;
    }
    
    size_t len = strlen(data);
    size_t valid_chars = 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=') {
            valid_chars++;
        } else if (c != '\r' && c != '\n' && c != ' ') {
            return FALSE;
        }
    }
    
    return (valid_chars * 100 / len) > 90;
}

// 解析订阅数据 (修改版：累加节点，不清除列表)
void ParseSubscriptionData(const char* data) {
    if (!data || strlen(data) == 0) {
        return;
    }
    
    char* dataCopy = NULL;
    if (is_base64_encoded(data)) {
        size_t decoded_len = 0;
        dataCopy = base64_decode(data, &decoded_len);
        if (!dataCopy) {
            AppendLog("[订阅] Base64解码失败\r\n");
            return;
        }
    } else {
        dataCopy = strdup(data);
        if (!dataCopy) return;
    }
    
    char* line = strtok(dataCopy, "\r\n");
    int newNodesCount = 0;
    
    while (line != NULL) {
        if (strlen(line) > 0 && line[0] != ';' && strncmp(line, "//", 2) != 0) {
            if (strncmp(line, "ech://", 6) != 0 && strncmp(line, "ECH://", 6) != 0) {
                line = strtok(NULL, "\r\n");
                continue;
            }
            
            char nodeName[MAX_SMALL_LEN] = {0};
            char server[MAX_URL_LEN] = {0};
            char token[MAX_URL_LEN] = {0};
            char ip[MAX_SMALL_LEN] = {0};
            char dns[MAX_SMALL_LEN] = {0};
            char ech[MAX_SMALL_LEN] = {0};
            
            // 分离节点名称和参数部分，# 后面到行尾都是节点名称
            char* nameStart = strchr(line, '#');
            if (nameStart) {
                char* urlDecoded = URLDecode(nameStart + 1);
                if (urlDecoded) {
                    char* gbkName = UTF8ToGBK(urlDecoded);
                    if (gbkName) {
                        strncpy(nodeName, gbkName, MAX_SMALL_LEN - 1);
                        nodeName[MAX_SMALL_LEN - 1] = '\0';
                        free(gbkName);
                    }
                    free(urlDecoded);
                }
                *nameStart = '\0';
            }
            
            // 解析参数部分 ech://server|token|ip|dns|ech
            char* p = line;
            if (strncmp(p, "ech://", 6) == 0 || strncmp(p, "ECH://", 6) == 0) {
                p += 6;
            }
            
            int partIndex = 0;
            char* start = p;
            
            while (*p) {
                if (*p == '|' || *(p + 1) == '\0') {
                    size_t len = (*p == '|') ? (size_t)(p - start) : (size_t)(p - start + 1);
                    
                    if (partIndex == 0 && len > 0 && len < MAX_URL_LEN) {
                        strncpy(server, start, len);
                        server[len] = '\0';
                    } else if (partIndex == 1 && len > 0 && len < MAX_URL_LEN) {
                        strncpy(token, start, len);
                        token[len] = '\0';
                    } else if (partIndex == 2 && len > 0 && len < MAX_SMALL_LEN) {
                        strncpy(ip, start, len);
                        ip[len] = '\0';
                    } else if (partIndex == 3 && len > 0 && len < MAX_SMALL_LEN) {
                        strncpy(dns, start, len);
                        dns[len] = '\0';
                    } else if (partIndex == 4 && len > 0 && len < MAX_SMALL_LEN) {
                        strncpy(ech, start, len);
                        ech[len] = '\0';
                    }
                    
                    if (*p == '|') {
                        partIndex++;
                        start = p + 1;
                    }
                }
                p++;
            }
            
            if (partIndex == 0 && strlen(server) == 0) {
                strncpy(server, start, MAX_URL_LEN - 1);
                server[MAX_URL_LEN - 1] = '\0';
            }
            
            // 如果没有节点名称，使用服务器地址
            if (strlen(nodeName) == 0 && strlen(server) > 0) {
                char* colonPos = strchr(server, ':');
                if (colonPos) {
                    size_t hostLen = (size_t)(colonPos - server);
                    if (hostLen > 0 && hostLen < MAX_SMALL_LEN) {
                        strncpy(nodeName, server, hostLen);
                        nodeName[hostLen] = '\0';
                    }
                } else {
                    strncpy(nodeName, server, MAX_SMALL_LEN - 1);
                    nodeName[MAX_SMALL_LEN - 1] = '\0';
                }
            }
            
            if (strlen(nodeName) > 0 && strlen(server) > 0) {
                strcpy(currentConfig.configName, nodeName);
                strcpy(currentConfig.server, server);
                strcpy(currentConfig.token, token);
                strcpy(currentConfig.ip, ip);
                
                if (strlen(dns) == 0) {
                    strcpy(currentConfig.dns, "dns.alidns.com/dns-query");
                } else {
                    strcpy(currentConfig.dns, dns);
                }
                
                if (strlen(ech) == 0) {
                    strcpy(currentConfig.ech, "cloudflare-ech.com");
                } else {
                    strcpy(currentConfig.ech, ech);
                }
                
                // 使用全局计数器保存
                SaveNodeConfig(g_totalNodeCount);
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nodeName);
                g_totalNodeCount++;
                newNodesCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    
    free(dataCopy);
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 解析得到 %d 个节点\r\n", newNodesCount);
    AppendLog(logMsg);
}

// 处理单个订阅URL
void ProcessSingleSubscription(const char* url) {
    if (strlen(url) == 0) return;
    
    char logMsg[MAX_URL_LEN + 30];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 获取: %s\r\n", url);
    AppendLog(logMsg);
    
    HINTERNET hInternet = InternetOpen("ECHWorkerClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return;
    
    HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, 
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    
    if (!hConnect) {
        AppendLog("[订阅] 连接失败\r\n");
        InternetCloseHandle(hInternet);
        return;
    }
    
    // 动态分配大缓冲区，防止溢出
    size_t bufSize = 1024 * 1024; // 1MB
    char* buffer = (char*)malloc(bufSize);
    if (!buffer) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return;
    }
    
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    char tempBuf[4096];
    
    buffer[0] = 0;
    
    while (InternetReadFile(hConnect, tempBuf, sizeof(tempBuf) - 1, &bytesRead) && bytesRead > 0) {
        tempBuf[bytesRead] = 0;
        if (totalRead + bytesRead < bufSize - 1) {
            strcat(buffer, tempBuf);
            totalRead += bytesRead;
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

// 获取所有订阅 (重构入口函数)
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
    
    // 3. (可选) 清理 nodes 文件夹下的旧文件，防止残留
    // 这里简单处理：重写时会覆盖同名文件，但如果新节点数少于旧节点数，会有残留。
    // 生产环境建议遍历删除 nodes/*.ini，这里略过以保持代码简洁。
    
    // 4. 遍历所有订阅链接
    for (int i = 0; i < subCount; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        ProcessSingleSubscription(url);
    }
    
    // 5. 保存新的节点列表文件
    SaveNodeList();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "更新完成，共获取 %d 个节点", g_totalNodeCount);
    MessageBox(hMainWindow, msg, "订阅成功", MB_OK | MB_ICONINFORMATION);
    AppendLog("[订阅] 全部更新完成\r\n");
    AppendLog("--------------------------\r\n");
}
