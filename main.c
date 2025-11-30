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

#define APP_VERSION "1.1"
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

int Scale(int x) {
    return (x * g_scale) / 100;
}

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
#define ID_SUBSCRIBE_URL_EDIT 1016
#define ID_FETCH_SUB_BTN    1017
#define ID_NODE_LIST        1018

HWND hMainWindow;
HWND hConfigNameEdit, hServerEdit, hListenEdit, hTokenEdit, hIpEdit, hDnsEdit, hEchEdit;
HWND hStartBtn, hStopBtn, hLogEdit, hSaveConfigBtn, hLoadConfigBtn;
HWND hSubscribeUrlEdit, hFetchSubBtn, hNodeList;
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
void FetchSubscription();
void ParseSubscriptionData(const char* data);
void SaveNodeConfig(const char* nodeName);

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

    int winWidth = Scale(900);
    int winHeight = Scale(800);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    DWORD winStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

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
            SetControlValues();
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

                case ID_FETCH_SUB_BTN:
                    FetchSubscription();
                    break;

                case ID_NODE_LIST:
                    if (HIWORD(wParam) == LBN_DBLCLK) {
                        int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            char nodeName[MAX_SMALL_LEN];
                            SendMessage(hNodeList, LB_GETTEXT, sel, (LPARAM)nodeName);
                            
                            // 加载对应的配置文件
                            char fileName[MAX_PATH];
                            snprintf(fileName, MAX_PATH, "nodes/%s.ini", nodeName);
                            
                            FILE* f = fopen(fileName, "r");
                            if (f) {
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
                                SetControlValues();
                                AppendLog("[订阅] 已加载节点配置\r\n");
                            }
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
    int lineHeight = Scale(30);
    int lineGap = Scale(10);
    int editH = Scale(26);
    int curY = margin;

    // 订阅功能组
    int groupSubH = Scale(140);
    HWND hGroupSub = CreateWindow("BUTTON", "订阅管理", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, groupSubH, hwnd, NULL, NULL, NULL);
    SendMessage(hGroupSub, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(25);
    CreateLabelAndEdit(hwnd, "订阅链接:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_SUBSCRIBE_URL_EDIT, &hSubscribeUrlEdit, FALSE);
    
    innerY += lineHeight + lineGap;
    
    hFetchSubBtn = CreateWindow("BUTTON", "获取订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(165), innerY, Scale(120), Scale(32), hwnd, (HMENU)ID_FETCH_SUB_BTN, NULL, NULL);
    SendMessage(hFetchSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    HWND hNodeLabel = CreateWindow("STATIC", "节点列表(双击加载):", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(40), Scale(140), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        margin + Scale(165), innerY + Scale(37), groupW - Scale(180), Scale(50), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += groupSubH + Scale(15);

    // 配置名称
    HWND hConfigLabel = CreateWindow("STATIC", "配置名称:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin, curY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConfigLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hConfigNameEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + Scale(90), curY, Scale(200), editH, hwnd, (HMENU)ID_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hConfigNameEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hConfigNameEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);

    curY += lineHeight + Scale(5);

    int group1H = Scale(80);
    HWND hGroup1 = CreateWindow("BUTTON", "核心配置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY = curY + Scale(25);

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
        margin, curY, winW - (margin * 2), Scale(150), hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}

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
    
    #define APPEND_ARG(flag, val) if(strlen(val) > 0) { \
        strcat(cmdLine, " " flag " \""); \
        strcat(cmdLine, val); \
        strcat(cmdLine, "\""); \
    }

    APPEND_ARG("-f", currentConfig.server);
    APPEND_ARG("-l", currentConfig.listen);
    
    if (strlen(currentConfig.token) > 0) {
        APPEND_ARG("-token", currentConfig.token);
    }
    
    if (strlen(currentConfig.ip) > 0) {
        APPEND_ARG("-ip", currentConfig.ip);
    }
    
    if (strlen(currentConfig.dns) > 0 && strcmp(currentConfig.dns, "dns.alidns.com/dns-query") != 0) {
        APPEND_ARG("-dns", currentConfig.dns);
    }
    
    if (strlen(currentConfig.dns) > 0) {
        char* firstChar = currentConfig.dns;
        if (*firstChar >= '0' && *firstChar <= '9') {
            strcat(cmdLine, " -insecure-dns");
            AppendLog("[提示] 检测到IP格式DNS,已自动跳过TLS证书验证\r\n");
        }
    }
    
    if (strlen(currentConfig.ech) > 0 && strcmp(currentConfig.ech, "cloudflare-ech.com") != 0) {
        APPEND_ARG("-ech", currentConfig.ech);
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
    char u8Buf[2048];
    DWORD read;
    
    while (isProcessRunning && hLogPipe) {
        if (ReadFile(hLogPipe, buf, sizeof(buf)-1, &read, NULL) && read > 0) {
            buf[read] = 0;
            int wLen = MultiByteToWideChar(CP_UTF8, 0, buf, -1, NULL, 0);
            if (wLen > 0) {
                WCHAR* wBuf = (WCHAR*)malloc(wLen * sizeof(WCHAR));
                if (wBuf) {
                    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wBuf, wLen);
                    WideCharToMultiByte(CP_ACP, 0, wBuf, -1, u8Buf, sizeof(u8Buf), NULL, NULL);
                    AppendLogAsync(u8Buf);
                    free(wBuf);
                }
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
    char fileName[MAX_PATH];
    if (strlen(currentConfig.configName) == 0) {
        MessageBox(hMainWindow, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    snprintf(fileName, MAX_PATH, "%s.ini", currentConfig.configName);
    
    FILE* f = fopen(fileName, "w");
    if (!f) {
        MessageBox(hMainWindow, "保存配置失败", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\n",
        currentConfig.configName, currentConfig.server, currentConfig.listen, currentConfig.token, 
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

void ParseSubscriptionData(const char* data) {
    if (!data || strlen(data) == 0) {
        AppendLog("[订阅] 订阅数据为空\r\n");
        return;
    }
    
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    
    char* dataCopy = strdup(data);
    if (!dataCopy) return;
    
    char* line = strtok(dataCopy, "\r\n");
    int nodeCount = 0;
    
    while (line != NULL) {
        // 跳过空行和注释
        if (strlen(line) > 0 && line[0] != ';' && strncmp(line, "//", 2) != 0) {
            // 格式: ech://服务地址|token|优选IP|dns|ech域名#节点名称
            char nodeName[MAX_SMALL_LEN] = {0};
            char server[MAX_URL_LEN] = {0};
            char token[MAX_URL_LEN] = {0};
            char ip[MAX_SMALL_LEN] = {0};
            char dns[MAX_SMALL_LEN] = {0};
            char ech[MAX_SMALL_LEN] = {0};
            
            // 先提取节点名称（#后面的部分）
            char* nameStart = strchr(line, '#');
            if (nameStart) {
                strncpy(nodeName, nameStart + 1, MAX_SMALL_LEN - 1);
                nodeName[MAX_SMALL_LEN - 1] = '\0';
                *nameStart = '\0';
            }
            
            // 检查是否以 ech:// 开头
            char* p = line;
            if (strncmp(p, "ech://", 6) == 0) {
                p += 6;
            }
            
            // 解析各个字段
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
            
            // 如果没有|分隔符，整个作为server
            if (partIndex == 0 && strlen(server) == 0) {
                strncpy(server, start, MAX_URL_LEN - 1);
                server[MAX_URL_LEN - 1] = '\0';
            }
            
            // 如果节点名称为空，使用服务地址作为名称
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
            
            // 保存节点配置
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
                
                SaveNodeConfig(nodeName);
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nodeName);
                nodeCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    
    free(dataCopy);
    
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 成功解析 %d 个节点\r\n", nodeCount);
    AppendLog(logMsg);
    
    if (nodeCount > 0) {
        MessageBox(hMainWindow, "订阅获取成功", "成功", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBox(hMainWindow, "未找到有效节点", "提示", MB_OK | MB_ICONWARNING);
    }
}

void FetchSubscription() {
    char url[MAX_URL_LEN];
    GetWindowText(hSubscribeUrlEdit, url, sizeof(url));
    
    if (strlen(url) == 0) {
        MessageBox(hMainWindow, "请输入订阅链接", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    AppendLog("[订阅] 正在获取订阅...\r\n");
    
    HINTERNET hInternet = InternetOpen("ECHWorkerClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        AppendLog("[订阅] 初始化网络失败\r\n");
        return;
    }
    
    HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, 
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    
    if (!hConnect) {
        AppendLog("[订阅] 连接订阅地址失败\r\n");
        InternetCloseHandle(hInternet);
        return;
    }
    
    char* buffer = (char*)malloc(1024 * 1024); // 1MB buffer
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
        if (totalRead + bytesRead < 1024 * 1024 - 1) {
            strcat(buffer, tempBuf);
            totalRead += bytesRead;
        }
    }
    
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    
    if (totalRead > 0) {
        ParseSubscriptionData(buffer);
    } else {
        AppendLog("[订阅] 获取订阅数据失败\r\n");
        MessageBox(hMainWindow, "获取订阅失败", "错误", MB_OK | MB_ICONERROR);
    }
    
    free(buffer);
}
