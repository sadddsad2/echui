#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <wininet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// ================= 定义与宏 =================
#define APP_VERSION "2.2"
#define APP_TITLE "ECH 客户端 v" APP_VERSION
#define SINGLE_INSTANCE_MUTEX_NAME "ECHClient_Mutex_Unique_ID"

#define MAX_URL_LEN 8192
#define MAX_SMALL_LEN 2048
#define MAX_CMD_LEN 32768
#define MAX_NODES 100 // 最大节点数量

// 自定义消息
#define WM_TRAYICON (WM_USER + 1)
#define WM_APPEND_LOG (WM_USER + 2) 

// 资源ID
#define IDI_APP_ICON 101 
#define ID_TRAY_ICON 9001
#define ID_TRAY_OPEN 9002
#define ID_TRAY_EXIT 9003

// 主窗口控件ID
#define ID_BTN_ADD_NODE    1001
#define ID_BTN_EDIT_NODE   1002
#define ID_BTN_DEL_NODE    1003
#define ID_BTN_START       1004
#define ID_BTN_STOP        1005
#define ID_BTN_SUB_ADD     1006
#define ID_BTN_SUB_DEL     1007
#define ID_BTN_SUB_UPD     1008
#define ID_LIST_NODES      1009
#define ID_LIST_SUBS       1010
#define ID_EDIT_SUB_URL    1011
#define ID_EDIT_LOG        1012

// 配置窗口控件ID
#define ID_CFG_SAVE        2001
#define ID_CFG_CANCEL      2002
#define ID_CFG_TYPE        2003

// ================= 数据结构 =================

typedef enum {
    NODE_TYPE_ECH = 0,   // ech-tunnel
    NODE_TYPE_ECHW = 1   // ech-workers
} NodeType;

typedef struct {
    char configName[MAX_SMALL_LEN];
    NodeType nodeType;
    char server[MAX_URL_LEN];
    char listen[MAX_SMALL_LEN];
    char token[MAX_URL_LEN];
    char ip[MAX_SMALL_LEN];
    char dns[MAX_SMALL_LEN];
    char ech[MAX_SMALL_LEN];
    int connections;
    int fallback;
    BOOL isManual; // TRUE: 手动添加/修改, FALSE: 订阅获取
} Config;

// ================= 全局变量 =================

HINSTANCE hInst;
HWND hMainWindow;
HWND hNodeList, hSubList, hLogEdit, hSubUrlEdit;
HWND hStartBtn, hStopBtn, hEditNodeBtn;

// 字体
HFONT hFontUI = NULL;
HFONT hFontLog = NULL;

// 进程控制
PROCESS_INFORMATION processInfo = {0};
HANDLE hLogReadPipe = NULL;
HANDLE hLogWritePipe = NULL;
HANDLE hLogThread = NULL;
HANDLE hJobObject = NULL;
BOOL isProcessRunning = FALSE;

// 节点数据
Config g_nodeList[MAX_NODES];
int g_listCount = 0;          // 节点总数
int g_subscribedCount = 0;    // 订阅节点数量 (g_nodeList[0] 到 g_subscribedCount-1)
int g_editingIndex = -1;      // <--- 确保有此行，用于配置窗口判断是添加还是修改

Config currentConfig;       // 当前运行/选中的配置

// DPI缩放
int g_dpi = 96;
int g_scale = 100;

// 配置窗口句柄
HWND hConfigWnd = NULL;
HWND hCfgName, hCfgType, hCfgServer, hCfgListen, hCfgToken, hCfgIp, hCfgDns, hCfgEch, hCfgConn, hCfgFb;

// ================= 函数声明 =================

int Scale(int x);
void AppendLog(const char* format, ...);
void StartProcess();
void StopProcess();

// 文件与数据管理
void NodeListSaveToJSON();
void NodeListLoadFromJSON();
void NodeListUpdateUI();
void LoadConfigFromList(int index, Config* outCfg);
void SaveSubscriptionList();
void LoadSubscriptionList();

// 订阅与解析
void OpenConfigWindow(int index);
void UpdateConfigControls(NodeType type);
void FetchAllSubscriptions();
void ParseSubscriptionData(const char* data, BOOL isManualAddition);

// 编码转换工具
char* UTF8ToGBK(const char* utf8Str);
char* GBKToUTF8(const char* gbkStr);
char* URLDecode(const char* str);
char* Base64Decode(const char* input, size_t* out_len);
char* JSONEscape(const char* s);
char* JSONUnescape(const char* s);


// ================= DPI 辅助 =================
int Scale(int x) {
    return (x * g_scale) / 100;
}

// ================= 日志处理 =================
void AppendLogAsync(char* text) {
    if(hMainWindow) PostMessage(hMainWindow, WM_APPEND_LOG, 0, (LPARAM)text);
    else free(text);
}

void AppendLog(const char* format, ...) {
    char buf[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    
    SYSTEMTIME st;
    GetLocalTime(&st);
    char* fullMsg = (char*)malloc(strlen(buf) + 64);
    sprintf(fullMsg, "[%02d:%02d:%02d] %s", st.wHour, st.wMinute, st.wSecond, buf);
    
    AppendLogAsync(fullMsg);
}

// ================= JSON/编码工具 =================

char* JSONEscape(const char* s) {
    // 简易 JSON 字符串转义：只处理双引号和反斜杠
    if (!s) return strdup("");
    size_t len = strlen(s);
    // 预估最大需要三倍空间 (所有字符都转义)
    char* escaped = (char*)malloc(len * 3 + 1);
    char* p = escaped;
    
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '"' || s[i] == '\\') {
            *p++ = '\\';
            *p++ = s[i];
        } else if (s[i] == '\r' || s[i] == '\n') {
            // 跳过或处理换行符，简化为跳过
        } else {
            *p++ = s[i];
        }
    }
    *p = 0;
    return escaped;
}

char* JSONUnescape(const char* s) {
    // 简易 JSON 字符串反转义
    if (!s) return strdup("");
    char* unescaped = strdup(s);
    if (!unescaped) return NULL;
    char* src = unescaped;
    char* dst = unescaped;
    
    while (*src) {
        if (*src == '\\' && (src[1] == '"' || src[1] == '\\' || src[1] == '/' || src[1] == 'n' || src[1] == 'r')) {
            src++;
            if (*src == 'n') *dst++ = '\n';
            else if (*src == 'r') *dst++ = '\r';
            else *dst++ = *src;
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = 0;
    return unescaped;
}

char* GetJSONValue(const char* json, const char* key, char* buffer, size_t bufSize) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\":", key);
    
    char* start = strstr(json, search);
    if (!start) return NULL;
    start += strlen(search);
    
    // 跳过空格和冒号
    while (*start == ' ' || *start == ':') start++;

    if (*start == '"') { // String
        start++;
        char* end = strchr(start, '"');
        if (!end) return NULL;
        size_t len = end - start;
        if (len >= bufSize) len = bufSize - 1;
        strncpy(buffer, start, len);
        buffer[len] = 0;
        char* unesc = JSONUnescape(buffer);
        if (unesc) {
            strncpy(buffer, unesc, bufSize-1);
            buffer[bufSize-1] = 0;
            free(unesc);
        }
        return buffer;
    } else { // Number/Bool
        char* end = strchr(start, ',');
        if (!end) end = strchr(start, '}');
        if (!end) return NULL;
        size_t len = end - start;
        if (len >= bufSize) len = bufSize - 1;
        strncpy(buffer, start, len);
        buffer[len] = 0;
        // Trim whitespace
        for (int i=len-1; i>=0 && (buffer[i] == ' ' || buffer[i] == '\n' || buffer[i] == '\r'); i--) buffer[i] = 0;
        return buffer;
    }
}

// ... (其他编码工具保持不变)
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

char* Base64Decode(const char* input, size_t* out_len) {
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
        if (c == '=' || base64_table[c] == 64) continue;
        block[block_pos++] = base64_table[c];
        if (block_pos == 4) {
            output[j++] = (block[0] << 2) | (block[1] >> 4);
            if (j < output_len) output[j++] = (block[1] << 4) | (block[2] >> 2);
            if (j < output_len) output[j++] = (block[2] << 6) | block[3];
            block_pos = 0;
        }
    }
    output[j] = 0;
    if (out_len) *out_len = j;
    return output;
}

char* UTF8ToGBK(const char* utf8Str) {
    if (!utf8Str) return NULL;
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (len == 0) return strdup(utf8Str);
    wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wstr, len);
    int len2 = WideCharToMultiByte(CP_ACP, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* str = (char*)malloc(len2);
    WideCharToMultiByte(CP_ACP, 0, wstr, -1, str, len2, NULL, NULL);
    free(wstr);
    return str;
}

char* GBKToUTF8(const char* gbkStr) {
    if (!gbkStr) return NULL;
    int len = MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, NULL, 0);
    if (len == 0) return strdup(gbkStr);
    wchar_t* wstr = (wchar_t*)malloc(len * sizeof(wchar_t));
    MultiByteToWideChar(CP_ACP, 0, gbkStr, -1, wstr, len);
    int len2 = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    char* str = (char*)malloc(len2);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, str, len2, NULL, NULL);
    free(wstr);
    return str;
}

char* URLDecode(const char* str) {
    if (!str) return NULL;
    char* decoded = (char*)malloc(strlen(str) + 1);
    char* ptr = decoded;
    while (*str) {
        if (*str == '%' && str[1] && str[2]) {
            char hex[3] = { str[1], str[2], 0 };
            *ptr++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else if (*str == '+') {
            *ptr++ = ' ';
            str++;
        } else {
            *ptr++ = *str++;
        }
    }
    *ptr = 0;
    return decoded;
}

// ================= JSON 存储与管理 =================

void NodeListSaveToJSON() {
    FILE* f = fopen("nodes.json", "w");
    if (!f) {
        AppendLog("[错误] 无法打开 nodes.json 文件进行写入\r\n");
        return;
    }
    
    // 写入 JSON 数组开始符号
    fprintf(f, "[\r\n");

    for (int i = 0; i < g_listCount; i++) {
        Config* cfg = &g_nodeList[i];
        
        // C 字符串转义并转为 UTF8 格式写入文件 (JSON标准)
        char* uName = GBKToUTF8(cfg->configName);
        char* uSvr = GBKToUTF8(cfg->server);
        char* uTkn = GBKToUTF8(cfg->token);
        
        char* escName = JSONEscape(uName ? uName : cfg->configName);
        char* escSvr = JSONEscape(uSvr ? uSvr : cfg->server);
        char* escTkn = JSONEscape(uTkn ? uTkn : cfg->token);
        
        fprintf(f, "  {\r\n");
        fprintf(f, "    \"name\": \"%s\",\r\n", escName);
        fprintf(f, "    \"type\": %d,\r\n", cfg->nodeType);
        fprintf(f, "    \"server\": \"%s\",\r\n", escSvr);
        fprintf(f, "    \"listen\": \"%s\",\r\n", cfg->listen);
        fprintf(f, "    \"token\": \"%s\",\r\n", escTkn);
        fprintf(f, "    \"ip\": \"%s\",\r\n", cfg->ip);
        fprintf(f, "    \"dns\": \"%s\",\r\n", cfg->dns);
        fprintf(f, "    \"ech\": \"%s\",\r\n", cfg->ech);
        fprintf(f, "    \"connections\": %d,\r\n", cfg->connections);
        fprintf(f, "    \"fallback\": %d,\r\n", cfg->fallback);
        fprintf(f, "    \"isManual\": %s\r\n", cfg->isManual ? "true" : "false");
        fprintf(f, "  }%s\r\n", (i < g_listCount - 1) ? "," : "");

        if (uName) free(uName); if (uSvr) free(uSvr); if (uTkn) free(uTkn);
        if (escName) free(escName); if (escSvr) free(escSvr); if (escTkn) free(escTkn);
    }
    
    // 写入 JSON 数组结束符号
    fprintf(f, "]\r\n");
    fclose(f);
    AppendLog("[系统] 节点配置已保存到 nodes.json\r\n");
}

void NodeListLoadFromJSON() {
    g_listCount = 0;
    g_subscribedCount = 0;
    
    FILE* f = fopen("nodes.json", "r");
    if (!f) return;

    // 读取整个文件内容（假设文件不太大，适合读入内存）
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* json_data = (char*)malloc(fsize + 1);
    if (!json_data) { fclose(f); return; }
    fread(json_data, 1, fsize, f);
    json_data[fsize] = 0;
    fclose(f);

    // 查找 JSON 对象的开始和结束
    char* current_obj = strchr(json_data, '{');
    char buffer[MAX_URL_LEN];

    while (current_obj && g_listCount < MAX_NODES) {
        char* end_obj = strchr(current_obj, '}');
        if (!end_obj) break;
        
        // 提取单个对象字符串
        *end_obj = 0; 
        
        Config* cfg = &g_nodeList[g_listCount];
        memset(cfg, 0, sizeof(Config));

        // 解析字段
        if (GetJSONValue(current_obj, "name", buffer, sizeof(buffer))) {
            char* gbk = UTF8ToGBK(buffer);
            strncpy(cfg->configName, gbk ? gbk : buffer, MAX_SMALL_LEN - 1);
            if(gbk) free(gbk);
        }
        if (GetJSONValue(current_obj, "type", buffer, sizeof(buffer))) cfg->nodeType = atoi(buffer);
        if (GetJSONValue(current_obj, "server", buffer, sizeof(buffer))) {
            char* gbk = UTF8ToGBK(buffer);
            strncpy(cfg->server, gbk ? gbk : buffer, MAX_URL_LEN - 1);
            if(gbk) free(gbk);
        }
        if (GetJSONValue(current_obj, "listen", buffer, sizeof(buffer))) strncpy(cfg->listen, buffer, MAX_SMALL_LEN - 1);
        if (GetJSONValue(current_obj, "token", buffer, sizeof(buffer))) {
            char* gbk = UTF8ToGBK(buffer);
            strncpy(cfg->token, gbk ? gbk : buffer, MAX_URL_LEN - 1);
            if(gbk) free(gbk);
        }
        if (GetJSONValue(current_obj, "ip", buffer, sizeof(buffer))) strncpy(cfg->ip, buffer, MAX_SMALL_LEN - 1);
        if (GetJSONValue(current_obj, "dns", buffer, sizeof(buffer))) strncpy(cfg->dns, buffer, MAX_SMALL_LEN - 1);
        if (GetJSONValue(current_obj, "ech", buffer, sizeof(buffer))) strncpy(cfg->ech, buffer, MAX_SMALL_LEN - 1);
        if (GetJSONValue(current_obj, "connections", buffer, sizeof(buffer))) cfg->connections = atoi(buffer);
        if (GetJSONValue(current_obj, "fallback", buffer, sizeof(buffer))) cfg->fallback = atoi(buffer);
        if (GetJSONValue(current_obj, "isManual", buffer, sizeof(buffer))) cfg->isManual = (strcmp(buffer, "true") == 0);
        
        g_listCount++;
        if (!cfg->isManual) g_subscribedCount++;

        // 查找下一个对象
        *end_obj = '}'; // 恢复对象结束符
        current_obj = strchr(end_obj, '{');
    }

    free(json_data);
}

void NodeListUpdateUI() {
    SendMessage(hNodeList, LB_RESETCONTENT, 0, 0);
    for (int i = 0; i < g_listCount; i++) {
        char nameWithTag[MAX_SMALL_LEN + 16];
        const char* tag = g_nodeList[i].isManual ? "[手动]" : "[订阅]";
        snprintf(nameWithTag, sizeof(nameWithTag), "%s %s", tag, g_nodeList[i].configName);
        SendMessage(hNodeList, LB_ADDSTRING, 0, (LPARAM)nameWithTag);
    }
}

void LoadConfigFromList(int index, Config* outCfg) {
    if (index >= 0 && index < g_listCount) {
        *outCfg = g_nodeList[index];
        char msg[256];
        snprintf(msg, sizeof(msg), "[节点] 已选中: %s (Type: %d)\r\n", outCfg->configName, outCfg->nodeType);
        AppendLog(msg);
    }
}


// ================= 进程管理 (与上版本相同) =================

DWORD WINAPI LogReaderThread(LPVOID lpParam) {
    char buf[1024];
    DWORD read;
    while (isProcessRunning && hLogReadPipe) {
        if (ReadFile(hLogReadPipe, buf, sizeof(buf) - 1, &read, NULL) && read > 0) {
            buf[read] = 0;
            char* gbk = UTF8ToGBK(buf);
            if(gbk) {
                AppendLogAsync(gbk);
            } else {
                AppendLogAsync(strdup(buf));
            }
        } else {
            break;
        }
    }
    return 0;
}

void StartProcess() {
    if (isProcessRunning) return;

    // 1. 构造命令行
    char cmdLine[MAX_CMD_LEN];
    char exePath[MAX_PATH];
    
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        strcpy(exePath, "ech-tunnel.exe");
    } else {
        strcpy(exePath, "ech-workers.exe");
    }
    
    // 检查文件是否存在
    if (GetFileAttributes(exePath) == INVALID_FILE_ATTRIBUTES) {
        AppendLog("错误: 找不到 %s\r\n", exePath);
        return;
    }

    snprintf(cmdLine, MAX_CMD_LEN, "\"%s\"", exePath);

    // 2. 根据类型拼接参数
    if (currentConfig.nodeType == NODE_TYPE_ECH) {
        if (strlen(currentConfig.server) > 0) {
            char svr[MAX_URL_LEN];
            if (strncmp(currentConfig.server, "wss://", 6) != 0) sprintf(svr, "wss://%s", currentConfig.server);
            else strcpy(svr, currentConfig.server);
            strcat(cmdLine, " -f \""); strcat(cmdLine, svr); strcat(cmdLine, "\"");
        }
        if (strlen(currentConfig.listen) > 0) {
            char lst[MAX_SMALL_LEN];
            if (strncmp(currentConfig.listen, "proxy://", 8) != 0 && strncmp(currentConfig.listen, "socks5://", 9) != 0) 
                sprintf(lst, "proxy://%s", currentConfig.listen);
            else strcpy(lst, currentConfig.listen);
            strcat(cmdLine, " -l \""); strcat(cmdLine, lst); strcat(cmdLine, "\"");
        }
        if(strlen(currentConfig.token) > 0) { strcat(cmdLine, " -token \""); strcat(cmdLine, currentConfig.token); strcat(cmdLine, "\""); }
        if(strlen(currentConfig.ip) > 0) { strcat(cmdLine, " -ip \""); strcat(cmdLine, currentConfig.ip); strcat(cmdLine, "\""); }
        
        if (currentConfig.fallback) {
            strcat(cmdLine, " -fallback");
        } else {
            if(strlen(currentConfig.dns) > 0) { strcat(cmdLine, " -dns \""); strcat(cmdLine, currentConfig.dns); strcat(cmdLine, "\""); }
            if(strlen(currentConfig.ech) > 0) { strcat(cmdLine, " -ech \""); strcat(cmdLine, currentConfig.ech); strcat(cmdLine, "\""); }
        }
        if (currentConfig.connections > 0 && currentConfig.connections != 3) {
            char tmp[32]; sprintf(tmp, " -n %d", currentConfig.connections); strcat(cmdLine, tmp);
        }

    } else {
        if(strlen(currentConfig.server) > 0) { strcat(cmdLine, " -f "); strcat(cmdLine, currentConfig.server); }
        if (strlen(currentConfig.listen) > 0) {
            char* ptr = currentConfig.listen;
            if (strncmp(ptr, "socks5://", 9) == 0) ptr += 9;
            else if (strncmp(ptr, "proxy://", 8) == 0) ptr += 8;
            else if (strncmp(ptr, "http://", 7) == 0) ptr += 7;
            strcat(cmdLine, " -l "); strcat(cmdLine, ptr);
        }
        if(strlen(currentConfig.token) > 0) { strcat(cmdLine, " -token "); strcat(cmdLine, currentConfig.token); }
        if(strlen(currentConfig.ip) > 0) { strcat(cmdLine, " -ip "); strcat(cmdLine, currentConfig.ip); }
        if(strlen(currentConfig.dns) > 0) { 
            strcat(cmdLine, " -dns "); strcat(cmdLine, currentConfig.dns); 
            if (currentConfig.dns[0] >= '0' && currentConfig.dns[0] <= '9') strcat(cmdLine, " -insecure-dns");
        }
        if(strlen(currentConfig.ech) > 0) { strcat(cmdLine, " -ech "); strcat(cmdLine, currentConfig.ech); }
    }

    // 3. 管道与进程启动
    SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), NULL, TRUE };
    if (!CreatePipe(&hLogReadPipe, &hLogWritePipe, &sa, 0)) return;
    SetHandleInformation(hLogReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFO si = {0};
    si.cb = sizeof(STARTUPINFO);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hLogWritePipe;
    si.hStdError = hLogWritePipe;
    si.wShowWindow = SW_HIDE;

    if (CreateProcess(NULL, cmdLine, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB, NULL, NULL, &si, &processInfo)) {
        CloseHandle(hLogWritePipe); 
        
        hJobObject = CreateJobObject(NULL, NULL);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobHandle(hJobObject, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
        AssignProcessToJobObject(hJobObject, processInfo.hProcess);

        isProcessRunning = TRUE;
        hLogThread = CreateThread(NULL, 0, LogReaderThread, NULL, 0, NULL);
        
        EnableWindow(hStartBtn, FALSE);
        EnableWindow(hStopBtn, TRUE);
        EnableWindow(hEditNodeBtn, FALSE); 

        AppendLog("已启动: %s (%s)\r\n", currentConfig.configName, 
            currentConfig.nodeType == NODE_TYPE_ECH ? "ECH Tunnel" : "ECH Worker");
    } else {
        CloseHandle(hLogReadPipe);
        CloseHandle(hLogWritePipe);
        AppendLog("启动失败，错误码: %d\r\n", GetLastError());
    }
}

void StopProcess() {
    if (!isProcessRunning) return;

    if (processInfo.hProcess) {
        TerminateProcess(processInfo.hProcess, 0);
        CloseHandle(processInfo.hProcess);
        CloseHandle(processInfo.hThread);
        processInfo.hProcess = NULL;
    }
    if (hJobObject) { CloseHandle(hJobObject); hJobObject = NULL; }
    
    isProcessRunning = FALSE;

    if (hLogThread) {
        WaitForSingleObject(hLogThread, 500);
        CloseHandle(hLogThread);
        hLogThread = NULL;
    }
    if (hLogReadPipe) { CloseHandle(hLogReadPipe); hLogReadPipe = NULL; }

    EnableWindow(hStartBtn, TRUE);
    EnableWindow(hStopBtn, FALSE);
    EnableWindow(hEditNodeBtn, TRUE);
    AppendLog("进程已停止。\r\n");
}


// ================= 配置窗口逻辑 =================

void UpdateConfigControls(NodeType type) {
    if (type == NODE_TYPE_ECH) {
        EnableWindow(hCfgConn, TRUE);
        EnableWindow(hCfgFb, TRUE);
    } else {
        EnableWindow(hCfgConn, FALSE);
        EnableWindow(hCfgFb, FALSE);
    }
}

LRESULT CALLBACK ConfigWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static Config tempConfig; // 静态变量用于存储临时配置

    switch (uMsg) {
        case WM_INITDIALOG:
            // 初始化 tempConfig
            if (lParam) tempConfig = *(Config*)lParam;
            
            // 控件初始化... (已在 OpenConfigWindow 中完成)
            
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_CFG_TYPE && HIWORD(wParam) == CBN_SELCHANGE) {
                int idx = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                UpdateConfigControls((NodeType)idx);
            }
            else if (LOWORD(wParam) == ID_CFG_CANCEL) {
                DestroyWindow(hWnd);
            }
            else if (LOWORD(wParam) == ID_CFG_SAVE) {
                // 1. 获取数据到 tempConfig
                char buf[1024];
                GetWindowText(hCfgName, tempConfig.configName, MAX_SMALL_LEN);
                tempConfig.nodeType = (NodeType)SendMessage(hCfgType, CB_GETCURSEL, 0, 0);
                GetWindowText(hCfgServer, tempConfig.server, MAX_URL_LEN);
                GetWindowText(hCfgListen, tempConfig.listen, MAX_SMALL_LEN);
                GetWindowText(hCfgToken, tempConfig.token, MAX_URL_LEN);
                GetWindowText(hCfgIp, tempConfig.ip, MAX_SMALL_LEN);
                GetWindowText(hCfgDns, tempConfig.dns, MAX_SMALL_LEN);
                GetWindowText(hCfgEch, tempConfig.ech, MAX_SMALL_LEN);
                GetWindowText(hCfgConn, buf, 32); tempConfig.connections = atoi(buf);
                tempConfig.fallback = (SendMessage(hCfgFb, BM_GETCHECK, 0, 0) == BST_CHECKED);
                tempConfig.isManual = TRUE; // 只要是通过配置窗口保存，就是手动节点

                if (strlen(tempConfig.configName) == 0) {
                    MessageBox(hWnd, "名称不能为空", "错误", MB_OK|MB_ICONERROR);
                    return 0;
                }
                
                // 2. 更新内存列表
                if (g_editingIndex == -1) {
                    // 新增
                    if (g_listCount < MAX_NODES) {
                        g_nodeList[g_listCount] = tempConfig;
                        g_listCount++;
                        AppendLog("[系统] 添加新节点: %s\r\n", tempConfig.configName);
                    } else {
                        MessageBox(hWnd, "节点数量已达上限", "错误", MB_OK|MB_ICONERROR);
                        return 0;
                    }
                } else {
                    // 修改
                    g_nodeList[g_editingIndex] = tempConfig;
                    AppendLog("[系统] 修改节点: %s\r\n", tempConfig.configName);
                }
                
                // 3. 自动保存到 JSON
                NodeListSaveToJSON();
                
                // 4. 刷新 UI
                NodeListUpdateUI();
                
                // 5. 选中当前节点并更新 currentConfig
                int selIndex = (g_editingIndex == -1) ? g_listCount - 1 : g_editingIndex;
                SendMessage(hNodeList, LB_SETCURSEL, selIndex, 0);
                LoadConfigFromList(selIndex, &currentConfig);

                DestroyWindow(hWnd);
            }
            break;
        case WM_DESTROY:
            EnableWindow(hMainWindow, TRUE);
            SetForegroundWindow(hMainWindow);
            hConfigWnd = NULL;
            break;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void CreateLabel(HWND parent, const char* text, int x, int y, int w, int h) {
    HWND h = CreateWindow("STATIC", text, WS_CHILD|WS_VISIBLE|SS_LEFT, Scale(x), Scale(y), Scale(w), Scale(h), parent, NULL, hInst, NULL);
    SendMessage(h, WM_SETFONT, (WPARAM)hFontUI, TRUE);
}

HWND CreateEdit(HWND parent, int x, int y, int w, int h) {
    HWND hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, 
        Scale(x), Scale(y), Scale(w), Scale(h), parent, NULL, hInst, NULL);
    SendMessage(hEdit, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    return hEdit;
}

void OpenConfigWindow(int index) {
    if (hConfigWnd) { SetForegroundWindow(hConfigWnd); return; }

    g_editingIndex = index;
    Config* initialCfg = (Config*)malloc(sizeof(Config));
    memset(initialCfg, 0, sizeof(Config));

    if (index >= 0) {
        // 修改：从内存列表加载
        *initialCfg = g_nodeList[index];
    } else {
        // 新增：默认值
        initialCfg->nodeType = NODE_TYPE_ECHW;
        strcpy(initialCfg->listen, "127.0.0.1:30000");
        initialCfg->connections = 3;
        strcpy(initialCfg->dns, "dns.alidns.com/dns-query");
        strcpy(initialCfg->ech, "cloudflare-ech.com");
    }

    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = ConfigWndProc;
    wc.hInstance = hInst;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = "ConfigWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassEx(&wc);

    hConfigWnd = CreateWindowEx(WS_EX_DLGMODALFRAME|WS_EX_TOPMOST, "ConfigWnd", 
        (index == -1 ? "添加节点" : "修改配置"), 
        WS_VISIBLE|WS_SYSMENU|WS_CAPTION, 
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(450), Scale(550), 
        hMainWindow, NULL, hInst, initialCfg);

    EnableWindow(hMainWindow, FALSE); 

    int y = 20; int lh = 30; int lw = 80; int ew = 300;
    
    CreateLabel(hConfigWnd, "名称:", 20, y, lw, 20);
    hCfgName = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgName, initialCfg->configName); y += lh + 10;

    CreateLabel(hConfigWnd, "类型:", 20, y, lw, 20);
    hCfgType = CreateWindow("COMBOBOX", "", WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_TABSTOP, Scale(110), Scale(y), Scale(ew), Scale(100), hConfigWnd, (HMENU)ID_CFG_TYPE, hInst, NULL);
    SendMessage(hCfgType, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hCfgType, CB_ADDSTRING, 0, (LPARAM)"ECH Tunnel (ech-tunnel.exe)");
    SendMessage(hCfgType, CB_ADDSTRING, 0, (LPARAM)"ECH Worker (ech-workers.exe)");
    SendMessage(hCfgType, CB_SETCURSEL, initialCfg->nodeType, 0); y += lh + 10;

    CreateLabel(hConfigWnd, "服务地址:", 20, y, lw, 20);
    hCfgServer = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgServer, initialCfg->server); y += lh + 10;

    CreateLabel(hConfigWnd, "监听地址:", 20, y, lw, 20);
    hCfgListen = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgListen, initialCfg->listen); y += lh + 10;

    CreateLabel(hConfigWnd, "Token:", 20, y, lw, 20);
    hCfgToken = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgToken, initialCfg->token); y += lh + 10;

    CreateLabel(hConfigWnd, "优选IP:", 20, y, lw, 20);
    hCfgIp = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgIp, initialCfg->ip); y += lh + 10;

    CreateLabel(hConfigWnd, "DNS:", 20, y, lw, 20);
    hCfgDns = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgDns, initialCfg->dns); y += lh + 10;

    CreateLabel(hConfigWnd, "ECH域名:", 20, y, lw, 20);
    hCfgEch = CreateEdit(hConfigWnd, 110, y, ew, 24); SetWindowText(hCfgEch, initialCfg->ech); y += lh + 10;

    CreateLabel(hConfigWnd, "并发连接:", 20, y, lw, 20);
    hCfgConn = CreateEdit(hConfigWnd, 110, y, ew, 24); 
    char tmp[16]; sprintf(tmp, "%d", initialCfg->connections); SetWindowText(hCfgConn, tmp); y += lh + 10;

    hCfgFb = CreateWindow("BUTTON", "禁用ECH (回退模式 - 仅Tunnel)", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, 
        Scale(110), Scale(y), Scale(300), Scale(24), hConfigWnd, NULL, hInst, NULL);
    SendMessage(hCfgFb, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessage(hCfgFb, BM_SETCHECK, initialCfg->fallback ? BST_CHECKED : BST_UNCHECKED, 0); y += lh + 20;

    CreateWindow("BUTTON", "保存", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(100), Scale(y), Scale(100), Scale(35), hConfigWnd, (HMENU)ID_CFG_SAVE, hInst, NULL);
    CreateWindow("BUTTON", "取消", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(250), Scale(y), Scale(100), Scale(35), hConfigWnd, (HMENU)ID_CFG_CANCEL, hInst, NULL);

    UpdateConfigControls(initialCfg->nodeType);
    
    free(initialCfg);
}

// ================= 主窗口界面 (与上版本相同) =================

void CreateMainControls(HWND hWnd) {
    int w = Scale(900), h = Scale(600);
    
    CreateWindow("STATIC", "节点列表:", WS_CHILD|WS_VISIBLE, Scale(10), Scale(10), Scale(100), Scale(20), hWnd, NULL, hInst, NULL);
    hNodeList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT, 
        Scale(10), Scale(35), Scale(280), Scale(h - 180), hWnd, (HMENU)ID_LIST_NODES, hInst, NULL);
    SendMessage(hNodeList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    int btnY = h - 130;
    int btnW = 80;
    CreateWindow("BUTTON", "添加节点", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(10), Scale(btnY), Scale(btnW), Scale(30), hWnd, (HMENU)ID_BTN_ADD_NODE, hInst, NULL);
    hEditNodeBtn = CreateWindow("BUTTON", "修改配置", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(10 + btnW + 10), Scale(btnY), Scale(btnW), Scale(30), hWnd, (HMENU)ID_BTN_EDIT_NODE, hInst, NULL);
    CreateWindow("BUTTON", "删除", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(10 + (btnW+10)*2), Scale(btnY), Scale(btnW), Scale(30), hWnd, (HMENU)ID_BTN_DEL_NODE, hInst, NULL);

    btnY += 40;
    hStartBtn = CreateWindow("BUTTON", "启动代理", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(10), Scale(btnY), Scale(130), Scale(40), hWnd, (HMENU)ID_BTN_START, hInst, NULL);
    hStopBtn = CreateWindow("BUTTON", "停止", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(150), Scale(btnY), Scale(130), Scale(40), hWnd, (HMENU)ID_BTN_STOP, hInst, NULL);
    EnableWindow(hStopBtn, FALSE);

    int rx = 310;
    int rw = w - 340;
    
    CreateWindow("STATIC", "订阅链接:", WS_CHILD|WS_VISIBLE, Scale(rx), Scale(10), Scale(80), Scale(20), hWnd, NULL, hInst, NULL);
    hSubUrlEdit = CreateEdit(hWnd, rx + 80, 10, rw - 180, 22);
    CreateWindow("BUTTON", "添加", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(rx + rw - 90), Scale(10), Scale(40), Scale(22), hWnd, (HMENU)ID_BTN_SUB_ADD, hInst, NULL);
    CreateWindow("BUTTON", "删", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(rx + rw - 45), Scale(10), Scale(40), Scale(22), hWnd, (HMENU)ID_BTN_SUB_DEL, hInst, NULL);

    hSubList = CreateWindowEx(WS_EX_CLIENTEDGE, "LISTBOX", NULL, WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY, 
        Scale(rx), Scale(35), Scale(rw), Scale(100), hWnd, (HMENU)ID_LIST_SUBS, hInst, NULL);
    SendMessage(hSubList, WM_SETFONT, (WPARAM)hFontUI, TRUE);

    CreateWindow("BUTTON", "更新所有订阅", WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, Scale(rx), Scale(140), Scale(120), Scale(28), hWnd, (HMENU)ID_BTN_SUB_UPD, hInst, NULL);

    CreateWindow("STATIC", "运行日志:", WS_CHILD|WS_VISIBLE, Scale(rx), Scale(180), Scale(100), Scale(20), hWnd, NULL, hInst, NULL);
    hLogEdit = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD|WS_VISIBLE|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
        Scale(rx), Scale(200), Scale(rw), Scale(h - 260), hWnd, (HMENU)ID_EDIT_LOG, hInst, NULL);
    SendMessage(hLogEdit, WM_SETFONT, (WPARAM)hFontLog, TRUE);
}

void ResizeLayout(int w, int h) {
    if(!hNodeList) return;
    
    MoveWindow(hNodeList, Scale(10), Scale(35), Scale(280), Scale(h - 180), TRUE);
    int btnY = h - 130;
    int btnW = 80;
    
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_ADD_NODE), Scale(10), Scale(btnY), Scale(btnW), Scale(30), TRUE);
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_EDIT_NODE), Scale(10 + btnW + 10), Scale(btnY), Scale(btnW), Scale(30), TRUE);
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_DEL_NODE), Scale(10 + (btnW+10)*2), Scale(btnY), Scale(btnW), Scale(30), TRUE);
    btnY += 40;
    MoveWindow(hStartBtn, Scale(10), Scale(btnY), Scale(130), Scale(40), TRUE);
    MoveWindow(hStopBtn, Scale(150), Scale(btnY), Scale(130), Scale(40), TRUE);

    int rx = 310;
    int rw = w - 340;
    if (rw < 100) rw = 100;
    
    MoveWindow(hSubUrlEdit, Scale(rx + 80), Scale(10), Scale(rw - 180), Scale(22), TRUE);
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_SUB_ADD), Scale(rx + rw - 90), Scale(10), Scale(40), Scale(22), TRUE);
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_SUB_DEL), Scale(rx + rw - 45), Scale(10), Scale(40), Scale(22), TRUE);
    MoveWindow(hSubList, Scale(rx), Scale(35), Scale(rw), Scale(100), TRUE);
    MoveWindow(GetDlgItem(hMainWindow, ID_BTN_SUB_UPD), Scale(rx), Scale(140), Scale(120), Scale(28), TRUE);
    MoveWindow(hLogEdit, Scale(rx), Scale(200), Scale(rw), Scale(h - 250), TRUE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        CreateMainControls(hWnd);
        LoadSubscriptionList();
        NodeListLoadFromJSON();
        NodeListUpdateUI();
        if (g_listCount > 0) {
            SendMessage(hNodeList, LB_SETCURSEL, 0, 0);
            LoadConfigFromList(0, &currentConfig);
        }
        break;

    case WM_SIZE:
        ResizeLayout(LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_APPEND_LOG:
        if (lParam) {
            char* text = (char*)lParam;
            int len = GetWindowTextLength(hLogEdit);
            SendMessage(hLogEdit, EM_SETSEL, len, len);
            SendMessage(hLogEdit, EM_REPLACESEL, 0, (LPARAM)text);
            free(text);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case ID_BTN_ADD_NODE:
                OpenConfigWindow(-1);
                break;
            case ID_BTN_EDIT_NODE:
                {
                    int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                    if (sel == LB_ERR) MessageBox(hWnd, "请先选择一个节点", "提示", MB_OK);
                    else OpenConfigWindow(sel);
                }
                break;
            case ID_BTN_DEL_NODE:
                {
                    int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                    if (sel == LB_ERR) return 0;
                    if (MessageBox(hWnd, "确定删除选中节点？此操作不可撤销！", "确认", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        
                        // 从内存中移除节点
                        for (int i = sel; i < g_listCount - 1; i++) {
                            g_nodeList[i] = g_nodeList[i + 1];
                        }
                        g_listCount--;
                        
                        // 调整 subscribedCount
                        if (sel < g_subscribedCount) {
                            g_subscribedCount--;
                        }
                        
                        // 保存并刷新
                        NodeListSaveToJSON();
                        NodeListUpdateUI();
                        
                        // 选中新列表中的第一个节点
                        if (g_listCount > 0) {
                            SendMessage(hNodeList, LB_SETCURSEL, 0, 0);
                            LoadConfigFromList(0, &currentConfig);
                        } else {
                            memset(&currentConfig, 0, sizeof(Config));
                        }
                    }
                }
                break;
            case ID_BTN_START:
                if (!isProcessRunning) StartProcess();
                break;
            case ID_BTN_STOP:
                StopProcess();
                break;
            case ID_BTN_SUB_ADD:
                {
                    char url[MAX_URL_LEN];
                    GetWindowText(hSubUrlEdit, url, sizeof(url));
                    if(strlen(url)==0) return 0;
                    
                    if (strstr(url, "://")) {
                        // 认为是 ech/echw 链接，直接解析并添加到手动节点
                        ParseSubscriptionData(url, TRUE); 
                    } else {
                        // 认为是订阅 URL，添加到订阅列表
                        SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)url);
                        SaveSubscriptionList();
                    }
                    SetWindowText(hSubUrlEdit, "");
                }
                break;
            case ID_BTN_SUB_DEL:
                {
                    int sel = SendMessage(hSubList, LB_GETCURSEL, 0, 0);
                    if(sel != LB_ERR) { SendMessage(hSubList, LB_DELETESTRING, sel, 0); SaveSubscriptionList(); }
                }
                break;
            case ID_BTN_SUB_UPD:
                FetchAllSubscriptions();
                break;
            case ID_LIST_NODES:
                if (HIWORD(wParam) == LBN_SELCHANGE) {
                    int sel = SendMessage(hNodeList, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) LoadConfigFromList(sel, &currentConfig);
                }
                break;
        }
        break;

    case WM_DESTROY:
        StopProcess();
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// ================= 订阅功能实现 =================

void SaveSubscriptionList() {
    FILE* f = fopen("subs.txt", "w");
    if(!f) return;
    int cnt = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    for(int i=0; i<cnt; i++) {
        char url[MAX_URL_LEN]; SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        fprintf(f, "%s\n", url);
    }
    fclose(f);
}
void LoadSubscriptionList() {
    FILE* f = fopen("subs.txt", "r");
    if(!f) return;
    char line[MAX_URL_LEN];
    while(fgets(line, sizeof(line), f)) {
        while(strlen(line)>0 && (line[strlen(line)-1]=='\r'||line[line[strlen(line)-1]=='\n'])) line[strlen(line)-1]=0;
        if(strlen(line)>0) SendMessage(hSubList, LB_ADDSTRING, 0, (LPARAM)line);
    }
    fclose(f);
}

void ParseSubscriptionData(const char* data, BOOL isManualAddition) {
    if(!data) return;
    char* copy = NULL;
    // 检查是否是 Base64 编码
    if (!strstr(data, "://") && !strchr(data, ' ')) {
        size_t len;
        copy = Base64Decode(data, &len);
        if(!copy) return;
    } else {
        copy = strdup(data);
    }

    char* token = strtok(copy, "\r\n");
    int added = 0;
    while(token) {
        // 跳过空行和注释
        if(strlen(token) > 0 && token[0] != '#') {
            char* line = token;
            if(strncmp(line, "ech://", 6)==0 || strncmp(line, "echw://", 7)==0) {
                if (g_listCount >= MAX_NODES) {
                    AppendLog("[警告] 节点列表已满，跳过添加。\r\n");
                    break;
                }
                
                NodeType type = (strncmp(line, "ech://", 6)==0) ? NODE_TYPE_ECH : NODE_TYPE_ECHW;
                int prefix = (type == NODE_TYPE_ECH) ? 6 : 7;
                
                Config cfg; memset(&cfg, 0, sizeof(cfg));
                cfg.nodeType = type;
                cfg.connections = 3; 
                strcpy(cfg.listen, "127.0.0.1:30000");
                strcpy(cfg.dns, "dns.alidns.com/dns-query");
                strcpy(cfg.ech, "cloudflare-ech.com");
                cfg.isManual = isManualAddition; // 核心区别

                // 解析 Name
                char* hash = strchr(line, '#');
                if(hash) {
                    *hash = 0;
                    char* dec = URLDecode(hash+1);
                    char* gbk = UTF8ToGBK(dec);
                    if (gbk) {
                        strncpy(cfg.configName, gbk, MAX_SMALL_LEN - 1);
                        free(gbk);
                    } else {
                        strncpy(cfg.configName, dec, MAX_SMALL_LEN - 1);
                    }
                    free(dec);
                } else {
                    sprintf(cfg.configName, "未命名节点_%d", g_listCount + 1);
                }

                // 解析字段 server|token|ip|dns|ech
                char* p = line + prefix;
                char* start = p;
                int idx = 0;
                while(1) {
                    if(*p == '|' || *p == 0) {
                        char val[MAX_URL_LEN] = {0};
                        int len = p - start;
                        if(len < MAX_URL_LEN) { strncpy(val, start, len); }
                        
                        // URL Decode (通常只需要对 Name/Server/Token 进行)
                        char* dec = URLDecode(val);

                        if(idx==0) strncpy(cfg.server, dec, MAX_URL_LEN - 1);
                        else if(idx==1) strncpy(cfg.token, dec, MAX_URL_LEN - 1);
                        else if(idx==2) strncpy(cfg.ip, dec, MAX_SMALL_LEN - 1);
                        else if(idx==3 && strlen(dec)>0) strncpy(cfg.dns, dec, MAX_SMALL_LEN - 1);
                        else if(idx==4 && strlen(dec)>0) strncpy(cfg.ech, dec, MAX_SMALL_LEN - 1);
                        
                        free(dec);

                        if(*p == 0) break;
                        start = p + 1;
                        idx++;
                    }
                    p++;
                }

                // 保存到内存
                g_nodeList[g_listCount] = cfg;
                g_listCount++;
                if (!isManualAddition) g_subscribedCount++;
                
                added++;
            }
        }
        token = strtok(NULL, "\r\n");
    }
    if(copy) free(copy);
    AppendLog("解析完成，添加 %d 个节点\r\n", added);
}


void FetchAllSubscriptions() {
    int cnt = SendMessage(hSubList, LB_GETCOUNT, 0, 0);
    if(cnt == 0) return;

    AppendLog("--------------------------\r\n");
    AppendLog("[系统] 开始更新订阅...\r\n");
    
    // 1. 清空旧的订阅节点 (保留手动节点，即 g_nodeList[g_subscribedCount] 及以后)
    for (int i = 0; i < g_listCount - g_subscribedCount; i++) {
        g_nodeList[i] = g_nodeList[g_subscribedCount + i];
    }
    g_listCount -= g_subscribedCount;
    g_subscribedCount = 0; // 重置订阅计数

    // 2. 循环获取
    HINTERNET hInt = InternetOpen("ECHClient", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if(!hInt) { AppendLog("[错误] InternetOpen失败。\r\n"); return; }

    for(int i=0; i<cnt; i++) {
        char url[MAX_URL_LEN]; SendMessage(hSubList, LB_GETTEXT, i, (LPARAM)url);
        AppendLog("获取: %s\r\n", url);
        HINTERNET hUrl = InternetOpenUrl(hInt, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
        if(hUrl) {
            char* buf = malloc(512*1024); 
            DWORD read = 0, total = 0;
            while(InternetReadFile(hUrl, buf+total, 4096, &read) && read > 0) total += read;
            buf[total] = 0;
            InternetCloseHandle(hUrl);
            
            // 解析并添加到内存。isManualAddition=FALSE
            ParseSubscriptionData(buf, FALSE); 
            free(buf);
        } else {
             AppendLog("[错误] 获取订阅失败。\r\n");
        }
    }
    InternetCloseHandle(hInt);
    
    // 3. 自动保存并刷新 UI
    NodeListSaveToJSON();
    NodeListUpdateUI();
    
    AppendLog("[系统] 订阅更新完毕。\r\n");
}


// ================= WinMain 入口 (与上版本相同) =================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutex(NULL, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    hInst = hInstance;
    
    HMODULE hUser32 = LoadLibrary("user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDPIAwareFunc)(void);
        SetProcessDPIAwareFunc setDPIAware = (SetProcessDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (setDPIAware) setDPIAware();
        FreeLibrary(hUser32);
    }
    HDC hdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    g_scale = (g_dpi * 100) / 96;
    ReleaseDC(NULL, hdc);

    hFontUI = CreateFont(Scale(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Microsoft YaHei");
    hFontLog = CreateFont(Scale(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, 0, 0, 0, 0, "Consolas");

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "ECHMainWnd";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClass(&wc);

    hMainWindow = CreateWindow("ECHMainWnd", APP_TITLE, WS_OVERLAPPEDWINDOW, 
        CW_USEDEFAULT, CW_USEDEFAULT, Scale(900), Scale(600), NULL, NULL, hInstance, NULL);

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}
