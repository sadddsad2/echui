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

#define APP_VERSION "3.1"
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

// 主窗口控件ID
#define ID_NODE_LIST          1003
#define ID_START_BTN          1007
#define ID_STOP_BTN           1008
#define ID_CLEAR_LOG_BTN      1009
#define ID_LOG_EDIT           1010
#define ID_EDIT_NODE_BTN      1011
#define ID_ADD_NODE_BTN       1012
#define ID_DEL_NODE_BTN       1013
#define ID_DEL_ALL_BTN        1014  // 新增：删除全部节点
#define ID_COPY_ALL_BTN       1015  // 新增：复制全部节点链接
#define ID_PASTE_NODE_BTN     1016  // 新增：粘贴节点
#define ID_SUB_MANAGE_BTN     1017  // 原订阅管理ID改为1017

// 订阅管理对话框控件ID
#define ID_SUB_URL_EDIT       3001
#define ID_SUB_ADD_BTN        3002
#define ID_SUB_LIST           3003
#define ID_SUB_DEL_BTN        3004
#define ID_SUB_FETCH_BTN      3005
#define ID_SUB_CLOSE_BTN      3006

// 编辑/添加节点对话框控件ID
#define ID_DLG_CONFIG_NAME_EDIT 2000
#define ID_DLG_NODE_TYPE_COMBO  2001
#define ID_DLG_SERVER_EDIT      2002
#define ID_DLG_LISTEN_EDIT      2003
#define ID_DLG_TOKEN_EDIT       2004
#define ID_DLG_IP_EDIT          2005
#define ID_DLG_DNS_EDIT         2006
#define ID_DLG_ECH_EDIT         2007
#define ID_DLG_CONN_EDIT        2008
#define ID_DLG_CONN_UP          2009
#define ID_DLG_CONN_DOWN        2010
#define ID_DLG_FALLBACK_CHECK   2011
#define ID_DLG_OK_BTN           2012
#define ID_DLG_CANCEL_BTN       2013

HWND hMainWindow;
HWND hNodeList;
HWND hStartBtn, hStopBtn, hLogEdit;
HWND hEditNodeBtn, hAddNodeBtn, hDelNodeBtn, hDelAllBtn, hCopyAllBtn, hPasteNodeBtn, hSubManageBtn;

PROCESS_INFORMATION processInfo;
HANDLE hLogPipe = NULL;
HANDLE hLogThread = NULL;
BOOL isProcessRunning = FALSE;
NOTIFYICONDATA nid;

typedef struct {
    char configName[MAX_SMALL_LEN];
    NodeType nodeType;           
    char dns[MAX_SMALL_LEN];     
    char ech[MAX_SMALL_LEN];     
    char server[MAX_URL_LEN];    
    char ip[MAX_SMALL_LEN];      
    char listen[MAX_SMALL_LEN];
    int connections;             
    int fallback;                
    char token[MAX_URL_LEN];     
} Config;

Config currentConfig = {
    "默认配置", NODE_TYPE_ECHW, 
    "dns.alidns.com/dns-query", "cloudflare-ech.com", 
    "example.com:443", "", "127.0.0.1:30000", 
    6, 0, ""
};

// 用于对话框的临时配置和编辑索引
Config tempConfig;
int g_editingNodeIndex = -1;

// 函数声明
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK EditNodeDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK AddNodeDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
INT_PTR CALLBACK SubManageDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

void CreateControls(HWND hwnd);
void StartProcess();
void StopProcess();
void AppendLog(const char* text);
void AppendLogAsync(const char* text);
DWORD WINAPI LogReaderThread(LPVOID lpParam);
void SaveConfig();
void LoadConfig();
void InitTrayIcon(HWND hwnd);
void ShowTrayIcon();
void RemoveTrayIcon();

void FetchAllSubscriptions();
void ProcessSingleSubscription(const char* url);
void ParseSubscriptionData(const char* data);
void AddSubscription(HWND hwndDlg);
void DelSubscription(HWND hwndDlg);
void SaveSubscriptionList();
void LoadSubscriptionList();

void DelSelectedNode();
void DelAllNodes();           // 新增：删除全部节点
void CopyAllNodeLinks();      // 新增：复制全部节点链接
void PasteNodes();            // 新增：粘贴节点
void SaveNodeConfig(int nodeIndex, BOOL isManual);
void LoadNodeList();
void SaveNodeList();
void SaveManualNodeList();
void LoadManualNodeList();
void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart);
void ShowEditNodeDialog();
void ShowAddNodeDialog();
void ShowSubManageDialog();

void UpdateControlsForNodeType(HWND hwndDlg, NodeType type);

char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
char* URLEncode(const char* str);  // 新增：URL编码
BOOL IsUTF8File(const char* fileName);
char* base64_decode(const char* input, size_t* out_len);
char* base64_encode(const unsigned char* input, size_t len);  // 新增：Base64编码
BOOL is_base64_encoded(const char* data);
char* GenerateNodeLink(int nodeIndex);  // 新增：生成节点链接
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

    int winWidth = Scale(900);
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
// ============ 第三部分:窗口过程和主窗口控件创建 ============

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            CreateControls(hwnd);
            LoadConfig();
            LoadSubscriptionList();
            LoadNodeList();
            LoadManualNodeList();
            break;

        case WM_SIZE: {
            if (wParam == SIZE_MINIMIZED) break;
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            int winW = rect.right;
            int winH = rect.bottom;
            int margin = Scale(15);
            
            HDWP hdwp = BeginDeferWindowPos(20);
            
            // 节点列表区域
            int curY = margin;
            int nodeListH = winH - Scale(280);
            if (nodeListH < Scale(300)) nodeListH = Scale(300);
            
            HWND hGroupNode = GetDlgItem(hwnd, 5001);
            if (hGroupNode) {
                DeferWindowPos(hdwp, hGroupNode, NULL, margin, curY, 
                    winW - margin * 2, nodeListH, SWP_NOZORDER);
            }
            
            // 按钮栏
            curY += nodeListH + Scale(10);
            int btnH = Scale(38);
            
            // 日志区域填充剩余空间
            curY += btnH + Scale(10);
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

                case ID_START_BTN:
                    if (!isProcessRunning) {
                        int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                        if (sel == LB_ERR) {
                            MessageBox(hwnd, "请先选择一个节点", "提示", MB_OK | MB_ICONWARNING);
                            break;
                        }
                        LoadNodeConfigByIndex(sel, TRUE);
                    }
                    break;

                case ID_STOP_BTN:
                    if (isProcessRunning) StopProcess();
                    break;

                case ID_CLEAR_LOG_BTN:
                    SetWindowText(hLogEdit, "");
                    break;

                case ID_EDIT_NODE_BTN:
                    ShowEditNodeDialog();
                    break;

                case ID_ADD_NODE_BTN:
                    ShowAddNodeDialog();
                    break;

                case ID_DEL_NODE_BTN:
                    DelSelectedNode();
                    break;

                case ID_DEL_ALL_BTN:
                    DelAllNodes();
                    break;

                case ID_COPY_ALL_BTN:
                    CopyAllNodeLinks();
                    break;

                case ID_PASTE_NODE_BTN:
                    PasteNodes();
                    break;

                case ID_SUB_MANAGE_BTN:
                    ShowSubManageDialog();
                    break;

                case ID_NODE_LIST:
                    if (HIWORD(wParam) == LBN_DBLCLK) {
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

void CreateControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int winW = rect.right;
    int winH = rect.bottom;
    int margin = Scale(15);
    int groupW = winW - (margin * 2);
    int curY = margin;

    // ========== 节点列表区域 ==========
    int nodeListH = winH - Scale(280);
    if (nodeListH < Scale(300)) nodeListH = Scale(300);
    
    HWND hGroupNode = CreateWindow("BUTTON", "节点列表", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, nodeListH, hwnd, (HMENU)5001, NULL, NULL);
    SendMessage(hGroupNode, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(25);

    // 节点操作按钮 - 第一行
    int btnY = innerY;
    int btnX = margin + Scale(12);
    int btnW = Scale(100);
    int btnH = Scale(30);
    int btnGap = Scale(10);

    hEditNodeBtn = CreateWindow("BUTTON", "修改配置", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX, btnY, btnW, btnH, hwnd, (HMENU)ID_EDIT_NODE_BTN, NULL, NULL);
    SendMessage(hEditNodeBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hAddNodeBtn = CreateWindow("BUTTON", "添加节点", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + btnW + btnGap, btnY, btnW, btnH, hwnd, (HMENU)ID_ADD_NODE_BTN, NULL, NULL);
    SendMessage(hAddNodeBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hDelNodeBtn = CreateWindow("BUTTON", "删除节点", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + (btnW + btnGap) * 2, btnY, btnW, btnH, hwnd, (HMENU)ID_DEL_NODE_BTN, NULL, NULL);
    SendMessage(hDelNodeBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 新增按钮 - 删除全部
    hDelAllBtn = CreateWindow("BUTTON", "删除全部", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + (btnW + btnGap) * 3, btnY, btnW, btnH, hwnd, (HMENU)ID_DEL_ALL_BTN, NULL, NULL);
    SendMessage(hDelAllBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 节点操作按钮 - 第二行
    btnY += btnH + Scale(8);

    // 新增按钮 - 复制全部
    hCopyAllBtn = CreateWindow("BUTTON", "复制全部", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX, btnY, btnW, btnH, hwnd, (HMENU)ID_COPY_ALL_BTN, NULL, NULL);
    SendMessage(hCopyAllBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 新增按钮 - 粘贴节点
    hPasteNodeBtn = CreateWindow("BUTTON", "粘贴节点", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + btnW + btnGap, btnY, btnW, btnH, hwnd, (HMENU)ID_PASTE_NODE_BTN, NULL, NULL);
    SendMessage(hPasteNodeBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    // 订阅管理按钮
    hSubManageBtn = CreateWindow("BUTTON", "订阅管理", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + (btnW + btnGap) * 2, btnY, btnW, btnH, hwnd, (HMENU)ID_SUB_MANAGE_BTN, NULL, NULL);
    SendMessage(hSubManageBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY = btnY + btnH + Scale(10);
    
    HWND hNodeLabel = CreateWindow("STATIC", "双击节点可启动代理:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(12), innerY + Scale(3), Scale(150), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        margin + Scale(12), innerY + Scale(25), groupW - Scale(24), 
        nodeListH - Scale(133), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += nodeListH + Scale(10);

    // ========== 按钮栏 ==========
    btnW = Scale(120);
    btnH = Scale(38);
    int startX = margin;

    hStartBtn = CreateWindow("BUTTON", "启动代理", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        startX, curY, btnW, btnH, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "停止", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + btnW + Scale(15), curY, btnW, btnH, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    HWND hClrBtn = CreateWindow("BUTTON", "清空日志", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        winW - margin - btnW, curY, btnW, btnH, hwnd, (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH + Scale(10);

    // ========== 日志区域 ==========
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
// ============ 第五部分:订阅管理对话框和节点编辑对话框 ============

void ShowSubManageDialog() {
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(3), hMainWindow, SubManageDialogProc);
}

INT_PTR CALLBACK SubManageDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hSubUrlEdit, hSubList;
    
    switch (uMsg) {
        case WM_INITDIALOG: {
            SetWindowText(hwndDlg, "订阅管理");
            
            // 居中窗口
            int dlgW = Scale(580);
            int dlgH = Scale(380);
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hwndDlg, NULL, (screenW - dlgW) / 2, (screenH - dlgH) / 2, 
                        dlgW, dlgH, SWP_NOZORDER);
            
            int margin = Scale(15);
            int editH = Scale(24);
            int btnH = Scale(32);
            int y = margin;
            
            // 订阅链接输入
            HWND hLabel = CreateWindow("STATIC", "订阅链接:", WS_VISIBLE | WS_CHILD | SS_LEFT,
                margin, y + Scale(4), Scale(80), Scale(20), hwndDlg, NULL, NULL, NULL);
            SendMessage(hLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            hSubUrlEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                margin + Scale(85), y, dlgW - Scale(190), editH, hwndDlg, (HMENU)ID_SUB_URL_EDIT, NULL, NULL);
            SendMessage(hSubUrlEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            HWND hAddBtn = CreateWindow("BUTTON", "添加", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                dlgW - margin - Scale(85), y, Scale(80), editH, hwndDlg, (HMENU)ID_SUB_ADD_BTN, NULL, NULL);
            SendMessage(hAddBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            y += editH + Scale(15);
            
            // 订阅列表
            hSubList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
                margin, y, dlgW - margin * 2 - Scale(10), Scale(200), hwndDlg, (HMENU)ID_SUB_LIST, NULL, NULL);
            SendMessage(hSubList, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            y += Scale(200) + Scale(15);
            
            // 操作按钮
            int btnW = Scale(110);
            int btnX = margin;
            
            HWND hDelBtn = CreateWindow("BUTTON", "删除选中", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                btnX, y, btnW, btnH, hwndDlg, (HMENU)ID_SUB_DEL_BTN, NULL, NULL);
            SendMessage(hDelBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            HWND hFetchBtn = CreateWindow("BUTTON", "更新所有订阅", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                btnX + btnW + Scale(15), y, Scale(130), btnH, hwndDlg, (HMENU)ID_SUB_FETCH_BTN, NULL, NULL);
            SendMessage(hFetchBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            HWND hCloseBtn = CreateWindow("BUTTON", "关闭", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                dlgW - margin - btnW - Scale(5), y, btnW, btnH, hwndDlg, (HMENU)ID_SUB_CLOSE_BTN, NULL, NULL);
            SendMessage(hCloseBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
            
            // 加载订阅列表
            FILE* f = fopen("subscriptions.txt", "r");
            if (f) {
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
            
            return TRUE;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SUB_ADD_BTN: {
                    AddSubscription(hwndDlg);
                    break;
                }
                
                case ID_SUB_DEL_BTN: {
                    DelSubscription(hwndDlg);
                    break;
                }
                
                case ID_SUB_FETCH_BTN: {
                    FetchAllSubscriptions();
                    break;
                }
                
                case ID_SUB_LIST:
                    if (HIWORD(wParam) == LBN_SELCHANGE) {
                        int sel = SendDlgItemMessage(hwndDlg, ID_SUB_LIST, LB_GETCURSEL, 0, 0);
                        if (sel != LB_ERR) {
                            char url[MAX_URL_LEN];
                            SendDlgItemMessage(hwndDlg, ID_SUB_LIST, LB_GETTEXT, sel, (LPARAM)url);
                            SetDlgItemText(hwndDlg, ID_SUB_URL_EDIT, url);
                        }
                    }
                    break;
                
                case ID_SUB_CLOSE_BTN:
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
            }
            break;
        
        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

void ShowEditNodeDialog() {
    int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) {
        MessageBox(hMainWindow, "请先选择要修改的节点", "提示", MB_OK | MB_ICONWARNING);
        return;
    }
    
    g_editingNodeIndex = sel;
    LoadNodeConfigByIndex(sel, FALSE);
    memcpy(&tempConfig, &currentConfig, sizeof(Config));
    
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(1), hMainWindow, EditNodeDialogProc);
}

void ShowAddNodeDialog() {
    strcpy(tempConfig.configName, "新节点");
    tempConfig.nodeType = NODE_TYPE_ECHW;
    strcpy(tempConfig.dns, "dns.alidns.com/dns-query");
    strcpy(tempConfig.ech, "cloudflare-ech.com");
    strcpy(tempConfig.server, "");
    strcpy(tempConfig.ip, "");
    strcpy(tempConfig.listen, "127.0.0.1:30000");
    tempConfig.connections = 3;
    tempConfig.fallback = 0;
    strcpy(tempConfig.token, "");
    
    g_editingNodeIndex = -1;
    
    DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(2), hMainWindow, AddNodeDialogProc);
}

void CreateDialogControls(HWND hwndDlg) {
    int margin = Scale(12);
    int labelW = Scale(95);
    int editW = Scale(330);
    int editH = Scale(24);
    int lineGap = Scale(32);
    int y = margin;
    
    CreateWindow("STATIC", "配置名称:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    HWND hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "节点类型:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    HWND hCombo = CreateWindow("COMBOBOX", "", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        margin + labelW, y, editW, Scale(200), hwndDlg, (HMENU)ID_DLG_NODE_TYPE_COMBO, NULL, NULL);
    SendMessage(hCombo, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)"ECH (ech-tunnel)");
    SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)"ECHW (ech-workers)");
    y += lineGap;
    
    CreateWindow("STATIC", "服务地址:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_SERVER_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_URL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "监听地址:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_LISTEN_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "身份令牌:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_TOKEN_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_URL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "优选IP(域名):", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_IP_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "DNS服务器:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_DNS_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "ECH域名:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    hEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + labelW, y, editW, editH, hwndDlg, (HMENU)ID_DLG_ECH_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);
    y += lineGap;
    
    CreateWindow("STATIC", "并发连接:", WS_VISIBLE | WS_CHILD | SS_LEFT,
        margin, y + Scale(3), labelW, Scale(20), hwndDlg, NULL, NULL, NULL);
    
    int numW = Scale(60);
    int btnSize = Scale(28);
    hEdit = CreateWindow("EDIT", "3", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_NUMBER | ES_CENTER,
        margin + labelW, y, numW, editH, hwndDlg, (HMENU)ID_DLG_CONN_EDIT, NULL, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    HWND hBtnDown = CreateWindow("BUTTON", "-", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + labelW + numW + Scale(5), y, btnSize, editH, hwndDlg, (HMENU)ID_DLG_CONN_DOWN, NULL, NULL);
    SendMessage(hBtnDown, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    HWND hBtnUp = CreateWindow("BUTTON", "+", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + labelW + numW + Scale(5) + btnSize + Scale(5), y, btnSize, editH, hwndDlg, (HMENU)ID_DLG_CONN_UP, NULL, NULL);
    SendMessage(hBtnUp, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += lineGap;
    
    HWND hCheck = CreateWindow("BUTTON", "禁用ECH (回退到普通TLS 1.3)", 
        WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
        margin + labelW, y, Scale(280), Scale(22), hwndDlg, (HMENU)ID_DLG_FALLBACK_CHECK, NULL, NULL);
    SendMessage(hCheck, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    y += lineGap + Scale(5);
    
    int btnW = Scale(95);
    int btnH = Scale(34);
    int btnX = (Scale(450) - btnW * 2 - Scale(15)) / 2;
    
    HWND hOkBtn = CreateWindow("BUTTON", "确定", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        btnX, y, btnW, btnH, hwndDlg, (HMENU)ID_DLG_OK_BTN, NULL, NULL);
    SendMessage(hOkBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    HWND hCancelBtn = CreateWindow("BUTTON", "取消", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        btnX + btnW + Scale(15), y, btnW, btnH, hwndDlg, (HMENU)ID_DLG_CANCEL_BTN, NULL, NULL);
    SendMessage(hCancelBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
}
// ============ 第六部分:对话框处理函数和进程控制 ============

void SetDialogValues(HWND hwndDlg) {
    SetDlgItemText(hwndDlg, ID_DLG_CONFIG_NAME_EDIT, tempConfig.configName);
    SendDlgItemMessage(hwndDlg, ID_DLG_NODE_TYPE_COMBO, CB_SETCURSEL, tempConfig.nodeType, 0);
    SetDlgItemText(hwndDlg, ID_DLG_SERVER_EDIT, tempConfig.server);
    SetDlgItemText(hwndDlg, ID_DLG_LISTEN_EDIT, tempConfig.listen);
    SetDlgItemText(hwndDlg, ID_DLG_TOKEN_EDIT, tempConfig.token);
    SetDlgItemText(hwndDlg, ID_DLG_IP_EDIT, tempConfig.ip);
    SetDlgItemText(hwndDlg, ID_DLG_DNS_EDIT, tempConfig.dns);
    SetDlgItemText(hwndDlg, ID_DLG_ECH_EDIT, tempConfig.ech);
    
    char connBuf[32];
    sprintf(connBuf, "%d", tempConfig.connections);
    SetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, connBuf);
    
    SendDlgItemMessage(hwndDlg, ID_DLG_FALLBACK_CHECK, BM_SETCHECK, 
        tempConfig.fallback ? BST_CHECKED : BST_UNCHECKED, 0);
    
    UpdateControlsForNodeType(hwndDlg, tempConfig.nodeType);
}

void GetDialogValues(HWND hwndDlg) {
    GetDlgItemText(hwndDlg, ID_DLG_CONFIG_NAME_EDIT, tempConfig.configName, sizeof(tempConfig.configName));
    
    int typeIdx = SendDlgItemMessage(hwndDlg, ID_DLG_NODE_TYPE_COMBO, CB_GETCURSEL, 0, 0);
    tempConfig.nodeType = (typeIdx != CB_ERR) ? (NodeType)typeIdx : NODE_TYPE_ECHW;
    
    GetDlgItemText(hwndDlg, ID_DLG_SERVER_EDIT, tempConfig.server, sizeof(tempConfig.server));
    GetDlgItemText(hwndDlg, ID_DLG_LISTEN_EDIT, tempConfig.listen, sizeof(tempConfig.listen));
    GetDlgItemText(hwndDlg, ID_DLG_TOKEN_EDIT, tempConfig.token, sizeof(tempConfig.token));
    GetDlgItemText(hwndDlg, ID_DLG_IP_EDIT, tempConfig.ip, sizeof(tempConfig.ip));
    GetDlgItemText(hwndDlg, ID_DLG_DNS_EDIT, tempConfig.dns, sizeof(tempConfig.dns));
    GetDlgItemText(hwndDlg, ID_DLG_ECH_EDIT, tempConfig.ech, sizeof(tempConfig.ech));
    
    char connBuf[32];
    GetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, connBuf, sizeof(connBuf));
    tempConfig.connections = atoi(connBuf);
    if (tempConfig.connections < 1) tempConfig.connections = 1;
    
    tempConfig.fallback = (SendDlgItemMessage(hwndDlg, ID_DLG_FALLBACK_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
}

void UpdateControlsForNodeType(HWND hwndDlg, NodeType type) {
    HWND hConnEdit = GetDlgItem(hwndDlg, ID_DLG_CONN_EDIT);
    HWND hConnUp = GetDlgItem(hwndDlg, ID_DLG_CONN_UP);
    HWND hConnDown = GetDlgItem(hwndDlg, ID_DLG_CONN_DOWN);
    HWND hFallback = GetDlgItem(hwndDlg, ID_DLG_FALLBACK_CHECK);
    HWND hDns = GetDlgItem(hwndDlg, ID_DLG_DNS_EDIT);
    HWND hEch = GetDlgItem(hwndDlg, ID_DLG_ECH_EDIT);
    
    if (type == NODE_TYPE_ECH) {
        ShowWindow(hConnEdit, SW_SHOW);
        ShowWindow(hConnUp, SW_SHOW);
        ShowWindow(hConnDown, SW_SHOW);
        ShowWindow(hFallback, SW_SHOW);
        
        BOOL fallback = (SendMessage(hFallback, BM_GETCHECK, 0, 0) == BST_CHECKED);
        EnableWindow(hDns, !fallback);
        EnableWindow(hEch, !fallback);
    } else {
        ShowWindow(hConnEdit, SW_HIDE);
        ShowWindow(hConnUp, SW_HIDE);
        ShowWindow(hConnDown, SW_HIDE);
        ShowWindow(hFallback, SW_HIDE);
        
        EnableWindow(hDns, TRUE);
        EnableWindow(hEch, TRUE);
    }
}

INT_PTR CALLBACK EditNodeDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            SetWindowText(hwndDlg, "修改节点配置");
            
            int dlgW = Scale(450);
            int dlgH = Scale(420);
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hwndDlg, NULL, (screenW - dlgW) / 2, (screenH - dlgH) / 2, 
                        dlgW, dlgH, SWP_NOZORDER);
            
            CreateDialogControls(hwndDlg);
            SetDialogValues(hwndDlg);
            return TRUE;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_DLG_NODE_TYPE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = SendDlgItemMessage(hwndDlg, ID_DLG_NODE_TYPE_COMBO, CB_GETCURSEL, 0, 0);
                        UpdateControlsForNodeType(hwndDlg, (NodeType)sel);
                    }
                    break;
                
                case ID_DLG_FALLBACK_CHECK: {
                    BOOL checked = (SendDlgItemMessage(hwndDlg, ID_DLG_FALLBACK_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    EnableWindow(GetDlgItem(hwndDlg, ID_DLG_DNS_EDIT), !checked);
                    EnableWindow(GetDlgItem(hwndDlg, ID_DLG_ECH_EDIT), !checked);
                    break;
                }
                
                case ID_DLG_CONN_UP: {
                    char buf[16];
                    GetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf, 16);
                    int val = atoi(buf);
                    if (val < 20) {
                        sprintf(buf, "%d", val + 1);
                        SetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf);
                    }
                    break;
                }
                
                case ID_DLG_CONN_DOWN: {
                    char buf[16];
                    GetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf, 16);
                    int val = atoi(buf);
                    if (val > 1) {
                        sprintf(buf, "%d", val - 1);
                        SetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf);
                    }
                    break;
                }
                
                case ID_DLG_OK_BTN: {
                    GetDialogValues(hwndDlg);
                    
                    if (strlen(tempConfig.configName) == 0) {
                        MessageBox(hwndDlg, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    
                    if (strlen(tempConfig.server) == 0) {
                        MessageBox(hwndDlg, "请输入服务地址", "提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    
                    memcpy(&currentConfig, &tempConfig, sizeof(Config));
                    
                    BOOL isManual = (g_editingNodeIndex >= g_totalNodeCount);
                    int fileIndex = isManual ? (g_editingNodeIndex - g_totalNodeCount) : g_editingNodeIndex;
                    SaveNodeConfig(fileIndex, isManual);
                    
                    SendMessage(hNodeList, LB_DELETESTRING, g_editingNodeIndex, 0);
                    SendMessage(hNodeList, LB_INSERTSTRING, g_editingNodeIndex, (LPARAM)currentConfig.configName);
                    SendMessage(hNodeList, LB_SETCURSEL, g_editingNodeIndex, 0);
                    
                    if (isManual) {
                        SaveManualNodeList();
                    } else {
                        SaveNodeList();
                    }
                    
                    AppendLog("[配置] 节点配置已更新\r\n");
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }
                
                case ID_DLG_CANCEL_BTN:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
        
        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK AddNodeDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_INITDIALOG: {
            SetWindowText(hwndDlg, "添加新节点");
            
            int dlgW = Scale(450);
            int dlgH = Scale(420);
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            int screenH = GetSystemMetrics(SM_CYSCREEN);
            SetWindowPos(hwndDlg, NULL, (screenW - dlgW) / 2, (screenH - dlgH) / 2, 
                        dlgW, dlgH, SWP_NOZORDER);
            
            CreateDialogControls(hwndDlg);
            SetDialogValues(hwndDlg);
            return TRUE;
        }
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_DLG_NODE_TYPE_COMBO:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = SendDlgItemMessage(hwndDlg, ID_DLG_NODE_TYPE_COMBO, CB_GETCURSEL, 0, 0);
                        UpdateControlsForNodeType(hwndDlg, (NodeType)sel);
                    }
                    break;
                
                case ID_DLG_FALLBACK_CHECK: {
                    BOOL checked = (SendDlgItemMessage(hwndDlg, ID_DLG_FALLBACK_CHECK, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    EnableWindow(GetDlgItem(hwndDlg, ID_DLG_DNS_EDIT), !checked);
                    EnableWindow(GetDlgItem(hwndDlg, ID_DLG_ECH_EDIT), !checked);
                    break;
                }
                
                case ID_DLG_CONN_UP: {
                    char buf[16];
                    GetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf, 16);
                    int val = atoi(buf);
                    if (val < 20) {
                        sprintf(buf, "%d", val + 1);
                        SetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf);
                    }
                    break;
                }
                
                case ID_DLG_CONN_DOWN: {
                    char buf[16];
                    GetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf, 16);
                    int val = atoi(buf);
                    if (val > 1) {
                        sprintf(buf, "%d", val - 1);
                        SetDlgItemText(hwndDlg, ID_DLG_CONN_EDIT, buf);
                    }
                    break;
                }
                
                case ID_DLG_OK_BTN: {
                    GetDialogValues(hwndDlg);
                    
                    if (strlen(tempConfig.configName) == 0) {
                        MessageBox(hwndDlg, "请输入配置名称", "提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    
                    if (strlen(tempConfig.server) == 0) {
                        MessageBox(hwndDlg, "请输入服务地址", "提示", MB_OK | MB_ICONWARNING);
                        return TRUE;
                    }
                    
                    memcpy(&currentConfig, &tempConfig, sizeof(Config));
                    SaveNodeConfig(g_manualNodeCount, TRUE);
                    
                    SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)currentConfig.configName);
                    g_manualNodeCount++;
                    SaveManualNodeList();
                    
                    AppendLog("[配置] 新节点已添加\r\n");
                    EndDialog(hwndDlg, IDOK);
                    return TRUE;
                }
                
                case ID_DLG_CANCEL_BTN:
                    EndDialog(hwndDlg, IDCANCEL);
                    return TRUE;
            }
            break;
        
        case WM_CLOSE:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
    }
    return FALSE;
}
// ============ 第七部分:进程控制和日志函数 ============

void StartProcess() {
    char cmdLine[MAX_CMD_LEN];
    char exePath[MAX_PATH];
    
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
        
        if (currentConfig.connections != 3) {
            char nBuf[32]; 
            sprintf(nBuf, " -n %d", currentConfig.connections);
            strcat(cmdLine, nBuf);
        }
        
    } else {
        if (strlen(currentConfig.server) > 0) {
            strcat(cmdLine, " -f ");
            strcat(cmdLine, currentConfig.server);
        }
        
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
        
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[系统] 已启动 %s 模式 - %s\r\n", 
            currentConfig.nodeType == NODE_TYPE_ECH ? "ECH" : "ECHW",
            currentConfig.configName);
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
// ============ 第八部分:配置管理、订阅管理 ============

void SaveConfig() {
    FILE* f = fopen("config.ini", "w");
    if (!f) return;
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nnodeType=%d\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\nconnections=%d\nfallback=%d\n",
        currentConfig.configName, currentConfig.nodeType, currentConfig.server, currentConfig.listen, currentConfig.token, 
        currentConfig.ip, currentConfig.dns, currentConfig.ech, currentConfig.connections, currentConfig.fallback);
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
}

void AddSubscription(HWND hwndDlg) {
    char url[MAX_URL_LEN];
    GetDlgItemText(hwndDlg, ID_SUB_URL_EDIT, url, sizeof(url));
    if (strlen(url) == 0) return;
    
    if (strncmp(url, "ech://", 6) == 0 || strncmp(url, "ECH://", 6) == 0 ||
        strncmp(url, "echw://", 7) == 0 || strncmp(url, "ECHW://", 7) == 0) {
        AppendLog("[节点] 检测到节点链接,直接解析为节点...\r\n");
        ParseSubscriptionData(url);
        SaveManualNodeList();
        SetDlgItemText(hwndDlg, ID_SUB_URL_EDIT, "");
        MessageBox(hwndDlg, "节点已添加到列表", "成功", MB_OK | MB_ICONINFORMATION);
        return;
    }
    
    HWND hSubList = GetDlgItem(hwndDlg, ID_SUB_LIST);
    if (SendMessage(hSubList, LB_FINDSTRINGEXACT, -1, (LPARAM)url) != LB_ERR) {
        MessageBox(hwndDlg, "该订阅链接已存在", "提示", MB_OK);
        return;
    }
    
    SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
    SetDlgItemText(hwndDlg, ID_SUB_URL_EDIT, "");
    SaveSubscriptionList();
}

void DelSubscription(HWND hwndDlg) {
    HWND hSubList = GetDlgItem(hwndDlg, ID_SUB_LIST);
    int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    
    SendMessage(hSubList, LB_DELETESTRING, sel, 0);
    SaveSubscriptionList();
}

void SaveSubscriptionList() {
    FILE* f = fopen("subscriptions.txt", "w");
    if (!f) return;
    
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    
    // 从订阅管理对话框读取,如果对话框不存在则从文件读取
    HWND hSubDlg = FindWindow(NULL, "订阅管理");
    if (hSubDlg) {
        HWND hSubList = GetDlgItem(hSubDlg, ID_SUB_LIST);
        if (hSubList) {
            count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
            for (int i = 0; i < count; i++) {
                char url[MAX_URL_LEN];
                int len = SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
                if (len > 0) {
                    fprintf(f, "%s\n", url);
                }
            }
        }
    }
    fclose(f);
}

void LoadSubscriptionList() {
    // 订阅列表只在订阅管理对话框中加载
}

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
    
    BOOL foundFile = FALSE;
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", sel);
    if (GetFileAttributes(fileName) != INVALID_FILE_ATTRIBUTES) {
        DeleteFile(fileName);
        foundFile = TRUE;
    }
    
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
    } else {
        g_manualNodeCount--;
    }
    
    SaveNodeList();
    SaveManualNodeList();
    
    char logMsg[512];
    snprintf(logMsg, sizeof(logMsg), "[节点] 已删除节点: %s\r\n", nodeName);
    AppendLog(logMsg);
}

// 新增函数：删除全部节点
void DelAllNodes() {
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    if (count == 0) {
        MessageBox(hMainWindow, "节点列表为空", "提示", MB_OK);
        return;
    }
    
    char msg[256];
    snprintf(msg, sizeof(msg), "确定要删除全部 %d 个节点吗?\n此操作不可恢复!", count);
    if (MessageBox(hMainWindow, msg, "确认删除全部", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }
    
    // 删除所有订阅节点文件
    for (int i = 0; i < g_totalNodeCount; i++) {
        char fileName[MAX_PATH];
        snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", i);
        DeleteFile(fileName);
    }
    
    // 删除所有手动节点文件
    for (int i = 0; i < g_manualNodeCount; i++) {
        char fileName[MAX_PATH];
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", i);
        DeleteFile(fileName);
    }
    
    // 清空列表
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    
    g_totalNodeCount = 0;
    g_manualNodeCount = 0;
    
    SaveNodeList();
    SaveManualNodeList();
    
    AppendLog("[节点] 已删除全部节点\r\n");
    MessageBox(hMainWindow, "已删除全部节点", "完成", MB_OK | MB_ICONINFORMATION);
}

// 新增函数：生成节点链接
char* GenerateNodeLink(int nodeIndex) {
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    BOOL isUTF8 = IsUTF8File(fileName);
    FILE* f = fopen(fileName, isUTF8 ? "rb" : "r");
    
    if (!f) {
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", nodeIndex - g_totalNodeCount);
        isUTF8 = IsUTF8File(fileName);
        f = fopen(fileName, isUTF8 ? "rb" : "r");
    }
    
    if (!f) return NULL;
    
    if (isUTF8) {
        fseek(f, 3, SEEK_SET);
    }
    
    Config nodeConfig = {0};
    strcpy(nodeConfig.dns, "dns.alidns.com/dns-query");
    strcpy(nodeConfig.ech, "cloudflare-ech.com");
    nodeConfig.connections = 3;
    
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

        if (!strcmp(line, "configName")) strcpy(nodeConfig.configName, displayValue);
        else if (!strcmp(line, "nodeType")) nodeConfig.nodeType = (NodeType)atoi(displayValue);
        else if (!strcmp(line, "server")) strcpy(nodeConfig.server, displayValue);
        else if (!strcmp(line, "token")) strcpy(nodeConfig.token, displayValue);
        else if (!strcmp(line, "ip")) strcpy(nodeConfig.ip, displayValue);
        else if (!strcmp(line, "dns")) strcpy(nodeConfig.dns, displayValue);
        else if (!strcmp(line, "ech")) strcpy(nodeConfig.ech, displayValue);
        else if (!strcmp(line, "connections")) nodeConfig.connections = atoi(displayValue);
        else if (!strcmp(line, "fallback")) nodeConfig.fallback = atoi(displayValue);
        
        if (convertedValue) free(convertedValue);
    }
    fclose(f);
    
    // 生成链接格式: ech://server|token|ip|dns|ech|connections|fallback#name
    // 或 echw://server|token|ip|dns|ech#name
    char* link = (char*)malloc(MAX_URL_LEN * 2);
    if (!link) return NULL;
    
    char* utf8Name = GBKToUTF8(nodeConfig.configName);
    char* encodedName = URLEncode(utf8Name ? utf8Name : nodeConfig.configName);
    
    if (nodeConfig.nodeType == NODE_TYPE_ECH) {
        snprintf(link, MAX_URL_LEN * 2, "ech://%s|%s|%s|%s|%s|%d|%d#%s",
            nodeConfig.server,
            nodeConfig.token,
            nodeConfig.ip,
            nodeConfig.dns,
            nodeConfig.ech,
            nodeConfig.connections,
            nodeConfig.fallback,
            encodedName ? encodedName : nodeConfig.configName);
    } else {
        snprintf(link, MAX_URL_LEN * 2, "echw://%s|%s|%s|%s|%s#%s",
            nodeConfig.server,
            nodeConfig.token,
            nodeConfig.ip,
            nodeConfig.dns,
            nodeConfig.ech,
            encodedName ? encodedName : nodeConfig.configName);
    }
    
    if (utf8Name) free(utf8Name);
    if (encodedName) free(encodedName);
    
    return link;
}

// 新增函数：复制全部节点链接
void CopyAllNodeLinks() {
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    if (count == 0) {
        MessageBox(hMainWindow, "节点列表为空", "提示", MB_OK);
        return;
    }
    
    AppendLog("[节点] 正在生成节点链接...\r\n");
    
    size_t totalSize = count * MAX_URL_LEN * 2;
    char* allLinks = (char*)malloc(totalSize);
    if (!allLinks) {
        AppendLog("[错误] 内存分配失败\r\n");
        return;
    }
    
    allLinks[0] = '\0';
    int successCount = 0;
    
    for (int i = 0; i < count; i++) {
        char* link = GenerateNodeLink(i);
        if (link) {
            if (strlen(allLinks) > 0) {
                strcat(allLinks, "\r\n");
            }
            strcat(allLinks, link);
            free(link);
            successCount++;
        }
    }
    
    if (successCount == 0) {
        free(allLinks);
        AppendLog("[错误] 无法生成节点链接\r\n");
        MessageBox(hMainWindow, "无法生成节点链接", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    // 复制到剪贴板
    if (OpenClipboard(hMainWindow)) {
        EmptyClipboard();
        
        size_t len = strlen(allLinks);
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
        if (hMem) {
            char* pMem = (char*)GlobalLock(hMem);
            if (pMem) {
                memcpy(pMem, allLinks, len + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
        }
        CloseClipboard();
        
        char logMsg[256];
        snprintf(logMsg, sizeof(logMsg), "[节点] 已复制 %d 个节点链接到剪贴板\r\n", successCount);
        AppendLog(logMsg);
        
        char msg[256];
        snprintf(msg, sizeof(msg), "已复制 %d 个节点链接到剪贴板", successCount);
        MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
    } else {
        AppendLog("[错误] 无法打开剪贴板\r\n");
    }
    
    free(allLinks);
}

// 新增函数：粘贴节点
void PasteNodes() {
    if (!OpenClipboard(hMainWindow)) {
        AppendLog("[错误] 无法打开剪贴板\r\n");
        MessageBox(hMainWindow, "无法打开剪贴板", "错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    HANDLE hData = GetClipboardData(CF_TEXT);
    if (!hData) {
        CloseClipboard();
        AppendLog("[提示] 剪贴板中没有文本数据\r\n");
        MessageBox(hMainWindow, "剪贴板中没有文本数据", "提示", MB_OK);
        return;
    }
    
    char* clipText = (char*)GlobalLock(hData);
    if (!clipText) {
        CloseClipboard();
        return;
    }
    
    char* textCopy = strdup(clipText);
    GlobalUnlock(hData);
    CloseClipboard();
    
    if (!textCopy) return;
    
    AppendLog("[节点] 开始解析粘贴的节点...\r\n");
    
    int beforeCount = g_totalNodeCount + g_manualNodeCount;
    ParseSubscriptionData(textCopy);
    int afterCount = g_totalNodeCount + g_manualNodeCount;
    int addedCount = afterCount - beforeCount;
    
    free(textCopy);
    
    SaveManualNodeList();
    
    if (addedCount > 0) {
        char logMsg[256];
        snprintf(logMsg, sizeof(logMsg), "[节点] 成功添加 %d 个节点\r\n", addedCount);
        AppendLog(logMsg);
        
        char msg[256];
        snprintf(msg, sizeof(msg), "成功添加 %d 个节点", addedCount);
        MessageBox(hMainWindow, msg, "成功", MB_OK | MB_ICONINFORMATION);
    } else {
        AppendLog("[提示] 未识别到有效的节点链接\r\n");
        MessageBox(hMainWindow, "未识别到有效的节点链接", "提示", MB_OK);
    }
}
// ============ 第九部分:节点配置保存和加载 ============

void SaveNodeConfig(int nodeIndex, BOOL isManual) {
    if (isManual) {
        CreateDirectory("manual_nodes", NULL);
    } else {
        CreateDirectory("nodes", NULL);
    }
    
    char fileName[MAX_PATH];
    if (isManual) {
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", nodeIndex);
    } else {
        snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    }
    
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

void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart) {
    char fileName[MAX_PATH];
    
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    BOOL isUTF8 = IsUTF8File(fileName);
    FILE* f = fopen(fileName, isUTF8 ? "rb" : "r");
    
    if (!f) {
        snprintf(fileName, sizeof(fileName), "manual_nodes/node_%d.ini", nodeIndex - g_totalNodeCount);
        isUTF8 = IsUTF8File(fileName);
        f = fopen(fileName, isUTF8 ? "rb" : "r");
    }
    
    if (!f) {
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[节点] 配置文件不存在: node_%d\r\n", nodeIndex);
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
        else if (!strcmp(line, "nodeType")) currentConfig.nodeType = (NodeType)atoi(displayValue);
        else if (!strcmp(line, "server")) strcpy(currentConfig.server, displayValue);
        else if (!strcmp(line, "listen")) strcpy(currentConfig.listen, displayValue);
        else if (!strcmp(line, "token")) strcpy(currentConfig.token, displayValue);
        else if (!strcmp(line, "ip")) strcpy(currentConfig.ip, displayValue);
        else if (!strcmp(line, "dns")) strcpy(currentConfig.dns, displayValue);
        else if (!strcmp(line, "ech")) strcpy(currentConfig.ech, displayValue);
        else if (!strcmp(line, "connections")) currentConfig.connections = atoi(displayValue);
        else if (!strcmp(line, "fallback")) currentConfig.fallback = atoi(displayValue);
        
        if (convertedValue) free(convertedValue);
    }
    fclose(f);
    
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
        snprintf(logMsg, sizeof(logMsg), "[节点] 加载配置: %s\r\n", currentConfig.configName);
        AppendLog(logMsg);
    }
}

void SaveNodeList() {
    CreateDirectory("nodes", NULL);
    
    FILE* f = fopen("nodes/nodelist.txt", "wb");
    if (!f) return;
    
    fputc(0xEF, f);
    fputc(0xBB, f);
    fputc(0xBF, f);
    
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    int saveCount = (count < g_totalNodeCount) ? count : g_totalNodeCount;
    
    for (int i = 0; i < saveCount; i++) {
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

void SaveManualNodeList() {
    CreateDirectory("manual_nodes", NULL);
    
    FILE* f = fopen("manual_nodes/nodelist.txt", "wb");
    if (!f) return;
    
    fputc(0xEF, f);
    fputc(0xBB, f);
    fputc(0xBF, f);
    
    int count = SendMessage(hNodeList, LB_GETCOUNT, 0, 0);
    
    for (int i = g_totalNodeCount; i < count; i++) {
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

void LoadNodeList() {
    BOOL isUTF8 = IsUTF8File("nodes/nodelist.txt");
    FILE* f = fopen("nodes/nodelist.txt", isUTF8 ? "rb" : "r");
    if (!f) return;
    
    if (isUTF8) {
        fseek(f, 3, SEEK_SET);
    }
    
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    g_totalNodeCount = 0;
    
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
            g_totalNodeCount++;
        }
    }
    fclose(f);
}

void LoadManualNodeList() {
    BOOL isUTF8 = IsUTF8File("manual_nodes/nodelist.txt");
    FILE* f = fopen("manual_nodes/nodelist.txt", isUTF8 ? "rb" : "r");
    if (!f) return;
    
    if (isUTF8) {
        fseek(f, 3, SEEK_SET);
    }
    
    g_manualNodeCount = 0;
    
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
            g_manualNodeCount++;
        }
    }
    fclose(f);
    
    int totalNodes = g_totalNodeCount + g_manualNodeCount;
    if (totalNodes > 0) {
        char logMsg[256];
        snprintf(logMsg, sizeof(logMsg), "[节点] 已加载 %d 个节点 (订阅:%d 手动:%d)\r\n", 
                 totalNodes, g_totalNodeCount, g_manualNodeCount);
        AppendLog(logMsg);
    }
}
// ============ 第十部分:编码转换函数 ============

char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str || strlen(utf8Str) == 0) return strdup("");
    
    int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8Str, -1, NULL, 0);
    if (wideLen == 0) {
        wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
        if (wideLen == 0) return strdup("UTF8DecodeError");
    }
    
    wchar_t* wideStr = (wchar_t*)malloc(wideLen * sizeof(wchar_t));
    if (!wideStr) return strdup("MemoryError");
    
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideLen);
    
    BOOL usedDefault = FALSE;
    int gbkLen = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, NULL, 0, "?", &usedDefault);
    
    if (gbkLen == 0) {
        free(wideStr);
        return strdup("GBKEncodeError");
    }
    
    char* gbkStr = (char*)malloc(gbkLen);
    if (!gbkStr) {
        free(wideStr);
        return strdup("MemoryError");
    }
    
    WideCharToMultiByte(CP_ACP, 0, wideStr, -1, gbkStr, gbkLen, "?", NULL);
    free(wideStr);
    
    return gbkStr;
}

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

// 新增函数：URL编码
char* URLEncode(const char* str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char* encoded = (char*)malloc(len * 3 + 1);
    if (!encoded) return NULL;
    
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded[j++] = c;
        } else if (c == ' ') {
            encoded[j++] = '+';
        } else {
            sprintf(encoded + j, "%%%02X", c);
            j += 3;
        }
    }
    encoded[j] = '\0';
    
    return encoded;
}

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
// ============ 第十一部分:Base64编码解码 ============

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

// 新增函数：Base64编码
char* base64_encode(const unsigned char* input, size_t len) {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    if (!input || len == 0) return NULL;
    
    size_t output_len = 4 * ((len + 2) / 3);
    char* output = (char*)malloc(output_len + 1);
    if (!output) return NULL;
    
    size_t i = 0, j = 0;
    unsigned char a3[3];
    unsigned char a4[4];
    
    while (len--) {
        a3[i++] = *(input++);
        if (i == 3) {
            a4[0] = (a3[0] & 0xfc) >> 2;
            a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
            a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
            a4[3] = a3[2] & 0x3f;
            
            for (i = 0; i < 4; i++) {
                output[j++] = base64_chars[a4[i]];
            }
            i = 0;
        }
    }
    
    if (i) {
        for (size_t k = i; k < 3; k++) {
            a3[k] = '\0';
        }
        
        a4[0] = (a3[0] & 0xfc) >> 2;
        a4[1] = ((a3[0] & 0x03) << 4) + ((a3[1] & 0xf0) >> 4);
        a4[2] = ((a3[1] & 0x0f) << 2) + ((a3[2] & 0xc0) >> 6);
        
        for (size_t k = 0; k < i + 1; k++) {
            output[j++] = base64_chars[a4[k]];
        }
        
        while (i++ < 3) {
            output[j++] = '=';
        }
    }
    
    output[j] = '\0';
    return output;
}

BOOL is_base64_encoded(const char* data) {
    if (!data || strlen(data) == 0) return FALSE;
    
    if (strstr(data, "ech://") || strstr(data, "ECH://") || 
        strstr(data, "echw://") || strstr(data, "ECHW://") ||
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
// ============ 第十二部分:订阅解析函数 ============

void ParseSubscriptionData(const char* data) {
    if (!data || strlen(data) == 0) {
        return;
    }
    
    BOOL isManualNode = ((strncmp(data, "ech://", 6) == 0 || strncmp(data, "ECH://", 6) == 0 ||
                          strncmp(data, "echw://", 7) == 0 || strncmp(data, "ECHW://", 7) == 0) && 
                         (strchr(data, '\n') == NULL && strchr(data, '\r') == NULL));
    
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
            NodeType nodeType = NODE_TYPE_ECHW;
            char* parseStart = line;
            
            if (strncmp(line, "ech://", 6) == 0 || strncmp(line, "ECH://", 6) == 0) {
                nodeType = NODE_TYPE_ECH;
                parseStart = line + 6;
            } else if (strncmp(line, "echw://", 7) == 0 || strncmp(line, "ECHW://", 7) == 0) {
                nodeType = NODE_TYPE_ECHW;
                parseStart = line + 7;
            } else {
                line = strtok(NULL, "\r\n");
                continue;
            }
            
            char nodeName[MAX_SMALL_LEN] = {0};
            char server[MAX_URL_LEN] = {0};
            char token[MAX_URL_LEN] = {0};
            char ip[MAX_SMALL_LEN] = {0};
            char dns[MAX_SMALL_LEN] = {0};
            char ech[MAX_SMALL_LEN] = {0};
            int connections = 3;
            int fallback = 0;
            
            char* nameStart = strchr(line, '#');
            if (nameStart) {
                char* urlDecoded = URLDecode(nameStart + 1);
                if (urlDecoded) {
                    size_t decLen = strlen(urlDecoded);
                    while (decLen > 0 && (urlDecoded[decLen-1] == ' ' || urlDecoded[decLen-1] == '\t')) {
                        urlDecoded[--decLen] = '\0';
                    }
                    
                    char* gbkName = UTF8ToGBK(urlDecoded);
                    if (gbkName) {
                        size_t copyLen = strlen(gbkName);
                        if (copyLen >= MAX_SMALL_LEN) {
                            copyLen = MAX_SMALL_LEN - 1;
                            while (copyLen > 0 && (unsigned char)gbkName[copyLen-1] >= 0x80) {
                                copyLen--;
                            }
                        }
                        memcpy(nodeName, gbkName, copyLen);
                        nodeName[copyLen] = '\0';
                        free(gbkName);
                    }
                    free(urlDecoded);
                }
                *nameStart = '\0';
            }
            
            char* p = parseStart;
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
                    } else if (partIndex == 5 && len > 0) {
                        char connStr[16] = {0};
                        strncpy(connStr, start, len < 16 ? len : 15);
                        connections = atoi(connStr);
                        if (connections < 1) connections = 1;
                    } else if (partIndex == 6 && len > 0) {
                        char fallStr[16] = {0};
                        strncpy(fallStr, start, len < 16 ? len : 15);
                        fallback = atoi(fallStr);
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
                currentConfig.nodeType = nodeType;
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
                
                currentConfig.connections = connections;
                currentConfig.fallback = fallback;
                
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
    
    if (isManualNode) {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "[节点] 已添加手动节点\r\n");
        AppendLog(logMsg);
    } else {
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "[订阅] 解析得到 %d 个节点\r\n", newNodesCount);
        AppendLog(logMsg);
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
    
    size_t bufSize = 1024 * 1024;
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
    HWND hSubDlg = FindWindow(NULL, "订阅管理");
    int subCount = 0;
    
    if (hSubDlg) {
        HWND hSubList = GetDlgItem(hSubDlg, ID_SUB_LIST);
        if (hSubList) {
            subCount = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
        }
    }
    
    if (subCount == 0) {
        MessageBox(hMainWindow, "请先添加订阅链接", "提示", MB_OK);
        return;
    }
    
    AppendLog("--------------------------\r\n");
    AppendLog("[订阅] 开始更新所有订阅...\r\n");
    
    for (int i = g_totalNodeCount - 1; i >= 0; i--) {
        SendMessage(hNodeList, LB_DELETESTRING, i, 0);
    }
    
    g_totalNodeCount = 0;
    
    if (hSubDlg) {
        HWND hSubList = GetDlgItem(hSubDlg, ID_SUB_LIST);
        if (hSubList) {
            for (int i = 0; i < subCount; i++) {
                char url[MAX_URL_LEN];
                SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
                ProcessSingleSubscription(url);
            }
        }
    }
    
    SaveNodeList();
    
    char msg[256];
    snprintf(msg, sizeof(msg), "更新完成,共获取 %d 个订阅节点", g_totalNodeCount);
    MessageBox(hMainWindow, msg, "订阅成功", MB_OK | MB_ICONINFORMATION);
    AppendLog("[订阅] 全部更新完成\r\n");
    AppendLog("--------------------------\r\n");
}
