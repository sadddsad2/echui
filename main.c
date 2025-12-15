// ============ 第一部分:头文件、宏定义、全局变量、函数声明 ============
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

#define APP_VERSION "2.0"
#define APP_TITLE "ECH 客户端 v" APP_VERSION

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
int g_manualNodeCount = 0;

int Scale(int x) {
    return (x * g_scale) / 100;
}

// 节点类型枚举
typedef enum {
    NODE_TYPE_ECH = 0,   // ech-tunnel.exe
    NODE_TYPE_ECHW = 1   // ech-workers.exe
} NodeType;

// 控件ID定义
#define ID_CONFIG_NAME_EDIT 1000
#define ID_NODE_TYPE_COMBO  1001
#define ID_SERVER_EDIT      1002
#define ID_LISTEN_EDIT      1003
#define ID_TOKEN_EDIT       1004
#define ID_IP_EDIT          1005
#define ID_DNS_EDIT         1006
#define ID_ECH_EDIT         1007
#define ID_CONN_EDIT        1008
#define ID_CONN_UP          1009
#define ID_CONN_DOWN        1010
#define ID_FALLBACK_CHECK   1011
#define ID_START_BTN        1012
#define ID_STOP_BTN         1013
#define ID_CLEAR_LOG_BTN    1014
#define ID_LOG_EDIT         1015
#define ID_SAVE_CONFIG_BTN  1016
#define ID_SUBSCRIBE_URL_EDIT 1017
#define ID_FETCH_SUB_BTN    1018
#define ID_NODE_LIST        1019
#define ID_ADD_SUB_BTN      1020
#define ID_DEL_SUB_BTN      1021
#define ID_SUB_LIST         1022
#define ID_DEL_NODE_BTN     1023

HWND hMainWindow;
HWND hConfigNameEdit, hNodeTypeCombo, hServerEdit, hListenEdit, hTokenEdit;
HWND hIpEdit, hDnsEdit, hEchEdit, hConnEdit;
HWND hFallbackCheck;
HWND hStartBtn, hStopBtn, hLogEdit, hSaveConfigBtn;
HWND hSubscribeUrlEdit, hFetchSubBtn, hNodeList;
HWND hAddSubBtn, hDelSubBtn, hSubList;
HWND hDelNodeBtn;

PROCESS_INFORMATION processInfo;
HANDLE hLogPipe = NULL;
HANDLE hLogThread = NULL;
BOOL isProcessRunning = FALSE;
NOTIFYICONDATA nid;

typedef struct {
    char configName[MAX_SMALL_LEN];
    NodeType nodeType;           // 节点类型
    char dns[MAX_SMALL_LEN];     
    char ech[MAX_SMALL_LEN];     
    char server[MAX_URL_LEN];    
    char ip[MAX_SMALL_LEN];      
    char listen[MAX_SMALL_LEN];
    int connections;             // 并发连接数(仅ECH类型)
    int fallback;                // 回退模式(仅ECH类型)
    char token[MAX_URL_LEN];     
} Config;

Config currentConfig = {
    "默认配置", NODE_TYPE_ECHW, 
    "dns.alidns.com/dns-query", "cloudflare-ech.com", 
    "example.com:443", "", "127.0.0.1:30000", 
    3, 0, ""
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

void DelSelectedNode();
void SaveNodeConfig(int nodeIndex, BOOL isManual);
void LoadNodeList();
void SaveNodeList();
void SaveManualNodeList();
void LoadManualNodeList();
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart);

void OnNodeTypeChanged();
void UpdateControlsForNodeType(NodeType type);

char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
BOOL IsUTF8File(const char* fileName);
char* base64_decode(const char* input, size_t* out_len);
BOOL is_base64_encoded(const char* data);

// ============ 第二部分:主函数和托盘图标 ============

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

    int winWidth = Scale(1000);
    int winHeight = Scale(750); 
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

// ============ 第三部分:窗口过程 ============

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            LoadConfig();
            LoadSubscriptionList();
            LoadNodeList();
            LoadManualNodeList();
            SetControlValues();
            UpdateControlsForNodeType(currentConfig.nodeType);
            break;

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) break;
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            int winW = rect.right;
            int winH = rect.bottom;
            int margin = Scale(20);
            
            // 重新布局所有控件
            HDWP hdwp = BeginDeferWindowPos(30);
            
            // 订阅管理区域保持固定高度
            int groupSubH = Scale(280);
            HWND hGroupSub = GetDlgItem(hwnd, 5000);
            if (hGroupSub) {
                DeferWindowPos(hdwp, hGroupSub, NULL, margin, margin, 
                    winW - margin * 2, groupSubH, SWP_NOZORDER);
            }
            
            // 配置区域
            int curY = margin + groupSubH + Scale(15);
            int group1H = Scale(220);
            HWND hGroup1 = GetDlgItem(hwnd, 5001);
            if (hGroup1) {
                DeferWindowPos(hdwp, hGroup1, NULL, margin, curY, 
                    winW - margin * 2, group1H, SWP_NOZORDER);
            }
            
            // 按钮栏
            curY += group1H + Scale(15);
            int btnH = Scale(38);
            HWND hBtn;
            if ((hBtn = GetDlgItem(hwnd, ID_CLEAR_LOG_BTN)) != NULL) {
                RECT btnRect;
                GetWindowRect(hBtn, &btnRect);
                int btnW = btnRect.right - btnRect.left;
                DeferWindowPos(hdwp, hBtn, NULL, winW - margin - btnW, curY, 
                    0, 0, SWP_NOSIZE | SWP_NOZORDER);
            }
            
            // 日志区域填充剩余空间
            curY += btnH + Scale(15);
            int logLabelH = Scale(25);
            curY += logLabelH;
            int logH = winH - curY - margin;
            if (logH < Scale(80)) logH = Scale(80);
            
            if (hLogEdit) {
                DeferWindowPos(hdwp, hLogEdit, NULL, margin, curY, 
                    winW - margin * 2, logH, SWP_NOZORDER);
            }
            
            EndDeferWindowPos(hdwp);
            InvalidateRect(hwnd, NULL, TRUE);
            break;
        }

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

                case ID_NODE_TYPE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        OnNodeTypeChanged();
                    }
                    break;

                case ID_FALLBACK_CHECK: {
                    BOOL checked = (SendMessage(hFallbackCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    EnableWindow(hDnsEdit, !checked);
                    EnableWindow(hEchEdit, !checked);
                    break;
                }

                case ID_CONN_UP: {
                    char buf[16];
                    GetWindowText(hConnEdit, buf, 16);
                    int val = atoi(buf);
                    if (val < 20) {
                        sprintf(buf, "%d", val + 1);
                        SetWindowText(hConnEdit, buf);
                    }
                    break;
                }

                case ID_CONN_DOWN: {
                    char buf[16];
                    GetWindowText(hConnEdit, buf, 16);
                    int val = atoi(buf);
                    if (val > 1) {
                        sprintf(buf, "%d", val - 1);
                        SetWindowText(hConnEdit, buf);
                    }
                    break;
                }

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
                
                case ID_ADD_SUB_BTN:
                    AddSubscription();
                    break;

                case ID_DEL_SUB_BTN:
                    DelSubscription();
                    break;

                case ID_FETCH_SUB_BTN:
                    FetchAllSubscriptions();
                    break;

                case ID_DEL_NODE_BTN:
                    DelSelectedNode();
                    break;

                case ID_SUB_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            char url[MAX_URL_LEN];
                            SendMessage(hSubList, LB_GETTEXT, sel, (LPARAM)url);
                            SetWindowText(hSubscribeUrlEdit, url);
                        }
                    }
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

// ============ 第四部分:控件创建 ============

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

    // 订阅管理区域
    int groupSubH = Scale(280); 
    HWND hGroupSub = CreateWindow("BUTTON", "订阅管理", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, groupSubH, hwnd, (HMENU)5000, NULL, NULL);
    SendMessage(hGroupSub, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(25);
    
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

    hSubList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        margin + Scale(15), innerY, groupW - Scale(30), Scale(60), hwnd, (HMENU)ID_SUB_LIST, NULL, NULL);
    SendMessage(hSubList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY += Scale(60) + Scale(5);

    hDelSubBtn = CreateWindow("BUTTON", "删除选中订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(15), innerY, Scale(120), Scale(30), hwnd, (HMENU)ID_DEL_SUB_BTN, NULL, NULL);
    SendMessage(hDelSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hFetchSubBtn = CreateWindow("BUTTON", "更新所有订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(150), innerY, Scale(120), Scale(30), hwnd, (HMENU)ID_FETCH_SUB_BTN, NULL, NULL);
    SendMessage(hFetchSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY += Scale(35);

    HWND hNodeLabel = CreateWindow("STATIC", "节点列表(单击查看/双击启用):", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(200), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hDelNodeBtn = CreateWindow("BUTTON", "删除选中节点", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + groupW - Scale(110), innerY, Scale(100), Scale(20), hwnd, (HMENU)ID_DEL_NODE_BTN, NULL, NULL);
    SendMessage(hDelNodeBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        margin + Scale(15), innerY + Scale(25), groupW - Scale(30), Scale(90), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += groupSubH + Scale(15);
    
    // 配置区域
    int group1H = Scale(220);
    HWND hGroup1 = CreateWindow("BUTTON", "配置", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, (HMENU)5001, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY = curY + Scale(25);
    
    int splitGap = Scale(15);
    int halfGroupW = (groupW - splitGap) / 2;
    
    // 配置名称
    HWND hConfigLabel = CreateWindow("STATIC", "配置名称:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(15), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConfigLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hConfigNameEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + Scale(105), innerY, Scale(200), editH, hwnd, (HMENU)ID_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hConfigNameEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hConfigNameEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);

    // 节点类型
    HWND hTypeLabel = CreateWindow("STATIC", "节点类型:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin + Scale(320), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hTypeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hNodeTypeCombo = CreateWindow("COMBOBOX", "", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        margin + Scale(400), innerY, Scale(150), Scale(200), 
        hwnd, (HMENU)ID_NODE_TYPE_COMBO, NULL, NULL);
    SendMessage(hNodeTypeCombo, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hNodeTypeCombo, CB_ADDSTRING, 0, (LPARAM)"ECH (ech-tunnel)");
    SendMessage(hNodeTypeCombo, CB_ADDSTRING, 0, (LPARAM)"ECHW (ech-workers)");
    SendMessage(hNodeTypeCombo, CB_SETCURSEL, 0, 0);

    innerY += lineHeight + lineGap;
    
    // 服务地址
    CreateLabelAndEdit(hwnd, "服务地址:", margin + Scale(15), innerY, halfGroupW - Scale(30), editH, ID_SERVER_EDIT, &hServerEdit, FALSE);
    
    // 监听地址
    int rightX = margin + halfGroupW + splitGap;
    CreateLabelAndEdit(hwnd, "监听地址:", rightX + Scale(15), innerY, halfGroupW - Scale(30), editH, ID_LISTEN_EDIT, &hListenEdit, FALSE);
    innerY += lineHeight + lineGap;

    // 身份令牌
    CreateLabelAndEdit(hwnd, "身份令牌:", margin + Scale(15), innerY, groupW - Scale(30), editH, ID_TOKEN_EDIT, &hTokenEdit, FALSE);
    innerY += lineHeight + lineGap;

    // 优选IP
    CreateLabelAndEdit(hwnd, "优选IP(域名):", margin + Scale(15), innerY, halfGroupW - Scale(30), editH, ID_IP_EDIT, &hIpEdit, FALSE);
    
    // 并发连接(仅ECH类型)
    HWND hConnLabel = CreateWindow("STATIC", "并发连接:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        rightX + Scale(15), innerY + Scale(3), Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConnLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int btnSize = Scale(26);
    int numW = Scale(50);
    int numX = rightX + Scale(100);

    hConnEdit = CreateWindow("EDIT", "3", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER | ES_CENTER, 
        numX, innerY, numW, editH, hwnd, (HMENU)ID_CONN_EDIT, NULL, NULL);
    SendMessage(hConnEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnDown = CreateWindow("BUTTON", "-", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 
        numX + numW + Scale(5), innerY, btnSize, editH, hwnd, (HMENU)ID_CONN_DOWN, NULL, NULL);
    SendMessage(hBtnDown, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hBtnUp = CreateWindow("BUTTON", "+", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 
        numX + numW + Scale(5) + btnSize + Scale(5), innerY, btnSize, editH, hwnd, (HMENU)ID_CONN_UP, NULL, NULL);
    SendMessage(hBtnUp, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY += lineHeight + lineGap;
    
    // DNS服务器
    CreateLabelAndEdit(hwnd, "DNS服务器(仅域名):", margin + Scale(15), innerY, halfGroupW - Scale(30), editH, ID_DNS_EDIT, &hDnsEdit, FALSE);
    
    // ECH域名
    CreateLabelAndEdit(hwnd, "ECH域名:", rightX + Scale(15), innerY, halfGroupW - Scale(30), editH, ID_ECH_EDIT, &hEchEdit, FALSE);
    
    innerY += lineHeight + lineGap;
    
    // 回退模式复选框(仅ECH类型)
    hFallbackCheck = CreateWindow("BUTTON", "禁用ECH (回退到普通TLS 1.3)", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        margin + Scale(15), innerY + Scale(2), Scale(300), Scale(22), 
        hwnd, (HMENU)ID_FALLBACK_CHECK, NULL, NULL);
    SendMessage(hFallbackCheck, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += group1H + Scale(15);

    // 按钮栏
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

    hSaveConfigBtn = CreateWindow("BUTTON", "添加当前节点到列表", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 2, curY, Scale(150), btnH, hwnd, (HMENU)ID_SAVE_CONFIG_BTN, NULL, NULL);
    SendMessage(hSaveConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        winW - margin - btnW, curY, btnW, btnH, hwnd, (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH + Scale(15);

    HWND hLogLabel = CreateWindow("STATIC", "运行日志:", WS_VISIBLE | WS_CHILD, 
        margin, curY, Scale(100), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hLogLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    curY += Scale(25);

    hLogEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 
        margin, curY, winW - (margin * 2), Scale(80), hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}

// ============ 第五部分:节点类型管理和进程控制 ============

void OnNodeTypeChanged() {
    int sel = SendMessage(hNodeTypeCombo, CB_GETCURSEL, 0, 0);
    if (sel != CB_ERR) {
        currentConfig.nodeType = (NodeType)sel;
        UpdateControlsForNodeType(currentConfig.nodeType);
    }
}

void UpdateControlsForNodeType(NodeType type) {
    if (type == NODE_TYPE_ECH) {
        // ECH类型:显示并发连接、回退模式
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_EDIT), SW_SHOW);
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_UP), SW_SHOW);
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_DOWN), SW_SHOW);
        ShowWindow(hFallbackCheck, SW_SHOW);
        
        // 根据回退模式决定DNS和ECH是否可用
        BOOL fallback = (SendMessage(hFallbackCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
        EnableWindow(hDnsEdit, !fallback);
        EnableWindow(hEchEdit, !fallback);
        
        // 更新提示文本
        char serverHint[256];
        strcpy(serverHint, "wss://example.com:443");
        SetWindowText(hServerEdit, strlen(currentConfig.server) == 0 ? serverHint : currentConfig.server);
        
        char listenHint[256];
        strcpy(listenHint, "127.0.0.1:30000");
        if (strlen(currentConfig.listen) == 0) {
            SetWindowText(hListenEdit, listenHint);
        }
    } else {
        // ECHW类型:隐藏并发连接、回退模式
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_EDIT), SW_HIDE);
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_UP), SW_HIDE);
        ShowWindow(GetDlgItem(hMainWindow, ID_CONN_DOWN), SW_HIDE);
        ShowWindow(hFallbackCheck, SW_HIDE);
        
        // DNS和ECH始终可用
        EnableWindow(hDnsEdit, TRUE);
        EnableWindow(hEchEdit, TRUE);
        
        // 更新提示文本
        char serverHint[256];
        strcpy(serverHint, "example.com:443");
        SetWindowText(hServerEdit, strlen(currentConfig.server) == 0 ? serverHint : currentConfig.server);
        
        char listenHint[256];
        strcpy(listenHint, "127.0.0.1:30000");
        if (strlen(currentConfig.listen) == 0) {
            SetWindowText(hListenEdit, listenHint);
        }
    }
}

void GetControlValues() {
    char buf[MAX_URL_LEN];
    GetWindowText(hConfigNameEdit, currentConfig.configName, sizeof(currentConfig.configName));
    
    int typeIdx = SendMessage(hNodeTypeCombo, CB_GETCURSEL, 0, 0);
    currentConfig.nodeType = (typeIdx != CB_ERR) ? (NodeType)typeIdx : NODE_TYPE_ECHW;
    
    GetWindowText(hServerEdit, buf, sizeof(buf));
    strcpy(currentConfig.server, buf);

    GetWindowText(hListenEdit, buf, sizeof(buf));
    strcpy(currentConfig.listen, buf);

    GetWindowText(hTokenEdit, currentConfig.token, sizeof(currentConfig.token));
    GetWindowText(hIpEdit, currentConfig.ip, sizeof(currentConfig.ip));
    GetWindowText(hDnsEdit, currentConfig.dns, sizeof(currentConfig.dns));
    GetWindowText(hEchEdit, currentConfig.ech, sizeof(currentConfig.ech));
    
    // 仅ECH类型读取这些参数
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        char connBuf[32];
        GetWindowText(hConnEdit, connBuf, 32);
        currentConfig.connections = atoi(connBuf);
        if (currentConfig.connections < 1) currentConfig.connections = 1;
        
        currentConfig.fallback = (SendMessage(hFallbackCheck, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
    }
}

void SetControlValues() {
    SetWindowText(hConfigNameEdit, currentConfig.configName);
    SendMessage(hNodeTypeCombo, CB_SETCURSEL, currentConfig.nodeType, 0);
    SetWindowText(hServerEdit, currentConfig.server);
    SetWindowText(hListenEdit, currentConfig.listen);
    SetWindowText(hTokenEdit, currentConfig.token);
    SetWindowText(hIpEdit, currentConfig.ip);
    SetWindowText(hDnsEdit, currentConfig.dns);
    SetWindowText(hEchEdit, currentConfig.ech);
    
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        char connBuf[32];
        sprintf(connBuf, "%d", currentConfig.connections);
        SetWindowText(hConnEdit, connBuf);
        
        SendMessage(hFallbackCheck, BM_SETCHECK, currentConfig.fallback ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    
    UpdateControlsForNodeType(currentConfig.nodeType);
}

void StartProcess() {
    char cmdLine[MAX_CMD_LEN];
    char exePath[MAX_PATH];
    
    // 根据节点类型选择不同的exe
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        strcpy(exePath, "ech-tunnel.exe");
    } else {
        strcpy(exePath, "ech-workers.exe");
    }
    
    if (GetFileAttributes(exePath) == INVALID_FILE_ATTRIBUTES) {
        char errMsg[512];
        snprintf(errMsg, sizeof(errMsg), "错误: 找不到 %s 文件!\r\n", exePath);
        AppendLog(errMsg);
        return;
    }
    
    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\"", exePath);
    
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        // ========== ECH类型 (ech-tunnel.exe) ==========
        // 处理服务地址 (自动添加wss://)
        if (strlen(currentConfig.server) > 0) {
            char serverAddr[MAX_URL_LEN];
            if (strncmp(currentConfig.server, "wss://", 6) != 0) {
                snprintf(serverAddr, sizeof(serverAddr), "wss://%s", currentConfig.server);
            } else {
                strcpy(serverAddr, currentConfig.server);
            }
            strcat(cmdLine, " -f \"");
            strcat(cmdLine, serverAddr);
            strcat(cmdLine, "\"");
        }
        
        // 处理监听地址 (自动添加proxy://)
        if (strlen(currentConfig.listen) > 0) {
            char listenAddr[MAX_SMALL_LEN];
            if (strncmp(currentConfig.listen, "proxy://", 8) != 0 &&
                strncmp(currentConfig.listen, "socks5://", 9) != 0 &&
                strncmp(currentConfig.listen, "http://", 7) != 0) {
                snprintf(listenAddr, sizeof(listenAddr), "proxy://%s", currentConfig.listen);
            } else {
                strcpy(listenAddr, currentConfig.listen);
            }
            strcat(cmdLine, " -l \"");
            strcat(cmdLine, listenAddr);
            strcat(cmdLine, "\"");
        }
        
        if (strlen(currentConfig.token) > 0) {
            strcat(cmdLine, " -token \"");
            strcat(cmdLine, currentConfig.token);
            strcat(cmdLine, "\"");
        }
        
        if (strlen(currentConfig.ip) > 0) {
            strcat(cmdLine, " -ip \"");
            strcat(cmdLine, currentConfig.ip);
            strcat(cmdLine, "\"");
        }
        
        // 回退模式
        if (currentConfig.fallback) {
            strcat(cmdLine, " -fallback");
        } else {
            if (strlen(currentConfig.dns) > 0 && strcmp(currentConfig.dns, "dns.alidns.com/dns-query") != 0) {
                strcat(cmdLine, " -dns \"");
                strcat(cmdLine, currentConfig.dns);
                strcat(cmdLine, "\"");
            }
            if (strlen(currentConfig.ech) > 0 && strcmp(currentConfig.ech, "cloudflare-ech.com") != 0) {
                strcat(cmdLine, " -ech \"");
                strcat(cmdLine, currentConfig.ech);
                strcat(cmdLine, "\"");
            }
        }
        
        // 并发连接数
        if (currentConfig.connections != 3) {
            char nBuf[32]; 
            sprintf(nBuf, " -n %d", currentConfig.connections);
            strcat(cmdLine, nBuf);
        }
        
    } else {
        // ========== ECHW类型 (ech-workers.exe) ==========
        if (strlen(currentConfig.server) > 0) {
            strcat(cmdLine, " -f ");
            strcat(cmdLine, currentConfig.server);
        }
        
        // 处理监听地址 (移除协议前缀)
        if (strlen(currentConfig.listen) > 0) {
            char listenAddr[MAX_SMALL_LEN];
            strcpy(listenAddr, currentConfig.listen);
            
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
        
        if (strlen(currentConfig.token) > 0) {
            strcat(cmdLine, " -token ");
            strcat(cmdLine, currentConfig.token);
        }
        
        if (strlen(currentConfig.ip) > 0) {
            strcat(cmdLine, " -ip ");
            strcat(cmdLine, currentConfig.ip);
        }
        
        if (strlen(currentConfig.dns) > 0 && strcmp(currentConfig.dns, "dns.alidns.com/dns-query") != 0) {
            strcat(cmdLine, " -dns ");
            strcat(cmdLine, currentConfig.dns);
        }
        
        // 检测DNS是否为IP格式,自动添加 -insecure-dns
        if (strlen(currentConfig.dns) > 0) {
            char* firstChar = currentConfig.dns;
            if (*firstChar >= '0' && *firstChar <= '9') {
                strcat(cmdLine, " -insecure-dns");
                AppendLog("[提示] 检测到IP格式DNS,已自动跳过TLS证书验证\r\n");
            }
        }
        
        if (strlen(currentConfig.ech) > 0 && strcmp(currentConfig.ech, "cloudflare-ech.com") != 0) {
            strcat(cmdLine, " -ech ");
            strcat(cmdLine, currentConfig.ech);
        }
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
        EnableWindow(hNodeTypeCombo, FALSE);
        
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[系统] 已启动 %s 模式\r\n", 
            currentConfig.nodeType == NODE_TYPE_ECH ? "ECH" : "ECHW");
        AppendLog(logMsg);
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
        EnableWindow(hNodeTypeCombo, TRUE);
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
// ============ 第六部分: 配置持久化与节点管理 ============

void SaveConfig() {
    FILE* f = fopen("config.ini", "w");
    if (!f) return;
    fprintf(f, "[ECHTunnel]\n");
    fprintf(f, "configName=%s\n", currentConfig.configName);
    fprintf(f, "nodeType=%d\n", currentConfig.nodeType); // 保存节点类型
    fprintf(f, "server=%s\n", currentConfig.server);
    fprintf(f, "listen=%s\n", currentConfig.listen);
    fprintf(f, "token=%s\n", currentConfig.token);
    fprintf(f, "ip=%s\n", currentConfig.ip);
    fprintf(f, "dns=%s\n", currentConfig.dns);
    fprintf(f, "ech=%s\n", currentConfig.ech);
    fprintf(f, "connections=%d\n", currentConfig.connections); // 保存并发数
    fprintf(f, "fallback=%d\n", currentConfig.fallback);       // 保存回退模式
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
        else if (!strcmp(line, "nodeType")) currentConfig.nodeType = (NodeType)atoi(val);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, val);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, val);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, val);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, val);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, val);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, val);
        else if (!strcmp(line, "connections")) currentConfig.connections = atoi(val);
        else if (!strcmp(line, "fallback")) currentConfig.fallback = atoi(val);
    }
    fclose(f);
    
    // 确保值有效
    if (currentConfig.connections < 1) currentConfig.connections = 3;
}

void SaveConfigToFile() {
    if (strlen(currentConfig.configName) == 0) {
        MessageBox(hMainWindow, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    // 保存为手动节点
    CreateDirectory("manual_nodes", NULL);
    
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", g_manualNodeCount);
    
    // 保存配置(复用逻辑)
    SaveNodeConfig(g_manualNodeCount, TRUE);
    
    // 添加到节点列表
    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)currentConfig.configName);
    g_manualNodeCount++;
    SaveManualNodeList();
    
    char msg[512];
    snprintf(msg, sizeof(msg), "配置已保存并添加到节点列表");
    MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
    AppendLog("[配置] 已保存配置并添加到节点列表\r\n");
}

// 删除选中的节点
void DelSelectedNode() {
    int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
        MessageBox(hMainWindow, "请先选择要删除的节点", "提示", MB_OK);
        return;
    }
    
    char nodeName[MAX_SMALL_LEN];
    SendMessage(hNodeList, LB_GETTEXT, sel, (LPARAM)nodeName);
    
    char msg[512];
    snprintf(msg, sizeof(msg), "确定要删除节点 '%s' 吗?", nodeName);
    if (MessageBox(hMainWindow, msg, "确认删除", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    
    // 删除文件
    BOOL foundFile = FALSE;
    char fileName[MAX_PATH];
    
    // 先尝试在订阅节点中查找
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", sel);
    if (GetFileAttributes(fileName) != INVALID_FILE_ATTRIBUTES) {
        DeleteFile(fileName);
        foundFile = TRUE;
    }
    
    // 再尝试在手动节点中查找
    if (!foundFile) {
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", sel - g_totalNodeCount);
        if (GetFileAttributes(fileName) != INVALID_FILE_ATTRIBUTES) {
            DeleteFile(fileName);
            foundFile = TRUE;
        }
    }
    
    SendMessage(hNodeList, LB_DELETESTRING, sel, 0);
    
    if (sel < g_totalNodeCount) {
        g_totalNodeCount--;
        SaveNodeList(); // 更新订阅列表文件
    } else {
        g_manualNodeCount--;
        SaveManualNodeList(); // 更新手动列表文件
    }
    
    AppendLog("[节点] 已删除节点\r\n");
}

// 保存节点配置到文件
void SaveNodeConfig(int nodeIndex, BOOL isManual) {
    if (isManual) CreateDirectory("manual_nodes", NULL);
    else CreateDirectory("nodes", NULL);
    
    char fileName[MAX_PATH];
    if (isManual) snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", nodeIndex);
    else snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    
    FILE* f = fopen(fileName, "wb");
    if (!f) return;
    
    fputc(0xEF, f); fputc(0xBB, f); fputc(0xBF, f); // UTF-8 BOM
    
    char* utf8ConfigName = GBKToUTF8(currentConfig.configName);
    char* utf8Server = GBKToUTF8(currentConfig.server);
    char* utf8Token = GBKToUTF8(currentConfig.token);
    char* utf8Ip = GBKToUTF8(currentConfig.ip);
    char* utf8Dns = GBKToUTF8(currentConfig.dns);
    char* utf8Ech = GBKToUTF8(currentConfig.ech);
    
    fprintf(f, "[ECHTunnel]\r\n");
    fprintf(f, "configName=%s\r\n", utf8ConfigName ? utf8ConfigName : currentConfig.configName);
    fprintf(f, "nodeType=%d\r\n", currentConfig.nodeType);
    fprintf(f, "server=%s\r\n", utf8Server ? utf8Server : currentConfig.server);
    fprintf(f, "listen=%s\r\n", currentConfig.listen);
    fprintf(f, "token=%s\r\n", utf8Token ? utf8Token : currentConfig.token);
    fprintf(f, "ip=%s\r\n", utf8Ip ? utf8Ip : currentConfig.ip);
    fprintf(f, "dns=%s\r\n", utf8Dns ? utf8Dns : currentConfig.dns);
    fprintf(f, "ech=%s\r\n", utf8Ech ? utf8Ech : currentConfig.ech);
    fprintf(f, "connections=%d\r\n", currentConfig.connections);
    fprintf(f, "fallback=%d\r\n", currentConfig.fallback);
    
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
    
    // 先尝试订阅节点
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    BOOL isUTF8 = IsUTF8File(fileName);
    FILE* f = fopen(fileName, isUTF8 ? "rb" : "r");
    
    // 如果订阅节点不存在,尝试手动节点
    if (!f) {
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", nodeIndex - g_totalNodeCount);
        isUTF8 = IsUTF8File(fileName);
        f = fopen(fileName, isUTF8 ? "rb" : "r");
    }
    
    if (!f) {
        AppendLog("[节点] 配置文件读取失败\r\n");
        return;
    }
    
    if (isUTF8) fseek(f, 3, SEEK_SET);
    
    // 设置默认值
    currentConfig.connections = 3;
    currentConfig.fallback = 0;
    currentConfig.nodeType = NODE_TYPE_ECHW; // 默认类型
    
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
            if (convertedValue) displayValue = convertedValue;
        }

        if (!strcmp(line, "configName")) strcpy(currentConfig.configName, displayValue);
        else if (!strcmp(line, "nodeType")) currentConfig.nodeType = (NodeType)atoi(val);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, displayValue);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, displayValue);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, displayValue);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, displayValue);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, displayValue);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, displayValue);
        else if (!strcmp(line, "connections")) currentConfig.connections = atoi(val);
        else if (!strcmp(line, "fallback")) currentConfig.fallback = atoi(val);
        
        if (convertedValue) free(convertedValue);
    }
    fclose(f);
    
    // 更新界面
    SetControlValues();
    UpdateControlsForNodeType(currentConfig.nodeType);
    
    if (autoStart) {
        if (isProcessRunning) StopProcess();
        // 给一点时间让进程完全释放
        Sleep(200); 
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[节点] 启动: %s\r\n", currentConfig.configName);
        AppendLog(logMsg);
        StartProcess();
    } else {
        AppendLog("[节点] 已加载配置\r\n");
    }
}

// 辅助列表保存/加载函数
void SaveListToFile(const char* path, int startIdx, int count) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fputc(0xEF, f); fputc(0xBB, f); fputc(0xBF, f);
    
    for (int i = startIdx; i < startIdx + count; i++) {
        char nodeName[MAX_SMALL_LEN];
        if (SendMessage(hNodeList, LB_GETTEXT, i, (LPARAM)nodeName) != LB_ERR) {
            char* utf8Name = GBKToUTF8(nodeName);
            fprintf(f, "%s\r\n", utf8Name ? utf8Name : nodeName);
            if (utf8Name) free(utf8Name);
        }
    }
    fclose(f);
}

void SaveNodeList() {
    CreateDirectory("nodes", NULL);
    // 只保存订阅部分的节点
    int listCount = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    int subCount = (listCount < g_totalNodeCount) ? listCount : g_totalNodeCount;
    SaveListToFile("nodes/nodelist.txt", 0, subCount);
}

void SaveManualNodeList() {
    CreateDirectory("manual_nodes", NULL);
    int listCount = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    int manualCount = listCount - g_totalNodeCount;
    if (manualCount > 0) {
        SaveListToFile("manual_nodes/nodelist.txt", g_totalNodeCount, manualCount);
    } else {
        // 如果没有手动节点，创建一个空文件或清空文件
        FILE* f = fopen("manual_nodes/nodelist.txt", "wb");
        if(f) { fputc(0xEF, f); fputc(0xBB, f); fputc(0xBF, f); fclose(f); }
    }
}

void LoadListFromFile(const char* path, int* countPtr) {
    BOOL isUTF8 = IsUTF8File(path);
    FILE* f = fopen(path, isUTF8 ? "rb" : "r");
    if (!f) return;
    if (isUTF8) fseek(f, 3, SEEK_SET);
    
    *countPtr = 0;
    char line[MAX_SMALL_LEN];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        
        if (len > 0) {
            if (isUTF8) {
                char* gbkName = UTF8ToGBK(line);
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)(gbkName ? gbkName : line));
                if (gbkName) free(gbkName);
            } else {
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)line);
            }
            (*countPtr)++;
        }
    }
    fclose(f);
}

void LoadNodeList() {
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    LoadListFromFile("nodes/nodelist.txt", &g_totalNodeCount);
}

void LoadManualNodeList() {
    LoadListFromFile("manual_nodes/nodelist.txt", &g_manualNodeCount);
}

// ============ 第七部分: 订阅管理与解析 ============

void SaveSubscriptionList() {
    FILE* f = fopen("subscriptions.txt", "w");
    if (!f) return;
    int count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        char url[MAX_URL_LEN];
        if (SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url) != LB_ERR) {
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
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len > 0) SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    fclose(f);
}

void AddSubscription() {
    char url[MAX_URL_LEN];
    GetWindowText(hSubscribeUrlEdit, url, sizeof(url));
    if (strlen(url) == 0) return;
    
    // 检查是否为直接添加的节点链接
    BOOL isEch = (strncmp(url, "ech://", 6) == 0 || strncmp(url, "ECH://", 6) == 0);
    BOOL isEchw = (strncmp(url, "echw://", 7) == 0 || strncmp(url, "ECHW://", 7) == 0);

    if (isEch || isEchw) {
        AppendLog("[节点] 检测到单节点链接,正在添加...\r\n");
        ParseSubscriptionData(url);
        SaveManualNodeList();
        SetWindowText(hSubscribeUrlEdit, "");
        return;
    }
    
    if (SendMessage(hSubList, LB_FINDSTRINGEXACT, -1, (LPARAM)url) != LB_ERR) {
        MessageBox(hMainWindow, "该订阅链接已存在", "提示", MB_OK);
        return;
    }
    
    SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
    SetWindowText(hSubscribeUrlEdit, "");
    SaveSubscriptionList();
}

void DelSubscription() {
    int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR) {
        SendMessage(hSubList, LB_DELETESTRING, sel, 0);
        SaveSubscriptionList();
    }
}

// 解析订阅数据的核心逻辑
// 支持 ech:// (ECH Tunnel) 和 echw:// (ECH Worker)
void ParseSubscriptionData(const char* data) {
    if (!data || strlen(data) == 0) return;
    
    char* dataCopy = NULL;
    
    // 简单判断是否Base64: 不包含协议头且无换行
    BOOL isEncoded = is_base64_encoded(data);
    if (isEncoded) {
        size_t decoded_len = 0;
        dataCopy = base64_decode(data, &decoded_len);
        if (!dataCopy) {
            AppendLog("[订阅] Base64解码失败\r\n");
            return;
        }
    } else {
        dataCopy = strdup(data);
    }
    
    if (!dataCopy) return;

    // 检查是否为单行手动添加 (没有换行符)
    BOOL isManualNode = (strchr(dataCopy, '\n') == NULL);
    
    char* line = strtok(dataCopy, "\r\n");
    int newNodesCount = 0;
    
    while (line != NULL) {
        // 跳过注释和空行
        if (strlen(line) > 0 && line[0] != ';' && strncmp(line, "//", 2) != 0) {
            
            NodeType type = NODE_TYPE_ECHW; // 默认
            int prefixLen = 0;
            
            if (strncmp(line, "ech://", 6) == 0 || strncmp(line, "ECH://", 6) == 0) {
                type = NODE_TYPE_ECH;
                prefixLen = 6;
            } else if (strncmp(line, "echw://", 7) == 0 || strncmp(line, "ECHW://", 7) == 0) {
                type = NODE_TYPE_ECHW;
                prefixLen = 7;
            } else {
                // 未知协议，跳过
                line = strtok(NULL, "\r\n");
                continue;
            }
            
            // 初始化临时变量
            char nodeName[MAX_SMALL_LEN] = {0};
            char server[MAX_URL_LEN] = {0};
            char token[MAX_URL_LEN] = {0};
            char ip[MAX_SMALL_LEN] = {0};
            char dns[MAX_SMALL_LEN] = {0};
            char ech[MAX_SMALL_LEN] = {0};
            
            // 提取名称 (#name)
            char* nameStart = strchr(line, '#');
            if (nameStart) {
                char* urlDecoded = URLDecode(nameStart + 1);
                if (urlDecoded) {
                    char* gbkName = UTF8ToGBK(urlDecoded);
                    if (gbkName) {
                        strncpy(nodeName, gbkName, MAX_SMALL_LEN - 1);
                        free(gbkName);
                    }
                    free(urlDecoded);
                }
                *nameStart = '\0'; // 截断名称部分，方便后续解析
            }
            
            // 解析参数 server|token|ip|dns|ech
            char* p = line + prefixLen;
            char* start = p;
            int partIndex = 0;
            
            while (*p) {
                if (*p == '|' || *(p+1) == '\0') {
                    size_t len = (*p == '|') ? (size_t)(p - start) : (size_t)(p - start + 1);
                    // 安全拷贝
                    if (len > 0) {
                        char temp[MAX_URL_LEN];
                        size_t safeLen = (len < MAX_URL_LEN) ? len : MAX_URL_LEN - 1;
                        strncpy(temp, start, safeLen);
                        temp[safeLen] = '\0';
                        
                        switch(partIndex) {
                            case 0: strcpy(server, temp); break;
                            case 1: strcpy(token, temp); break;
                            case 2: strcpy(ip, temp); break;
                            case 3: strcpy(dns, temp); break;
                            case 4: strcpy(ech, temp); break;
                        }
                    }
                    
                    if (*p == '|') {
                        partIndex++;
                        start = p + 1;
                    }
                }
                p++;
            }
            
            // 自动生成名称
            if (strlen(nodeName) == 0 && strlen(server) > 0) {
                 // 尝试用服务器域名
                 strncpy(nodeName, server, MAX_SMALL_LEN - 1);
                 // 简单的修饰
                 strcat(nodeName, (type == NODE_TYPE_ECH) ? " [ECH]" : " [WKR]");
            }
            
            // 保存有效节点
            if (strlen(server) > 0) {
                // 填充当前配置
                memset(&currentConfig, 0, sizeof(Config));
                strcpy(currentConfig.configName, nodeName);
                currentConfig.nodeType = type;
                strcpy(currentConfig.server, server);
                strcpy(currentConfig.token, token);
                strcpy(currentConfig.ip, ip);
                
                // 默认值处理
                strcpy(currentConfig.listen, "127.0.0.1:30000"); // 默认监听
                
                if (strlen(dns) > 0) strcpy(currentConfig.dns, dns);
                else strcpy(currentConfig.dns, "dns.alidns.com/dns-query");
                
                if (strlen(ech) > 0) strcpy(currentConfig.ech, ech);
                else strcpy(currentConfig.ech, "cloudflare-ech.com");
                
                currentConfig.connections = 3;
                currentConfig.fallback = 0;
                
                // 保存
                if (isManualNode) {
                    SaveNodeConfig(g_manualNodeCount, TRUE);
                    g_manualNodeCount++;
                } else {
                    SaveNodeConfig(g_totalNodeCount, FALSE);
                    g_totalNodeCount++;
                }
                
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nodeName);
                newNodesCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    
    free(dataCopy);
    
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 解析完成，新增 %d 个节点\r\n", newNodesCount);
    AppendLog(logMsg);
    
    // 恢复配置为列表第一个（如果有）
    if (SendMessage(hNodeList, LB_GETCOUNT, 0, 0) > 0) {
        LoadNodeConfigByIndex(0, FALSE);
    }
}

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
    
    // 动态缓冲区读取
    size_t bufSize = 1024 * 512; // 512KB
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

void FetchAllSubscriptions() {
    int subCount = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    if (subCount == 0) {
        MessageBox(hMainWindow, "请先添加订阅链接", "提示", MB_OK);
        return;
    }
    
    AppendLog("--------------------------\r\n");
    AppendLog("[订阅] 开始更新所有订阅...\r\n");
    
    // 清除订阅节点, 保留手动节点
    // 逻辑：删除索引 0 到 g_totalNodeCount-1 的项
    // 为了防止索引错位，从后往前删
    for (int i = g_totalNodeCount - 1; i >= 0; i--) {
        SendMessage(hNodeList, LB_DELETESTRING, i, 0);
    }
    
    g_totalNodeCount = 0;
    
    for (int i = 0; i < subCount; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        ProcessSingleSubscription(url);
    }
    
    SaveNodeList();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "更新完成，当前共有 %d 个订阅节点", g_totalNodeCount);
    MessageBox(hMainWindow, msg, "完成", MB_OK | MB_ICONINFORMATION);
    AppendLog("[订阅] 全部更新完成\r\n");
}

// ============ 第八部分: 编码与辅助工具函数 ============

// UTF-8 转 GBK
char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str || strlen(utf8Str) == 0) return strdup("");
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (wideLen == 0) return strdup(utf8Str); // Failback
    
    wchar_t* wideStr = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (!wideStr) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideLen);
    
    int gbkLen = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, NULL, 0, NULL, NULL);
    char* gbkStr = (char*)malloc(gbkLen);
    if (!gbkStr) { free(wideStr); return NULL; }
    
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
    if (!wideStr) return NULL;
    MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, wideStr, wideLen);
    
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, NULL, 0, NULL, NULL);
    char* utf8Str = (char*)malloc(utf8Len);
    if (!utf8Str) { free(wideStr); return NULL; }
    
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

// 检测文件是否为UTF-8 BOM
BOOL IsUTF8File(const char* fileName) {
    FILE* f = fopen(fileName, "rb");
    if (!f) return FALSE;
    unsigned char bom[3];
    size_t read = fread(bom, 1, 3, f);
    fclose(f);
    return (read == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF);
}

// Base64 解码表
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

char* base64_decode(const char* input, size_t* out_len) {
    size_t in_len = strlen(input);
    if (in_len == 0) return NULL;
    
    // 计算填充
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
        if (c == '=' || base64_table[c] == 64) continue; // Skip invalid
        
        block[block_pos++] = base64_table[c];
        
        if (block_pos == 4) {
            output[j++] = (block[0] << 2) | (block[1] >> 4);
            if (j < output_len) output[j++] = (block[1] << 4) | (block[2] >> 2);
            if (j < output_len) output[j++] = (block[2] << 6) | block[3];
            block_pos = 0;
        }
    }
    output[j] = '\0'; // Ensure null terminator
    if (out_len) *out_len = j;
    return output;
}

BOOL is_base64_encoded(const char* data) {
    if (!data || strlen(data) == 0) return FALSE;
    // 简单启发式：包含协议头肯定不是纯base64
    if (strstr(data, "://")) return FALSE;
    
    size_t len = strlen(data);
    size_t valid = 0;
    for (size_t i = 0; i < len; i++) {
        char c = data[i];
        if (isalnum(c) || c == '+' || c == '/' || c == '=' || c == '\r' || c == '\n') valid++;
    }
    // 超过90%是有效base64字符则认为是编码过的
    return (valid * 100 / len) > 90;
}
