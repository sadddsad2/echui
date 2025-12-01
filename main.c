// ��һ���֣�ͷ����ȫ�ֱ����ʹ��ڳ�ʼ��
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

#define APP_VERSION "1.3"
#define APP_TITLE "ECH workers �ͻ��� v" APP_VERSION

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

// �ؼ�ID����
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
#define ID_ADD_SUB_BTN      1019
#define ID_DEL_SUB_BTN      1020
#define ID_SUB_LIST         1021

HWND hMainWindow;
HWND hConfigNameEdit, hServerEdit, hListenEdit, hTokenEdit, hIpEdit, hDnsEdit, hEchEdit;
HWND hStartBtn, hStopBtn, hLogEdit, hSaveConfigBtn, hLoadConfigBtn;
HWND hSubscribeUrlEdit, hFetchSubBtn, hNodeList;
HWND hAddSubBtn, hDelSubBtn, hSubList;

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
    "Ĭ������", "dns.alidns.com/dns-query", "cloudflare-ech.com", "example.com:443", "", "127.0.0.1:30000", ""
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

    // ���ڸ�Ϊ800x700���ɵ�����С
    int winWidth = Scale(800);
    int winHeight = Scale(700); 
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    DWORD winStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;  // ���ӿɵ�����С

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
// �ڶ����֣����ڹ��̺Ϳؼ�����

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
            // ���ڴ�С�ı�ʱ����������������ؼ�λ�ã���ѡ��
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
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, "�򿪽���");
                    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "�˳�����");
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
                            MessageBox(hwnd, "����������ַ", "��ʾ", MB_OK | MB_ICONWARNING);
                            SetFocus(hServerEdit);
                            break;
                        }
                        if (strlen(currentConfig.listen) == 0) {
                            MessageBox(hwnd, "�����������ַ (127.0.0.1:...)", "��ʾ", MB_OK | MB_ICONWARNING);
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

void CreateLabelAndEdit2Column(HWND parent, const char* labelText, int x, int y, int w, int h, int editId, HWND* outEdit, BOOL numberOnly) {
    HWND hStatic = CreateWindow("STATIC", labelText, WS_VISIBLE | WS_CHILD | SS_LEFT, 
        x, y + Scale(3), Scale(90), Scale(20), parent, NULL, NULL, NULL);
    SendMessage(hStatic, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    DWORD style = WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;
    if (numberOnly) style |= ES_NUMBER | ES_CENTER;

    *outEdit = CreateWindow("EDIT", "", style, 
        x + Scale(95), y, w - Scale(95), h, parent, (HMENU)(intptr_t)editId, NULL, NULL);
    SendMessage(*outEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(*outEdit, EM_SETLIMITTEXT, (editId == ID_SERVER_EDIT || editId == ID_TOKEN_EDIT || editId == ID_SUBSCRIBE_URL_EDIT) ? MAX_URL_LEN : MAX_SMALL_LEN, 0);
}

void CreateControls(HWND hwnd) {
    RECT rect;
    GetClientRect(hwnd, &rect);
    int winW = rect.right;
    int margin = Scale(15);
    int groupW = winW - (margin * 2);
    int lineHeight = Scale(30);
    int lineGap = Scale(8);
    int editH = Scale(24);
    int curY = margin;

    // ���Ĺ�������
    int groupSubH = Scale(260);
    HWND hGroupSub = CreateWindow("BUTTON", "���Ĺ���", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, groupSubH, hwnd, NULL, NULL, NULL);
    SendMessage(hGroupSub, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    int innerY = curY + Scale(22);
    
    // ��������������
    HWND hSubLabel = CreateWindow("STATIC", "��������:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(12), innerY + Scale(3), Scale(70), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hSubLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hSubscribeUrlEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, 
        margin + Scale(85), innerY, groupW - Scale(180), editH, hwnd, (HMENU)ID_SUBSCRIBE_URL_EDIT, NULL, NULL);
    SendMessage(hSubscribeUrlEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hAddSubBtn = CreateWindow("BUTTON", "����", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + groupW - Scale(85), innerY, Scale(75), editH, hwnd, (HMENU)ID_ADD_SUB_BTN, NULL, NULL);
    SendMessage(hAddSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY += lineHeight + lineGap - Scale(5);

    // �����б���
    hSubList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
        margin + Scale(12), innerY, groupW - Scale(24), Scale(55), hwnd, (HMENU)ID_SUB_LIST, NULL, NULL);
    SendMessage(hSubList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    innerY += Scale(60);

    // ������ť��
    hDelSubBtn = CreateWindow("BUTTON", "ɾ��ѡ�ж���", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(12), innerY, Scale(110), Scale(28), hwnd, (HMENU)ID_DEL_SUB_BTN, NULL, NULL);
    SendMessage(hDelSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hFetchSubBtn = CreateWindow("BUTTON", "�������ж���", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        margin + Scale(130), innerY, Scale(110), Scale(28), hwnd, (HMENU)ID_FETCH_SUB_BTN, NULL, NULL);
    SendMessage(hFetchSubBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY += Scale(33);

    // �ڵ��б�
    HWND hNodeLabel = CreateWindow("STATIC", "�ڵ��б�(�����鿴/˫������):", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin + Scale(12), innerY + Scale(3), Scale(200), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hNodeLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    hNodeList = CreateWindow("LISTBOX", "", WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY,
        margin + Scale(12), innerY + Scale(25), groupW - Scale(24), Scale(80), hwnd, (HMENU)ID_NODE_LIST, NULL, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += groupSubH + Scale(12);
    
    // ��������
    HWND hConfigLabel = CreateWindow("STATIC", "��������:", WS_VISIBLE | WS_CHILD | SS_LEFT, 
        margin, curY + Scale(3), Scale(70), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hConfigLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hConfigNameEdit = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
        margin + Scale(75), curY, Scale(180), editH, hwnd, (HMENU)ID_CONFIG_NAME_EDIT, NULL, NULL);
    SendMessage(hConfigNameEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hConfigNameEdit, EM_SETLIMITTEXT, MAX_SMALL_LEN, 0);

    curY += lineHeight + Scale(5);

    // ������������ - 2�в���
    int group1H = Scale(80);
    HWND hGroup1 = CreateWindow("BUTTON", "��������", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group1H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup1, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    innerY = curY + Scale(22);
    int colW = (groupW - Scale(36)) / 2;

    CreateLabelAndEdit2Column(hwnd, "�����ַ:", margin + Scale(12), innerY, colW, editH, ID_SERVER_EDIT, &hServerEdit, FALSE);
    CreateLabelAndEdit2Column(hwnd, "������ַ:", margin + Scale(24) + colW, innerY, colW, editH, ID_LISTEN_EDIT, &hListenEdit, FALSE);

    curY += group1H + Scale(12);

    // �߼�ѡ������ - 2�в���
    int group2H = Scale(110);
    HWND hGroup2 = CreateWindow("BUTTON", "�߼�ѡ�� (��ѡ)", WS_VISIBLE | WS_CHILD | BS_GROUPBOX,
        margin, curY, groupW, group2H, hwnd, NULL, NULL, NULL);
    SendMessage(hGroup2, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    innerY = curY + Scale(22);

    CreateLabelAndEdit2Column(hwnd, "��������:", margin + Scale(12), innerY, colW, editH, ID_TOKEN_EDIT, &hTokenEdit, FALSE);
    CreateLabelAndEdit2Column(hwnd, "��ѡIP:", margin + Scale(24) + colW, innerY, colW, editH, ID_IP_EDIT, &hIpEdit, FALSE);
    innerY += lineHeight + lineGap;

    CreateLabelAndEdit2Column(hwnd, "ECH����:", margin + Scale(12), innerY, colW, editH, ID_ECH_EDIT, &hEchEdit, FALSE);
    CreateLabelAndEdit2Column(hwnd, "DNS����:", margin + Scale(24) + colW, innerY, colW, editH, ID_DNS_EDIT, &hDnsEdit, FALSE);

    curY += group2H + Scale(12);

    // ������ť
    int btnW = Scale(100);
    int btnH = Scale(32);
    int btnGap = Scale(15);
    int startX = margin;

    hStartBtn = CreateWindow("BUTTON", "��������", WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
        startX, curY, btnW, btnH, hwnd, (HMENU)ID_START_BTN, NULL, NULL);
    SendMessage(hStartBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hStopBtn = CreateWindow("BUTTON", "ֹͣ", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + btnW + btnGap, curY, btnW, btnH, hwnd, (HMENU)ID_STOP_BTN, NULL, NULL);
    SendMessage(hStopBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    EnableWindow(hStopBtn, FALSE);

    hSaveConfigBtn = CreateWindow("BUTTON", "��������", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 2, curY, btnW, btnH, hwnd, (HMENU)ID_SAVE_CONFIG_BTN, NULL, NULL);
    SendMessage(hSaveConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    hLoadConfigBtn = CreateWindow("BUTTON", "��������", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        startX + (btnW + btnGap) * 3, curY, btnW, btnH, hwnd, (HMENU)ID_LOAD_CONFIG_BTN, NULL, NULL);
    SendMessage(hLoadConfigBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    HWND hClrBtn = CreateWindow("BUTTON", "�����־", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
        rect.right - margin - btnW, curY, btnW, btnH, hwnd, (HMENU)ID_CLEAR_LOG_BTN, NULL, NULL);
    SendMessage(hClrBtn, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    curY += btnH + Scale(12);

    HWND hLogLabel = CreateWindow("STATIC", "������־:", WS_VISIBLE | WS_CHILD, 
        margin, curY, Scale(80), Scale(20), hwnd, NULL, NULL, NULL);
    SendMessage(hLogLabel, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    
    curY += Scale(22);

    hLogEdit = CreateWindow("EDIT", "", 
        WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY, 
        margin, curY, winW - (margin * 2), Scale(110), hwnd, (HMENU)ID_LOG_EDIT, NULL, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
    SendMessage(hLogEdit, EM_SETLIMITTEXT, 0, 0);
}
// �������֣����̹��������ô��������Ĺ��ܺͱ���ת��

// ============ �����б��ļ����� ============

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
    
    if (SendMessage(hSubList, LB_FINDSTRINGEXACT, -1, (LPARAM)url) != LB_ERR) {
        MessageBox(hMainWindow, "�ö��������Ѵ���", "��ʾ", MB_OK);
        return;
    }
    
    SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
    SetWindowText(hSubscribeUrlEdit, "");
    SaveSubscriptionList();
}

void DelSubscription() {
    int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    
    SendMessage(hSubList, LB_DELETESTRING, sel, 0);
    SaveSubscriptionList();
}

// ============ ���̹��������� ============

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
        AppendLog("����: �Ҳ��� ech-workers.exe �ļ�!\r\n");
        return;
    }
    
    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\"", exePath);
    
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
            AppendLog("[��ʾ] ��⵽IP��ʽDNS,���Զ�����TLS֤����֤\r\n");
        }
    }
    
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
        AppendLog("[ϵͳ] ����������...\r\n");
    } else {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        AppendLog("[����] ����ʧ��,�������á�\r\n");
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
        AppendLog("[ϵͳ] ������ֹͣ��\r\n");
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
        MessageBox(hMainWindow, "��������������", "��ʾ", MB_OK | MB_ICONWARNING);
        return;
    }
    
    char safeName[240];
    strncpy(safeName, currentConfig.configName, sizeof(safeName) - 1);
    safeName[sizeof(safeName) - 1] = '\0';
    
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "%s.ini", safeName);
    
    FILE* f = fopen(fileName, "w");
    if (!f) {
        MessageBox(hMainWindow, "��������ʧ��", "����", MB_OK | MB_ICONERROR);
        return;
    }
    
    fprintf(f, "[ECHTunnel]\nconfigName=%s\nserver=%s\nlisten=%s\ntoken=%s\nip=%s\ndns=%s\nech=%s\n",
        safeName, currentConfig.server, currentConfig.listen, currentConfig.token, 
        currentConfig.ip, currentConfig.dns, currentConfig.ech);
    fclose(f);
    
    char msg[512];
    snprintf(msg, sizeof(msg), "�����ѱ��浽: %s", fileName);
    MessageBox(hMainWindow, msg, "�ɹ�", MB_OK | MB_ICONINFORMATION);
    AppendLog("[����] �ѱ��������ļ�\r\n");
}

void LoadConfigFromFile() {
    OPENFILENAME ofn;
    char fileName[MAX_PATH] = "";
    
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMainWindow;
    ofn.lpstrFilter = "�����ļ� (*.ini)\0*.ini\0�����ļ� (*.*)\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = "ini";
    
    if (!GetOpenFileName(&ofn)) {
        return;
    }
    
    FILE* f = fopen(fileName, "r");
    if (!f) {
        MessageBox(hMainWindow, "�޷��������ļ�", "����", MB_OK | MB_ICONERROR);
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
        AppendLog("[����] ��⵽����������,����ֹͣ...\r\n");
        StopProcess();
        Sleep(500);
    }
    
    MessageBox(hMainWindow, "�����Ѽ���", "�ɹ�", MB_OK | MB_ICONINFORMATION);
    AppendLog("[����] �Ѽ��������ļ�\r\n");
    
    if (wasRunning) {
        AppendLog("[����] ����ʹ����������������...\r\n");
        Sleep(200);
        StartProcess();
    }
}

// ============ ����ת������ ============

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

// ============ �ڵ����ù��� ============

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

void LoadNodeConfigByIndex(int nodeIndex, BOOL autoStart) {
    char fileName[MAX_PATH];
    snprintf(fileName, sizeof(fileName), "nodes/node_%d.ini", nodeIndex);
    
    BOOL isUTF8 = IsUTF8File(fileName);
    FILE* f = fopen(fileName, isUTF8 ? "rb" : "r");
    if (!f) {
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[�ڵ�] �����ļ�������: %s\r\n", fileName);
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
            snprintf(logMsg, sizeof(logMsg), "[�ڵ�] �����л���: %s\r\n", currentConfig.configName);
            AppendLog(logMsg);
            AppendLog("[�ڵ�] ֹͣ��ǰ����...\r\n");
            StopProcess();
            Sleep(500);
            AppendLog("[�ڵ�] �����½ڵ�...\r\n");
            StartProcess();
        } else {
            char logMsg[512];
            snprintf(logMsg, sizeof(logMsg), "[�ڵ�] �����ڵ�: %s\r\n", currentConfig.configName);
            AppendLog(logMsg);
            StartProcess();
        }
    } else {
        char logMsg[512];
        snprintf(logMsg, sizeof(logMsg), "[�ڵ�] �鿴����: %s\r\n", currentConfig.configName);
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
        snprintf(logMsg, sizeof(logMsg), "[����] �Ѽ��� %d ���ڵ�\r\n", nodeCount);
        AppendLog(logMsg);
    }
}

// ============ Base64���루�����棩 ============

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

    // 计算填充
    size_t pad = 0;
    if (input[in_len - 1] == '=') pad++;
    if (in_len > 1 && input[in_len - 2] == '=') pad++;

    size_t out_size = ((in_len / 4) * 3) - pad;
    char* out = (char*)malloc(out_size + 1);
    if (!out) return NULL;

    size_t i, j = 0;
    for (i = 0; i < in_len; i += 4) {
        int n = d[(unsigned char)input[i]] << 18 | 
                d[(unsigned char)input[i + 1]] << 12 | 
                d[(unsigned char)input[i + 2]] << 6 | 
                d[(unsigned char)input[i + 3]];

        out[j++] = n >> 16;
        if (j < out_size) out[j++] = n >> 8 & 0xFF;
        if (j < out_size) out[j++] = n & 0xFF;
    }

    out[out_size] = '\0';
    if (out_len) *out_len = out_size;
    return out;
}

BOOL is_base64_encoded(const char* data) {
    if (!data || strlen(data) == 0) return FALSE;
    
    // 如果包含明确的协议头或换行符，通常不是纯Base64
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
            // 遇到非Base64字符且非空白字符
            return FALSE;
        }
    }
    
    // 简单的启发式检查：有效字符占比超过90%
    return (len > 0) && ((valid_chars * 100 / len) > 90);
}

// ============ 订阅解析与网络获取 ============

// 解析逻辑：处理 ech://server|token|ip|dns|ech#name 格式
void ParseSubscriptionData(const char* data) {
    if (!data || strlen(data) == 0) return;
    
    char* dataCopy = NULL;
    BOOL needFree = FALSE;

    // 尝试Base64解码
    if (is_base64_encoded(data)) {
        size_t decoded_len = 0;
        dataCopy = base64_decode(data, &decoded_len);
        if (dataCopy) {
            needFree = TRUE;
        } else {
            AppendLog("[订阅] Base64解码失败，尝试直接解析\r\n");
            dataCopy = strdup(data);
            needFree = TRUE;
        }
    } else {
        dataCopy = strdup(data);
        needFree = TRUE;
    }
    
    if (!dataCopy) return;

    char* line = strtok(dataCopy, "\r\n");
    int newNodesCount = 0;
    
    while (line != NULL) {
        // 跳过空行和注释
        if (strlen(line) > 0 && line[0] != ';' && strncmp(line, "//", 2) != 0) {
            
            // 检查协议头
            if (strncmp(line, "ech://", 6) != 0 && strncmp(line, "ECH://", 6) != 0) {
                line = strtok(NULL, "\r\n");
                continue;
            }
            
            Config nodeConfig;
            // 初始化默认值
            memset(&nodeConfig, 0, sizeof(Config));
            strcpy(nodeConfig.listen, "127.0.0.1:30000"); // 默认监听
            strcpy(nodeConfig.dns, "dns.alidns.com/dns-query");
            strcpy(nodeConfig.ech, "cloudflare-ech.com");

            // 1. 提取节点名称 ( # 之后的内容)
            char* nameStart = strchr(line, '#');
            if (nameStart) {
                *nameStart = '\0'; // 截断行，以便后续解析参数
                char* urlDecoded = URLDecode(nameStart + 1);
                if (urlDecoded) {
                    char* gbkName = UTF8ToGBK(urlDecoded);
                    if (gbkName) {
                        strncpy(nodeConfig.configName, gbkName, MAX_SMALL_LEN - 1);
                        free(gbkName);
                    } else {
                        strncpy(nodeConfig.configName, urlDecoded, MAX_SMALL_LEN - 1);
                    }
                    free(urlDecoded);
                }
            } else {
                snprintf(nodeConfig.configName, MAX_SMALL_LEN, "Node-%d", g_totalNodeCount + 1);
            }
            
            // 2. 解析参数 ech://server|token|ip|dns|ech
            char* p = line;
            if (strncmp(p, "ech://", 6) == 0 || strncmp(p, "ECH://", 6) == 0) {
                p += 6;
            }
            
            // 使用 | 分割参数
            // 格式顺序: server | token | ip | dns | ech
            int paramIdx = 0;
            char* tokenPtr = strtok(p, "|");
            
            while (tokenPtr != NULL) {
                switch (paramIdx) {
                    case 0: strncpy(nodeConfig.server, tokenPtr, MAX_URL_LEN - 1); break;
                    case 1: strncpy(nodeConfig.token, tokenPtr, MAX_URL_LEN - 1); break;
                    case 2: if(strlen(tokenPtr)>0) strncpy(nodeConfig.ip, tokenPtr, MAX_SMALL_LEN - 1); break;
                    case 3: if(strlen(tokenPtr)>0) strncpy(nodeConfig.dns, tokenPtr, MAX_SMALL_LEN - 1); break;
                    case 4: if(strlen(tokenPtr)>0) strncpy(nodeConfig.ech, tokenPtr, MAX_SMALL_LEN - 1); break;
                }
                paramIdx++;
                tokenPtr = strtok(NULL, "|");
            }

            // 验证必填项
            if (strlen(nodeConfig.server) > 0) {
                // 保存节点逻辑
                // 暂时备份当前UI配置
                Config backup = currentConfig;
                currentConfig = nodeConfig;
                
                SaveNodeConfig(g_totalNodeCount);
                
                // 恢复UI配置
                currentConfig = backup;
                
                // 添加到列表
                SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nodeConfig.configName);
                
                g_totalNodeCount++;
                newNodesCount++;
            }
        }
        line = strtok(NULL, "\r\n");
    }
    
    if (needFree) free(dataCopy);
    
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 解析完成，新增 %d 个节点\r\n", newNodesCount);
    AppendLog(logMsg);
}

void ProcessSingleSubscription(const char* url) {
    if (!url || strlen(url) == 0) return;

    char logMsg[MAX_URL_LEN + 32];
    snprintf(logMsg, sizeof(logMsg), "[订阅] 正在连接: %s\r\n", url);
    AppendLog(logMsg);

    HINTERNET hInternet = InternetOpen("ECHWorkerClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) {
        AppendLog("[订阅] 错误: InternetOpen 失败\r\n");
        return;
    }

    // 设置超时
    DWORD timeout = 5000;
    InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));

    HINTERNET hConnect = InternetOpenUrl(hInternet, url, NULL, 0, 
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE, 0);

    if (!hConnect) {
        AppendLog("[订阅] 错误: 无法连接到订阅地址\r\n");
        InternetCloseHandle(hInternet);
        return;
    }

    // 动态分配缓冲区读取数据
    size_t bufferSize = 512 * 1024; // 512KB limit
    char* buffer = (char*)malloc(bufferSize);
    if (!buffer) {
        InternetCloseHandle(hConnect);
        InternetCloseHandle(hInternet);
        return;
    }
    
    DWORD bytesRead = 0;
    DWORD totalRead = 0;
    char tempBuf[4096];

    while (InternetReadFile(hConnect, tempBuf, sizeof(tempBuf) - 1, &bytesRead) && bytesRead > 0) {
        if (totalRead + bytesRead < bufferSize - 1) {
            memcpy(buffer + totalRead, tempBuf, bytesRead);
            totalRead += bytesRead;
        } else {
            AppendLog("[订阅] 警告: 订阅内容过大，已截断\r\n");
            break;
        }
    }
    buffer[totalRead] = '\0';

    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);

    if (totalRead > 0) {
        ParseSubscriptionData(buffer);
    } else {
        AppendLog("[订阅] 获取到的内容为空\r\n");
    }

    free(buffer);
}

void FetchAllSubscriptions() {
    int count = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    if (count == 0) {
        MessageBox(hMainWindow, "请先添加订阅链接", "提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    EnableWindow(hFetchSubBtn, FALSE);
    AppendLog("------------------------------\r\n");
    AppendLog("[订阅] 开始更新订阅...\r\n");

    // 清空现有节点列表
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    g_totalNodeCount = 0;

    // 清理旧的节点文件 (nodes/*.ini)
    WIN32_FIND_DATA ffd;
    HANDLE hFind = FindFirstFile("nodes\\node_*.ini", &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "nodes\\%s", ffd.cFileName);
            DeleteFile(path);
        } while (FindNextFile(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    // 遍历订阅列表
    for (int i = 0; i < count; i++) {
        char url[MAX_URL_LEN];
        SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        ProcessSingleSubscription(url);
    }

    SaveNodeList();
    
    EnableWindow(hFetchSubBtn, TRUE);
    
    char msg[128];
    snprintf(msg, sizeof(msg), "订阅更新完成，共获取 %d 个节点", g_totalNodeCount);
    MessageBox(hMainWindow, msg, "完成", MB_OK | MB_ICONINFORMATION);
    AppendLog("[订阅] 更新结束\r\n");
    AppendLog("------------------------------\r\n");
}
