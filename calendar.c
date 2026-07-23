/* ============================================================================
 * CSV Calendar - C Implementation with Network Sync
 * 
 * COMPILATION INSTRUCTIONS:
 * 
 * With GCC (MinGW-w64):
 *   gcc -Os -s -o calendar.exe calendar.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32 -liphlpapi -lws2_32 -mwindows
 * 
 * REQUIREMENTS: Windows XP or later
 * DEPENDENCIES: Win32 API only (GDI32, USER32, COMDLG32, OLE32, WS2_32)
 * 
 * FEATURES:
 * - Network synchronization (TCP server/client)
 * - INI configuration persistence
 * - Event versioning and conflict resolution
 * - Delete threshold cleanup
 * 
 * THIS WORK IS NOT FIT FOR ANY FUNCTION OR PURPOSE, COMES WITH NO WARRANTY,
 * AND IS BEING RELEASED INTO THE PUBLIC DOMAIN.
 * ============================================================================ */

#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h> /* Fixed: Added for va_list */
#include <winsock2.h>
#include <iphlpapi.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

/* --- Zoom & Canvas Configuration --- */
float fZoom = 1.0f;
int iCanvasWidth = 1100;
int iCanvasHeight = 1440;
const int iTimeColWidth = 65;
const int iHeaderH = 50;
const int iSubHeaderH = 30;

int iScrollX = 0, iScrollY = 300;
int iClientW = 1050, iClientH = 700;

/* --- View & Date State --- */
int iViewMode = 1;
char sCurrentDate[16];
const char* aPeople[] = {"Alice", "Bob", "Charlie", "David", "Eve", "Frank", "Grace"};
int numPeople = 7;

/* --- Network & Sync Configuration --- */
int iNetPort = 9876;
int iNetSyncIntervalMs = 180000;
int iNetLogging = 0;
int iNetDeleteThreshold = 100;
char aServers[8][16] = {{0}};
int iMyNodeID = 1;
int iSyncCycles = 0;

SOCKET hServerListen = INVALID_SOCKET;
DWORD iSyncTimer = 0;
FILE* hLogFile = NULL;

/* --- File I/O Configuration --- */
char sCSVFile[MAX_PATH];
char sINIFile[MAX_PATH];
char sLogFile[MAX_PATH];

/* --- Event Data Structure: [ID, Title, StartMin, Duration, RGBColor, DateStr, PersonIdx, Version, LastModifiedBy] --- */
typedef struct {
    char id[64];
    char title[1024];
    int startMin;
    int duration;
    COLORREF color;
    char date[16];
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

Event* aEvents = NULL;
int numEvents = 0;
int maxEvents = 0;

/* --- Interaction State Variables --- */
int iDragMode = 0;
int iDragIndex = -1;
int iDragOffsetY = 0;
int iOrigStart = 0, iOrigDuration = 0;
int bCopyTriggered = 0;
int iEditingIndex = -1;
int iSelectedForDelete = -1;

/* --- UI Handles --- */
HWND hMainGUI, hCanvas, hInPlaceEdit;
HWND hBtnZoomIn, hBtnZoomOut, hBtnPrev, hBtnNext, hBtnPrevDay, hBtnNextDay;
HWND hBtnPrint, hBtnExport, hBtnDelete, hComboView, hLblDateTitle;
HFONT hUIFont, hTitleFont;

/* --- Macros --- */
#define RGB_HEX(hex) RGB(((hex) >> 16) & 0xFF, ((hex) >> 8) & 0xFF, (hex) & 0xFF)
#define CONTRAST_COLOR(c) (((GetRValue(c)*299 + GetGValue(c)*587 + GetBValue(c)*114)/1000 > 125) ? RGB(0,0,0) : RGB(255,255,255))
#define DARKEN(c, p) RGB(GetRValue(c)*(100-p)/100, GetGValue(c)*(100-p)/100, GetBValue(c)*(100-p)/100)

/* --- Function Prototypes --- */
void UpdateScrollBars();
void UpdateDateTitle();
void SetZoom(float fNewZoom);
void OpenInPlaceEdit(int eIdx);
void CloseInPlaceEdit(int bSave);
void LoadCSV();
void SaveCSV();
void LoadINI();
void SaveINI();
void LogMessage(const char* msg, ...);
void DrawCalendar(HDC hDC);
void DrawTimelineView(HDC hDC, int w, int h);
void DrawMonthView(HDC hDC, int w, int h);
void DrawUpcomingView(HDC hDC, int w, int h);
void PrintSchedule();
void ExportUpcomingSchedule();
void AddEvent(const char* id, const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx, int ver, int modBy);
void MarkEventModified(int idx);
int GetEventColumnIndex(int eIdx);
int IsPeopleView();
int GetColCount();
void GetCalcDate(time_t t, char* out);
void DateAdd(char* ioDate, char unit, int amt);
int DateDiffDays(const char* d1, const char* d2);
int GetDayOfWeek(int y, int m, int d);
int GetDaysInMonth(int y, int m);
void MinToTimeString(int min, char* out);
void FormatDayHeader(const char* inDate, char* outStr);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CanvasWndProc(HWND, UINT, WPARAM, LPARAM);

/* Fixed: Added missing prototypes to resolve compilation errors */
void InitializeNetwork();
void GetLocalIpAddress(char* out);
void HandleServerClient(SOCKET clientSock);
int RunClientSyncs();
void MergeCsvData(const char* newData);
int ProcessDeleteThreshold();
void GetEventScreenRect(int eIdx, RECT* r);
void PrintTimelineVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintMonthVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintUpcomingVector(HDC hDC, RECT rPage, int dpiX, int dpiY);


/* === NETWORK SYNCHRONIZATION === */

void InitializeNetwork() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(iNetPort);
    
    hServerListen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (hServerListen == INVALID_SOCKET) {
        LogMessage("Failed to create socket.");
        return;
    }
    
    if (bind(hServerListen, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        LogMessage("Failed to bind socket.");
        closesocket(hServerListen);
        hServerListen = INVALID_SOCKET;
        return;
    }
    
    if (listen(hServerListen, 7) == SOCKET_ERROR) {
        LogMessage("Failed to listen.");
        closesocket(hServerListen);
        hServerListen = INVALID_SOCKET;
        return;
    }
    
    char ip[16];
    GetLocalIpAddress(ip);
    LogMessage("Service started. Node ID: %d | Listening on port: %d | IP: %s", iMyNodeID, iNetPort, ip);
    
    iSyncTimer = GetTickCount();
}

void GetLocalIpAddress(char* out) {
    PIP_ADAPTER_INFO adapterInfo = NULL;
    ULONG bufferSize = sizeof(IP_ADAPTER_INFO);
    
    if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
        adapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
    }
    
    if (GetAdaptersInfo(adapterInfo, &bufferSize) == NO_ERROR && adapterInfo) {
        PIP_ADDR_STRING addr = &adapterInfo->IpAddressList;
        strcpy(out, addr->IpAddress.String);
        free(adapterInfo);
        return;
    }
    
    strcpy(out, "127.0.0.1");
}

void HandleServerClient(SOCKET clientSock) {
    /* Fixed: Reset accepted socket to blocking mode, otherwise recv() fails instantly with WSAEWOULDBLOCK */
    u_long mode = 0; 
    ioctlsocket(clientSock, FIONBIO, &mode);
    
    char buffer[65536] = {0};
    char recvBuf[4096];
    int totalRecv = 0;
    time_t startTime = time(NULL);
    
    while (1) {
        int bytes = recv(clientSock, recvBuf, sizeof(recvBuf) - 1, 0);
        if (bytes <= 0) break;
        
        /* Fixed: Prevent malicious payload buffer overflow */
        if (totalRecv + bytes >= (int)sizeof(buffer) - 1) break;
        
        memcpy(buffer + totalRecv, recvBuf, bytes);
        totalRecv += bytes;
        buffer[totalRecv] = '\0';
        
        if (strstr(buffer, "[EOF]")) break;
        if (difftime(time(NULL), startTime) > 10) break;
    }
    
    if (strstr(buffer, "[EOF]")) {
        char* eofPos = strstr(buffer, "[EOF]");
        *eofPos = '\0';
    }
    
    if (strlen(buffer) == 0) {
        LogMessage("Server: Received empty payload.");
        closesocket(clientSock);
        return;
    }
    
    char cmd[64] = {0};
    int clientNodeID = 0;
    sscanf(buffer, "%63[^¦]|%d", cmd, &clientNodeID);
    
    LogMessage("Server: Processing '%s' from Node %d", cmd, clientNodeID);
    
    FILE* fp = fopen(sCSVFile, "r");
    if (!fp) {
        const char* resp = "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n[EOF]";
        send(clientSock, resp, strlen(resp), 0);
        closesocket(clientSock);
        return;
    }
    
    char header[256];
    fgets(header, sizeof(header), fp);
    
    if (strcmp(cmd, "CLIENT_SYNC_ALL") == 0) {
        char response[65536] = {0};
        sprintf(response, "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n");
        
        char line[2048];
        while (fgets(line, sizeof(line), fp)) {
            strcat(response, line);
        }
        strcat(response, "[EOF]");
        send(clientSock, response, strlen(response), 0);
        fclose(fp);
        closesocket(clientSock);
        LogMessage("Server: Sent full database.");
        return;
    }
    
    char response[65536] = {0};
    sprintf(response, "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n");
    
    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        char id[64], dt[16];
        int ver, modBy;
        sscanf(line, "%63[^¦]|%[^|]|%*d|%*d|%*d|%[^|]|%*d|%d|%d", id, dt, dt, &ver, &modBy);
        
        if (modBy == clientNodeID) continue;
        
        char* pos = buffer;
        if (strstr(pos, "[EOF]")) *strstr(pos, "[EOF]") = '\0';
        
        int bShouldSend = 1;
        while ((pos = strstr(pos, "|")) != NULL) {
            char idCheck[64];
            int verCheck;
            sscanf(pos + 1, "%63[^¦]|%d", idCheck, &verCheck);
            if (strcmp(id, idCheck) == 0 && verCheck >= ver) {
                bShouldSend = 0;
                break;
            }
        }
        
        if (bShouldSend) {
            strcat(response, line);
        }
    }
    fclose(fp);
    
    strcat(response, "[EOF]");
    send(clientSock, response, strlen(response), 0);
    closesocket(clientSock);
    LogMessage("Server: Sent delta data.");
}

/* Fixed: Changed return type from void to int to allow boolean evaluation */
int RunClientSyncs() {
    int bMerged = 0;
    for (int i = 1; i <= 7; i++) {
        if (strlen(aServers[i]) == 0 || strcmp(aServers[i], "0") == 0) continue;
        
        struct hostent* host = gethostbyname(aServers[i]);
        if (!host) continue;
        
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr = *((struct in_addr*)host->h_addr);
        server.sin_port = htons(iNetPort);
        
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) continue;
        
        if (connect(sock, (struct sockaddr*)&server, sizeof(server)) != 0) {
            closesocket(sock);
            continue;
        }
        
        char payload[65536] = {0};
        FILE* fp = fopen(sCSVFile, "r");
        if (!fp) {
            sprintf(payload, "CLIENT_SYNC_ALL|%d\n[EOF]", iMyNodeID);
        } else {
            char header[256];
            fgets(header, sizeof(header), fp);
            sprintf(payload, "CLIENT_SYNC|%d\n", iMyNodeID);
            
            char line[2048];
            while (fgets(line, sizeof(line), fp)) {
                strcat(payload, line);
            }
            fclose(fp);
            strcat(payload, "[EOF]");
        }
        
        send(sock, payload, strlen(payload), 0);
        
        char response[65536] = {0};
        char recvBuf[4096];
        int totalRecv = 0;
        time_t startTime = time(NULL);
        
        while (1) {
            int bytes = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
            if (bytes <= 0) break;
            
            /* Fixed: Protect against memory overflows */
            if (totalRecv + bytes >= (int)sizeof(response) - 1) break;
            
            memcpy(response + totalRecv, recvBuf, bytes);
            totalRecv += bytes;
            response[totalRecv] = '\0';
            
            if (strstr(response, "[EOF]")) break;
            if (difftime(time(NULL), startTime) > 10) break;
        }
        
        closesocket(sock);
        
        if (strstr(response, "[EOF]")) {
            char* eofPos = strstr(response, "[EOF]");
            *eofPos = '\0';
            
            MergeCsvData(response);
            bMerged = 1;
        }
        
        LogMessage("Client: Sync completed with server %s", aServers[i]);
    }
    return bMerged;
}

void MergeCsvData(const char* newData) {
    char* temp = strdup(newData);
    
    /* Fixed: strtok_s parameter misuse caused crash. Replaced with standard strtok to ensure proper parsing across GCC / Win32. */
    char* line = strtok(temp, "\n");
    
    if (!line || strncmp(line, "ID", 2) != 0) {
        free(temp);
        return;
    }
    
    FILE* fp = fopen(sCSVFile, "r");
    if (!fp) {
        fp = fopen(sCSVFile, "w");
        fprintf(fp, "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n");
        fclose(fp);
    } else {
        fclose(fp);
    }
    
    char merged[65536] = {0};
    strcat(merged, "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n");
    
    FILE* oldFp = fopen(sCSVFile, "r");
    if (oldFp) {
        char oldLine[2048];
        fgets(oldLine, sizeof(oldLine), oldFp);
        
        char oldIds[100][64];
        int oldVers[100];
        int oldModBy[100];
        int oldCount = 0;
        
        while (fgets(oldLine, sizeof(oldLine), oldFp) && oldCount < 100) {
            sscanf(oldLine, "%63[^¦]|%[^|]|%*d|%*d|%*d|%[^|]|%*d|%d|%d", 
                   oldIds[oldCount], oldLine, oldLine, &oldVers[oldCount], &oldModBy[oldCount]);
            oldCount++;
        }
        fclose(oldFp);
        
        char* newDataLine = strtok(NULL, "\n");
        while (newDataLine) {
            if (strlen(newDataLine) == 0 || strncmp(newDataLine, "ID", 2) == 0) {
                newDataLine = strtok(NULL, "\n");
                continue;
            }
            
            char id[64], date[16];
            int ver, modBy;
            sscanf(newDataLine, "%63[^¦]|%[^|]|%*d|%*d|%*d|%[^|]|%*d|%d|%d", id, date, date, &ver, &modBy);
            
            int found = 0;
            for (int i = 0; i < oldCount; i++) {
                if (strcmp(oldIds[i], id) == 0) {
                    found = 1;
                    if (ver > oldVers[i] || (ver == oldVers[i] && modBy > oldModBy[i])) {
                        strcat(merged, newDataLine);
                        strcat(merged, "\n");
                    } else {
                        strcat(merged, oldLine);
                        strcat(merged, "\n");
                    }
                    break;
                }
            }
            
            if (!found) {
                strcat(merged, newDataLine);
                strcat(merged, "\n");
            }
            
            newDataLine = strtok(NULL, "\n");
        }
    }
    
    fp = fopen(sCSVFile, "w");
    if (fp) {
        fputs(merged, fp);
        fclose(fp);
    }
    
    free(temp);
    LogMessage("Client: Merge completed.");
}

/* Fixed: Changed return type from void to int to allow boolean evaluation */
int ProcessDeleteThreshold() {
    FILE* fp = fopen(sCSVFile, "r");
    if (!fp) return 0;
    
    char* temp = malloc(65536);
    if (!temp) { fclose(fp); return 0; }
    
    char header[256];
    fgets(header, sizeof(header), fp);
    strcpy(temp, header);
    
    char line[2048];
    int delCount = 0;
    while (fgets(line, sizeof(line), fp)) {
        char color[16];
        sscanf(line, "%*[^|]|%*[^|]|%*d|%*d|%[^|]", color);
        
        if (strcmp(color, "2") != 0) {
            strcat(temp, line);
        } else {
            delCount++;
        }
    }
    fclose(fp);
    
    if (delCount > 0) {
        FILE* outFp = fopen(sCSVFile, "w");
        if (outFp) {
            fputs(temp, outFp);
            fclose(outFp);
            LogMessage("Threshold reached. Cleaned up %d event(s) with Color 2.", delCount);
            free(temp);
            return 1;
        }
    }
    
    free(temp);
    return 0;
}

void LogMessage(const char* fmt, ...) {
    if (!iNetLogging || !hLogFile) return;
    
    va_list args;
    va_start(args, fmt);
    
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    char timestamp[64];
    sprintf(timestamp, "[%04d-%02d-%02d %02d:%02d:%02d] ", 
            tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
            tm->tm_hour, tm->tm_min, tm->tm_sec);
    
    fprintf(hLogFile, "%s", timestamp);
    vfprintf(hLogFile, fmt, args);
    fprintf(hLogFile, "\n");
    fflush(hLogFile);
    
    va_end(args);
}

/* === INI FILE MANAGEMENT === */

void LoadINI() {
    FILE* fp = fopen(sINIFile, "r");
    if (!fp) return;
    
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char section[64] = {0};
        char key[64], val[128];
        
        if (sscanf(line, "[%63[^]]]", section) == 1) continue;
        if (sscanf(line, "%63[^=]=%127[^\n]", key, val) == 2) {
            if (strcmp(section, "Window") == 0) {
                // Window position/size could be restored here
            } else if (strcmp(section, "Network") == 0) {
                if (strcmp(key, "Port") == 0) iNetPort = atoi(val);
                else if (strcmp(key, "SyncIntervalMs") == 0) iNetSyncIntervalMs = atoi(val);
                else if (strcmp(key, "Logging") == 0) iNetLogging = atoi(val);
                else if (strcmp(key, "DeleteThreshold") == 0) iNetDeleteThreshold = atoi(val);
            } else if (strcmp(section, "Servers") == 0) {
                int idx = atoi(key + 6);
                if (idx >= 1 && idx <= 7) strncpy(aServers[idx], val, 15);
            }
        }
    }
    fclose(fp);
    
    char myIP[16];
    GetLocalIpAddress(myIP);
    for (int i = 1; i <= 7; i++) {
        if (strlen(aServers[i]) > 0 && strstr(myIP, aServers[i])) {
            iMyNodeID = i;
            break;
        }
    }
}

void SaveINI() {
    FILE* fp = fopen(sINIFile, "w");
    if (!fp) return;
    
    fprintf(fp, "[Window]\n");
    fprintf(fp, "Node=%d\n", iMyNodeID);
    fprintf(fp, "\n[Network]\n");
    fprintf(fp, "Port=%d\n", iNetPort);
    fprintf(fp, "SyncIntervalMs=%d\n", iNetSyncIntervalMs);
    fprintf(fp, "Logging=%d\n", iNetLogging);
    fprintf(fp, "DeleteThreshold=%d\n", iNetDeleteThreshold);
    fprintf(fp, "\n[Servers]\n");
    for (int i = 1; i <= 7; i++) {
        fprintf(fp, "Server%d=%s\n", i, aServers[i]);
    }
    
    fclose(fp);
}

/* === CORE EVENT MANAGEMENT === */

void AddEvent(const char* id, const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx, int ver, int modBy) {
    if (numEvents >= maxEvents) {
        maxEvents = maxEvents == 0 ? 32 : maxEvents * 2;
        aEvents = (Event*)realloc(aEvents, maxEvents * sizeof(Event));
    }
    strncpy(aEvents[numEvents].id, id && strlen(id) > 0 ? id : "", 63);
    strncpy(aEvents[numEvents].title, title, 1023);
    aEvents[numEvents].startMin = start;
    aEvents[numEvents].duration = dur;
    aEvents[numEvents].color = col;
    strncpy(aEvents[numEvents].date, dt, 15);
    aEvents[numEvents].personIdx = pIdx;
    aEvents[numEvents].version = ver;
    aEvents[numEvents].lastModifiedBy = modBy > 0 ? modBy : iMyNodeID;
    numEvents++;
}

void MarkEventModified(int idx) {
    if (idx >= 0 && idx < numEvents) {
        aEvents[idx].version++;
        aEvents[idx].lastModifiedBy = iMyNodeID;
    }
}

void ReplaceAll(char* str, const char* search, const char* replace) {
    char buffer[4096];
    char* p;
    if (!(p = strstr(str, search))) return;
    strncpy(buffer, str, p - str);
    buffer[p - str] = '\0';
    sprintf(buffer + (p - str), "%s%s", replace, p + strlen(search));
    strcpy(str, buffer);
    ReplaceAll(str, search, replace);
}

/* Safely extracts tokens, returning empty strings for consecutive delimiters */
char* GetNextCSVToken(char** context) {
    if (!context || !*context) return NULL;
    char* start = *context;
    
    // Using strstr is safer for multi-byte delimiters like "¦"
    char* end = strstr(start, "¦"); 
    
    if (end) {
        *end = '\0';
        *context = end + strlen("¦");
    } else {
        *context = NULL;
        // Clean up trailing newline characters on the final token
        char* nl = strpbrk(start, "\r\n");
        if (nl) *nl = '\0';
    }
    return start;
}

void LoadCSV() {
    FILE* fp = fopen(sCSVFile, "r");
    if (!fp) return;
    char line[2048];
    fgets(line, sizeof(line), fp); /* Skip header */
    
    while (fgets(line, sizeof(line), fp)) {
        char id[64] = {0}, title[1024] = {0}, dt[16] = {0};
        int start = 0, dur = 0, col = 0, pIdx = 0, ver = 1, modBy = -1;

        char* ctx = line;
        char* token;

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(id, token, 63);

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(title, token, 1023);

        token = GetNextCSVToken(&ctx);
        if (token) start = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token) dur = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token) col = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token) strncpy(dt, token, 15);

        token = GetNextCSVToken(&ctx);
        if (token) pIdx = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token && *token) ver = atoi(token);

        token = GetNextCSVToken(&ctx);
        if (token && *token) modBy = atoi(token);

        ReplaceAll(title, "%2C", "¦");
        ReplaceAll(title, "%0A", "\r\n");

        if (modBy < 0) modBy = iMyNodeID;
        AddEvent(id, title, start, dur, col, dt, pIdx, ver, modBy);
    }
    fclose(fp);
}

void SaveCSV() {
    FILE* fp = fopen(sCSVFile, "w");
    if (!fp) return;
    fprintf(fp, "ID¦Title¦StartMin¦Duration¦Color¦Date¦PersonIdx¦Version¦LastModifiedBy\n");
    for (int i = 0; i < numEvents; i++) {
        char title[1024];
        strcpy(title, aEvents[i].title);
        ReplaceAll(title, "¦", "%2C");
        ReplaceAll(title, "\r\n", "%0A");
        ReplaceAll(title, "\n", "%0A");
        fprintf(fp, "%s¦%s¦%d¦%d¦%d¦%s¦%d¦%d¦%d\n",
                aEvents[i].id, title, aEvents[i].startMin, aEvents[i].duration,
                aEvents[i].color, aEvents[i].date, aEvents[i].personIdx,
                aEvents[i].version, aEvents[i].lastModifiedBy);
    }
    fclose(fp);
}

/* --- Font Enumeration --- */
BOOL CALLBACK SetFontEnumProc(HWND hwnd, LPARAM lParam) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

/* --- Helper: Sorted Upcoming Indices --- */
typedef struct { int origIdx; char sortKey[32]; } UpcomingEntry;

int CompareUpcoming(const void* a, const void* b) {
    return strcmp(((UpcomingEntry*)a)->sortKey, ((UpcomingEntry*)b)->sortKey);
}

int GetSortedUpcomingIndices(int* outIndices, int maxOut) {
    UpcomingEntry entries[2048];
    int count = 0;
    for (int i = 0; i < numEvents && count < 2048; i++) {
        if (strcmp(aEvents[i].date, sCurrentDate) >= 0 && aEvents[i].color != 2) {
            entries[count].origIdx = i;
            sprintf(entries[count].sortKey, "%s%04d", aEvents[i].date, aEvents[i].startMin);
            count++;
        }
    }
    qsort(entries, count, sizeof(UpcomingEntry), CompareUpcoming);
    int outCount = (count < maxOut) ? count : maxOut;
    for (int i = 0; i < outCount; i++) outIndices[i] = entries[i].origIdx;
    return outCount;
}

/* --- Event Column Index --- */
int GetEventColumnIndex(int eIdx) {
    /* Fixed: Typo on function name, changed _IsPeopleView to IsPeopleView */
    if (IsPeopleView()) {
        if (strcmp(aEvents[eIdx].date, sCurrentDate) == 0 && aEvents[eIdx].personIdx < GetColCount())
            return aEvents[eIdx].personIdx;
    } else {
        int diff = DateDiffDays(sCurrentDate, aEvents[eIdx].date);
        if (diff >= 0 && diff < GetColCount()) return diff;
    }
    return -1;
}

int IsPeopleView() { return (iViewMode == 4 || iViewMode == 5); }

int GetColCount() {
    if (iViewMode == 1) return 1;
    if (iViewMode == 2 || iViewMode == 4) return 4;
    if (iViewMode == 3 || iViewMode == 5) return 7;
    return 1;
}

/* --- Date & Time Utilities --- */
void GetCalcDate(time_t t, char* out) {
    struct tm* tm = localtime(&t);
    sprintf(out, "%04d/%02d/%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
}

void DateAdd(char* ioDate, char unit, int amt) {
    int y, m, d;
    sscanf(ioDate, "%d/%d/%d", &y, &m, &d);
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    if (unit == 'd') t.tm_mday += amt;
    else if (unit == 'M') t.tm_mon += amt;
    mktime(&t);
    sprintf(ioDate, "%04d/%02d/%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

int DateDiffDays(const char* d1, const char* d2) {
    int y1, m1, dy1, y2, m2, dy2;
    sscanf(d1, "%d/%d/%d", &y1, &m1, &dy1);
    sscanf(d2, "%d/%d/%d", &y2, &m2, &dy2);
    struct tm t1 = {0}, t2 = {0};
    t1.tm_year = y1 - 1900; t1.tm_mon = m1 - 1; t1.tm_mday = dy1;
    t2.tm_year = y2 - 1900; t2.tm_mon = m2 - 1; t2.tm_mday = dy2;
    return (int)round(difftime(mktime(&t2), mktime(&t1)) / 86400.0);
}

int GetDayOfWeek(int y, int m, int d) {
    struct tm t = {0};
    t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d;
    mktime(&t);
    return t.tm_wday + 1;
}

int GetDaysInMonth(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 29 : 28;
    if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
    return 31;
}

void MinToTimeString(int min, char* out) {
    if (min < 0) min = 0;
    if (min > 1440) min = 1440;
    int h = min / 60;
    int m = min % 60;
    int ampm = (h >= 12) ? 1 : 0;
    int dispH = h > 12 ? h - 12 : (h == 0 ? 12 : h);
    sprintf(out, "%d:%02d %s", dispH, m, ampm ? "PM" : "AM");
}

void FormatDayHeader(const char* inDate, char* outStr) {
    int y, m, d;
    sscanf(inDate, "%d/%d/%d", &y, &m, &d);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    int dow = GetDayOfWeek(y, m, d) - 1;
    sprintf(outStr, "%s %02d/%02d", days[dow], m, d);
}

void FormatDateTitle(const char* inDate, char* outStr) {
    int y, m, d;
    sscanf(inDate, "%d/%d/%d", &y, &m, &d);
    const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    sprintf(outStr, "%s %d, %d", months[m-1], d, y);
}

/* --- Zoom Handler --- */
void SetZoom(float fNewZoom) {
    if (iViewMode >= 6) return;
    if (fNewZoom < 0.6f) fNewZoom = 0.6f;
    if (fNewZoom > 2.5f) fNewZoom = 2.5f;
    if (fNewZoom == fZoom) return;

    CloseInPlaceEdit(1);
    int visH = iClientH - iHeaderH - iSubHeaderH;
    float centerMin = (iScrollY + (visH / 2.0f)) / fZoom;
    fZoom = fNewZoom;

    UpdateScrollBars();
    iScrollY = (int)round((centerMin * fZoom) - (visH / 2.0f));
    UpdateScrollBars();
    InvalidateRect(hCanvas, NULL, TRUE);
}

/* --- In-Place Editing --- */
void OpenInPlaceEdit(int eIdx) {
    CloseInPlaceEdit(1);
    iEditingIndex = eIdx;
    RECT r; GetEventScreenRect(eIdx, &r);
    if (!IsRectEmpty(&r)) {
        int w = max(120, r.right - r.left);
        int h = max(50, r.bottom - r.top);
        SetWindowTextA(hInPlaceEdit, aEvents[eIdx].title);
        MoveWindow(hInPlaceEdit, r.left, r.top, w, h, TRUE);
        ShowWindow(hInPlaceEdit, SW_SHOW);
        SetFocus(hInPlaceEdit);
    }
}

void CloseInPlaceEdit(int bSave) {
    if (iEditingIndex != -1) {
        if (bSave) {
            char newText[1024];
            GetWindowTextA(hInPlaceEdit, newText, 1024);
            if (strcmp(newText, aEvents[iEditingIndex].title) != 0) {
                strcpy(aEvents[iEditingIndex].title, newText);
                MarkEventModified(iEditingIndex);
                SaveCSV();
            }
        }
        ShowWindow(hInPlaceEdit, SW_HIDE);
        MoveWindow(hInPlaceEdit, -500, -500, 10, 10, FALSE);
        iEditingIndex = -1;
        InvalidateRect(hCanvas, NULL, TRUE);
    }
}

/* --- Scroll Bar Management --- */
void UpdateScrollBars() {
    RECT rc; GetClientRect(hMainGUI, &rc);
    iClientW = rc.right - rc.left; iClientH = rc.bottom - rc.top;

    int effW = max(iClientW, iCanvasWidth);
    int visH = iClientH - iHeaderH - iSubHeaderH;
    if (visH < 0) visH = 0;

    SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };

    if (iViewMode == 6) {
        iScrollY = 0; si.nMax = 0; si.nPage = visH; si.nPos = 0;
        SetScrollInfo(hCanvas, SB_VERT, &si, TRUE);
    } else {
        if (iViewMode == 7) {
            int c = 0;
            for (int i = 0; i < numEvents; i++)
                if (strcmp(aEvents[i].date, sCurrentDate) >= 0 && aEvents[i].color != 2) c++;
            iCanvasHeight = (c * 115) + 40;
        } else {
            iCanvasHeight = (int)round(1440 * fZoom);
        }
        int maxY = max(0, iCanvasHeight - visH);
        iScrollY = max(0, min(iScrollY, maxY));
        si.nMax = iCanvasHeight; si.nPage = visH; si.nPos = iScrollY;
        SetScrollInfo(hCanvas, SB_VERT, &si, TRUE);
    }

    int maxX = max(0, effW - iClientW);
    iScrollX = max(0, min(iScrollX, maxX));
    si.nMax = effW; si.nPage = iClientW; si.nPos = iScrollX;
    SetScrollInfo(hCanvas, SB_HORZ, &si, TRUE);
}

void UpdateDateTitle() {
    char txt[128] = {0};
    const char* months[] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
    int y, m, d;
    sscanf(sCurrentDate, "%d/%d/%d", &y, &m, &d);

    if (iViewMode == 1) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s", buf); }
    else if (iViewMode == 2) {
        char end[16]; strcpy(end, sCurrentDate); DateAdd(end, 'd', 3);
        char b1[64], b2[64]; FormatDateTitle(sCurrentDate, b1); FormatDateTitle(end, b2);
        sprintf(txt, "%s - %s", b1, b2);
    }
    else if (iViewMode == 3) {
        char end[16]; strcpy(end, sCurrentDate); DateAdd(end, 'd', 6);
        char b1[64], b2[64]; FormatDateTitle(sCurrentDate, b1); FormatDateTitle(end, b2);
        sprintf(txt, "%s - %s", b1, b2);
    }
    else if (iViewMode == 4) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s (4 Person Team)", buf); }
    else if (iViewMode == 5) { char buf[64]; FormatDateTitle(sCurrentDate, buf); sprintf(txt, "%s (7 Person Team)", buf); }
    else if (iViewMode == 6) sprintf(txt, "%s %d", months[m-1], y);
    else if (iViewMode == 7) strcpy(txt, "Upcoming Schedule");

    SetWindowTextA(hLblDateTitle, txt);
}

/* --- Entry Point --- */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    InitCommonControls();

    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* pExt = strrchr(exePath, '.');
    if (pExt) *pExt = '\0';
    sprintf(sCSVFile, "%s.csv", exePath);
    sprintf(sINIFile, "%s.ini", exePath);
    sprintf(sLogFile, "%s.log", exePath);

    if (GetFileAttributesA(sINIFile) == INVALID_FILE_ATTRIBUTES) {
        FILE* ini = fopen(sINIFile, "w");
        if (ini) {
            fprintf(ini, "[Window]\nNode=1\n\n[Network]\nPort=9876\nSyncIntervalMs=180000\nLogging=0\nDeleteThreshold=100\n\n[Servers]\n");
            for (int i = 1; i <= 7; i++) fprintf(ini, "Server%d=0\n", i);
            fclose(ini);
        }
    }

    LoadINI();
    GetCalcDate(time(NULL), sCurrentDate);
    LoadCSV();

    if (iNetLogging) hLogFile = fopen(sLogFile, "a");
    InitializeNetwork();

    WNDCLASS wc = {0};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "CSVCalendarMain";
    RegisterClass(&wc);

    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CanvasWndProc;
    wc.lpszClassName = "CSVCalendarCanvas";
    RegisterClass(&wc);

    hMainGUI = CreateWindow("CSVCalendarMain", "CSV Calendar", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, iClientW, iClientH, NULL, NULL, hInst, NULL);

    hUIFont = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    hTitleFont = CreateFont(22, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    hBtnZoomIn = CreateWindow("BUTTON", "+", WS_CHILD | WS_VISIBLE, 10, 12, 28, 26, hMainGUI, (HMENU)101, hInst, NULL);
    hBtnZoomOut = CreateWindow("BUTTON", "-", WS_CHILD | WS_VISIBLE, 42, 12, 28, 26, hMainGUI, (HMENU)102, hInst, NULL);
    hBtnPrev = CreateWindow("BUTTON", "< Prev", WS_CHILD | WS_VISIBLE, 80, 12, 60, 26, hMainGUI, (HMENU)103, hInst, NULL);
    hBtnNext = CreateWindow("BUTTON", "Next >", WS_CHILD | WS_VISIBLE, 145, 12, 60, 26, hMainGUI, (HMENU)104, hInst, NULL);
    hBtnPrevDay = CreateWindow("BUTTON", "< Day", WS_CHILD | WS_VISIBLE, 215, 12, 50, 26, hMainGUI, (HMENU)105, hInst, NULL);
    hBtnNextDay = CreateWindow("BUTTON", "Day >", WS_CHILD | WS_VISIBLE, 270, 12, 50, 26, hMainGUI, (HMENU)106, hInst, NULL);
    hBtnPrint = CreateWindow("BUTTON", "Print", WS_CHILD | WS_VISIBLE, 330, 12, 55, 26, hMainGUI, (HMENU)107, hInst, NULL);
    hBtnExport = CreateWindow("BUTTON", "Export", WS_CHILD | WS_VISIBLE, 390, 12, 55, 26, hMainGUI, (HMENU)108, hInst, NULL);

    hComboView = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 455, 13, 140, 200, hMainGUI, (HMENU)109, hInst, NULL);
    const char* views[] = {"1 Day View", "4 Day View", "Week View", "4 Person View", "7 Person View", "Month View", "Upcoming Schedule"};
    for (int i = 0; i < 7; i++) SendMessage(hComboView, CB_ADDSTRING, 0, (LPARAM)views[i]);
    SendMessage(hComboView, CB_SETCURSEL, 0, 0);

    hBtnDelete = CreateWindow("BUTTON", "Delete", WS_CHILD | WS_VISIBLE, 605, 12, 55, 26, hMainGUI, (HMENU)110, hInst, NULL);
    hLblDateTitle = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 670, 10, 365, 32, hMainGUI, NULL, hInst, NULL);

    EnumChildWindows(hMainGUI, SetFontEnumProc, (LPARAM)hUIFont);
    SendMessage(hLblDateTitle, WM_SETFONT, (WPARAM)hTitleFont, 0);

    hCanvas = CreateWindow("CSVCalendarCanvas", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           0, iHeaderH, iClientW, iClientH - iHeaderH, hMainGUI, NULL, hInst, NULL);

    hInPlaceEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                                -500, -500, 100, 100, hCanvas, NULL, hInst, NULL);
    SendMessage(hInPlaceEdit, WM_SETFONT, (WPARAM)hUIFont, 0);

    UpdateDateTitle();
    UpdateScrollBars();
    ShowWindow(hMainGUI, show);
    UpdateWindow(hMainGUI);

    if (hServerListen != INVALID_SOCKET) {
        u_long mode = 1;
        ioctlsocket(hServerListen, FIONBIO, &mode);
    }

    MSG msg;
    
    /* Fixed: Replaced blocking GetMessage loop with PeekMessage polling loop so background networking doesn't stall */
    while (1) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto ExitApp;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (hServerListen != INVALID_SOCKET) {
            SOCKET client = accept(hServerListen, NULL, NULL);
            if (client != INVALID_SOCKET) {
                LogMessage("Server: Incoming connection accepted.");
                HandleServerClient(client);
            }
        }

        if (GetTickCount() - iSyncTimer >= (DWORD)iNetSyncIntervalMs) {
            iSyncTimer = GetTickCount();
            CloseInPlaceEdit(1);
            if (RunClientSyncs()) {
                numEvents = 0;
                maxEvents = 0;
                free(aEvents); aEvents = NULL;
                LoadCSV();
                UpdateScrollBars();
                InvalidateRect(hCanvas, NULL, TRUE);
            }

            iSyncCycles++;
            if (iSyncCycles >= iNetDeleteThreshold) {
                if (ProcessDeleteThreshold()) {
                    numEvents = 0;
                    maxEvents = 0;
                    free(aEvents); aEvents = NULL;
                    LoadCSV();
                    UpdateScrollBars();
                    InvalidateRect(hCanvas, NULL, TRUE);
                }
                iSyncCycles = 0;
            }
        }
        
        Sleep(10); // Prevent 100% CPU usage
    }

ExitApp:
    SaveINI();
    if (hServerListen != INVALID_SOCKET) closesocket(hServerListen);
    if (hLogFile) fclose(hLogFile);
    WSACleanup();
    return 0;
}

/* --- Window Procedures --- */
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE: {
            iClientW = LOWORD(lParam);
            iClientH = HIWORD(lParam);
            if (hCanvas) {
                MoveWindow(hCanvas, 0, iHeaderH, iClientW, iClientH - iHeaderH, TRUE);
                UpdateScrollBars();
            }
            return 0;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) == CBN_SELCHANGE && (HWND)lParam == hComboView) {
                CloseInPlaceEdit(1);
                iSelectedForDelete = -1;
                iViewMode = SendMessage(hComboView, CB_GETCURSEL, 0, 0) + 1;
                UpdateDateTitle();
                UpdateScrollBars();
                InvalidateRect(hCanvas, NULL, TRUE);
                SetFocus(hMainGUI);
            }
            int id = LOWORD(wParam);
            if (id >= 101 && id <= 110) CloseInPlaceEdit(1);
            if (id == 101) SetZoom(fZoom + 0.2f);
            if (id == 102) SetZoom(fZoom - 0.2f);
            if (id == 103 || id == 104) {
                iSelectedForDelete = -1;
                int dir = (id == 103) ? -1 : 1;
                if (iViewMode == 1 || iViewMode == 4 || iViewMode == 5) DateAdd(sCurrentDate, 'd', 1 * dir);
                else if (iViewMode == 2) DateAdd(sCurrentDate, 'd', 4 * dir);
                else if (iViewMode == 3) DateAdd(sCurrentDate, 'd', 7 * dir);
                else if (iViewMode >= 6) DateAdd(sCurrentDate, 'M', 1 * dir);
                UpdateDateTitle(); UpdateScrollBars(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 105 || id == 106) {
                iSelectedForDelete = -1;
                DateAdd(sCurrentDate, 'd', (id == 105) ? -1 : 1);
                UpdateDateTitle(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 107) PrintSchedule();
            if (id == 108) ExportUpcomingSchedule();
            if (id == 110) {
                if (iSelectedForDelete != -1 && iSelectedForDelete < numEvents) {
                    aEvents[iSelectedForDelete].color = 2;
                    MarkEventModified(iSelectedForDelete);
                    if (iEditingIndex == iSelectedForDelete) CloseInPlaceEdit(0);
                    iSelectedForDelete = -1;
                    SaveCSV();
                    UpdateScrollBars();
                    InvalidateRect(hCanvas, NULL, TRUE);
                }
            }
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/* --- Get Event Screen Rect --- */
/* --- Get Event Screen Rect --- */
void GetEventScreenRect(int eIdx, RECT* r) {
    SetRectEmpty(r);
    if (eIdx < 0 || eIdx >= numEvents) return;
    if (aEvents[eIdx].color == 2) return;

    // SAFE FETCH: Initialize to safe defaults, then try to get true canvas size
    int cw = iClientW;
    int ch = iClientH;
    if (hCanvas != NULL) {
        RECT rcCanvas = {0};
        if (GetClientRect(hCanvas, &rcCanvas) && rcCanvas.right > 0) {
            cw = rcCanvas.right - rcCanvas.left;
            ch = rcCanvas.bottom - rcCanvas.top;
        }
    }

    if (iViewMode <= 5) {
        int colIdx = GetEventColumnIndex(eIdx);
        if (colIdx < 0) return;
        int effW = max(cw, iCanvasWidth);  
        int dColW = (effW - iTimeColWidth) / GetColCount();
        r->left = iTimeColWidth + (colIdx * dColW) - iScrollX + 4;
        r->top = (int)round(aEvents[eIdx].startMin * fZoom) - iScrollY + iSubHeaderH;
        r->right = r->left + dColW - 8;
        r->bottom = (int)round((aEvents[eIdx].startMin + aEvents[eIdx].duration) * fZoom) - iScrollY + iSubHeaderH;
    } else if (iViewMode == 6) {
        int y, m, d;
        sscanf(aEvents[eIdx].date, "%d/%d/%d", &y, &m, &d);
        int curY, curM, curD;
        sscanf(sCurrentDate, "%d/%d/%d", &curY, &curM, &curD);
        if (y != curY || m != curM) return;

        int startDay = GetDayOfWeek(y, m, 1);
        int cellIdx = d + startDay - 1;
        int row = (cellIdx - 1) / 7;
        int col = (cellIdx - 1) % 7;
        int colW = cw / 7;                 
        int rowH = (ch - iSubHeaderH) / 6; 

        int dayEvents[100]; int count = 0;
        for (int i = 0; i < numEvents && count < 100; i++) {
            if (aEvents[i].color != 2 && strcmp(aEvents[i].date, aEvents[eIdx].date) == 0) dayEvents[count++] = i;
        }
        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (aEvents[dayEvents[j]].startMin < aEvents[dayEvents[i]].startMin) {
                    int tmp = dayEvents[i]; dayEvents[i] = dayEvents[j]; dayEvents[j] = tmp;
                }
            }
        }
        int pillIdx = -1;
        for (int i = 0; i < count; i++) {
            if (dayEvents[i] == eIdx) { pillIdx = i; break; }
        }
        int maxVisible = (rowH - 22) / 15;
        if (count > maxVisible) maxVisible -= 1;
        if (pillIdx == -1 || pillIdx >= maxVisible) return;

        r->left = (col * colW) + 4;
        r->top = iSubHeaderH + (row * rowH) + 22 + (pillIdx * 15);
        r->right = (col * colW) + colW - 4;
        r->bottom = r->top + 13;
    } else if (iViewMode == 7) {
        int effW = max(cw, iCanvasWidth);  
        int upIndices[2048];
        int count = GetSortedUpcomingIndices(upIndices, 2048);
        for (int i = 0; i < count; i++) {
            if (upIndices[i] == eIdx) {
                r->left = 35 - iScrollX;
                r->top = 20 - iScrollY + (i * 115) + 16;
                r->right = effW - 35;
                r->bottom = r->top + 34;
                return;
            }
        }
    }
}

/* --- Get Date From Month XY --- */
int GetDateFromMonthXY(int x, int y, char* outDate) {
    if (y < iSubHeaderH) return 0;
    
    // SAFE FETCH
    int cw = iClientW;
    int ch = iClientH;
    if (hCanvas != NULL) {
        RECT rcCanvas = {0};
        if (GetClientRect(hCanvas, &rcCanvas) && rcCanvas.right > 0) {
            cw = rcCanvas.right - rcCanvas.left;
            ch = rcCanvas.bottom - rcCanvas.top;
        }
    }

    int colW = cw / 7;
    int rowH = (ch - iSubHeaderH) / 6;
    int col = x / colW;
    int row = (y - iSubHeaderH) / rowH;
    
    int yr, m, d;
    sscanf(sCurrentDate, "%d/%d/%d", &yr, &m, &d);
    int startDay = GetDayOfWeek(yr, m, 1);
    int cellIdx = (row * 7) + col + 1;
    int dayNum = cellIdx - startDay + 1;
    if (dayNum >= 1 && dayNum <= GetDaysInMonth(yr, m)) {
        sprintf(outDate, "%04d/%02d/%02d", yr, m, dayNum);
        return 1;
    }
    return 0;
}

/* --- Canvas Window Procedure --- */
LRESULT CALLBACK CanvasWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hDC = BeginPaint(hWnd, &ps);
            DrawCalendar(hDC);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEWHEEL: {
            if (iViewMode == 6) return 0;
            CloseInPlaceEdit(1);
            short delta = (short)HIWORD(wParam);
            iScrollY -= (delta > 0) ? 60 : -60;
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        case WM_VSCROLL:
        case WM_HSCROLL: {
            CloseInPlaceEdit(1);
            int req = LOWORD(wParam);
            SCROLLINFO si = { sizeof(SCROLLINFO), SIF_ALL };
            GetScrollInfo(hWnd, (msg == WM_VSCROLL) ? SB_VERT : SB_HORZ, &si);
            int* pScroll = (msg == WM_VSCROLL) ? &iScrollY : &iScrollX;
            int page = (msg == WM_VSCROLL) ? (iClientH - iHeaderH - iSubHeaderH) : iClientW;
            if (req == SB_LINEUP) *pScroll -= 30;
            if (req == SB_LINEDOWN) *pScroll += 30;
            if (req == SB_PAGEUP) *pScroll -= page;
            if (req == SB_PAGEDOWN) *pScroll += page;
            if (req == SB_THUMBTRACK) *pScroll = si.nTrackPos;
            UpdateScrollBars();
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            CloseInPlaceEdit(1);
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            if (my < iSubHeaderH && iViewMode != 7) return 0;

            for (int i = numEvents - 1; i >= 0; i--) {
                RECT r; GetEventScreenRect(i, &r);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    iSelectedForDelete = i;
                    iDragIndex = i; iOrigStart = aEvents[i].startMin; iOrigDuration = aEvents[i].duration; bCopyTriggered = 0;
                    if (iViewMode >= 6) iDragMode = 1;
                    else if (my - r.top <= 6) iDragMode = 2;
                    else if (r.bottom - my <= 6) iDragMode = 3;
                    else { iDragMode = 1; iDragOffsetY = my - r.top; }
                    break;
                }
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            int mx = (short)LOWORD(lParam) + iScrollX, my = (short)HIWORD(lParam) + iScrollY - iSubHeaderH;
            if (iDragMode > 0 && iDragIndex != -1 && iViewMode != 7) {
                if (!bCopyTriggered && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
                    AddEvent("", aEvents[iDragIndex].title, aEvents[iDragIndex].startMin, aEvents[iDragIndex].duration,
                             aEvents[iDragIndex].color, aEvents[iDragIndex].date, aEvents[iDragIndex].personIdx, 1, -1);
                    iDragIndex = numEvents - 1; bCopyTriggered = 1;
                }
                if (iViewMode == 6) {
                    char hoverDate[16];
                    if (GetDateFromMonthXY((short)LOWORD(lParam), (short)HIWORD(lParam), hoverDate)) {
                        strcpy(aEvents[iDragIndex].date, hoverDate);
                    }
                } else {
                    int curMin = (int)round(((double)my / fZoom) / 15.0) * 15;
if (iDragMode == 1) {
                        int nStart = (int)round((((double)(short)HIWORD(lParam) + iScrollY - iSubHeaderH - iDragOffsetY) / fZoom) / 15.0) * 15;
                        nStart = max(0, min(1440 - aEvents[iDragIndex].duration, nStart));
                        aEvents[iDragIndex].startMin = nStart;
                        if (mx > iTimeColWidth) {
                            
                            // SAFE FETCH
                            int cw = iClientW;
                            if (hCanvas != NULL) {
                                RECT rcCanvas = {0};
                                if (GetClientRect(hCanvas, &rcCanvas) && rcCanvas.right > 0) {
                                    cw = rcCanvas.right - rcCanvas.left;
                                }
                            }
                            
                            int col = (mx - iTimeColWidth) / ((max(cw, iCanvasWidth) - iTimeColWidth) / GetColCount());
                            if (col >= 0 && col < GetColCount()) {
                                if (IsPeopleView()) aEvents[iDragIndex].personIdx = col;
                                else { strcpy(aEvents[iDragIndex].date, sCurrentDate); DateAdd(aEvents[iDragIndex].date, 'd', col); }
                            }
                        }
                    } else if (iDragMode == 2) {                        curMin = max(0, curMin);
                        if (iOrigStart + iOrigDuration - curMin >= 15) {
                            aEvents[iDragIndex].startMin = curMin;
                            aEvents[iDragIndex].duration = iOrigStart + iOrigDuration - curMin;
                        }
                    } else if (iDragMode == 3) {
                        curMin = min(1440, curMin);
                        if (curMin - iOrigStart >= 15) aEvents[iDragIndex].duration = curMin - iOrigStart;
                    }
                }
                InvalidateRect(hWnd, NULL, TRUE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (iDragMode > 0) {
                if (iDragIndex != -1 && iDragIndex < numEvents) MarkEventModified(iDragIndex);
                SaveCSV();
            }
            iDragMode = 0; iDragIndex = -1;
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            if (my < iSubHeaderH && iViewMode != 7) return 0;
            for (int i = numEvents - 1; i >= 0; i--) {
                RECT r; GetEventScreenRect(i, &r);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                        aEvents[i].color = 2;
                        MarkEventModified(i);
                        iSelectedForDelete = -1;
                        SaveCSV();
                        UpdateScrollBars();
                        InvalidateRect(hWnd, NULL, TRUE);
                    } else {
                        iSelectedForDelete = i;
                        OpenInPlaceEdit(i);
                    }
                    return 0;
                }
            }
            COLORREF colors[5] = {RGB_HEX(0x039BE5), RGB_HEX(0x33B679), RGB_HEX(0x8E24AA), RGB_HEX(0xF4511E), RGB_HEX(0xE67C73)};
            COLORREF col = colors[numEvents % 5];

            if (iViewMode == 6) {
                char clickDate[16];
                if (GetDateFromMonthXY(mx, my, clickDate)) {
                    AddEvent("", "New Event", 540, 60, col, clickDate, 0, 1, -1);
                    SaveCSV();
                    OpenInPlaceEdit(numEvents - 1);
                }
            } else if (iViewMode == 7) {
                return 0;
} else if (mx + iScrollX > iTimeColWidth) {
                // SAFE FETCH
                int cw = iClientW;
                if (hCanvas != NULL) {
                    RECT rcCanvas = {0};
                    if (GetClientRect(hCanvas, &rcCanvas) && rcCanvas.right > 0) {
                        cw = rcCanvas.right - rcCanvas.left;
                    }
                }

                int effW = max(cw, iCanvasWidth);
                int dColW = (effW - iTimeColWidth) / GetColCount();
                int colIdx = (mx + iScrollX - iTimeColWidth) / dColW;
                if (colIdx >= GetColCount()) colIdx = GetColCount() - 1;

                int stMin = (int)round((((my + iScrollY - iSubHeaderH) / fZoom) / 30.0f)) * 30;
                if (stMin < 0) stMin = 0;
                if (stMin > 1440 - 60) stMin = 1440 - 60;

                char dt[16]; strcpy(dt, sCurrentDate);
                if (!IsPeopleView()) DateAdd(dt, 'd', colIdx);
                AddEvent("", "New Event", stMin, 60, col, dt, IsPeopleView() ? colIdx : 0, 1, -1);
                SaveCSV();
                OpenInPlaceEdit(numEvents - 1);
            }            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/* --- Drawing Engine --- */
void DrawCalendar(HDC hWinDC) {
    RECT rc; GetClientRect(hCanvas, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    HDC hDC = CreateCompatibleDC(hWinDC);
    HBITMAP hBmp = CreateCompatibleBitmap(hWinDC, w, h);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hDC, hBmp);

    RECT rAll = {0, 0, w, h};
    FillRect(hDC, &rAll, (HBRUSH)GetStockObject(WHITE_BRUSH));
    SetBkMode(hDC, TRANSPARENT);

    if (iViewMode == 6) DrawMonthView(hDC, w, h);
    else if (iViewMode == 7) DrawUpcomingView(hDC, w, h);
    else DrawTimelineView(hDC, w, h);

    BitBlt(hWinDC, 0, 0, w, h, hDC, 0, 0, SRCCOPY);
    SelectObject(hDC, hOldBmp);
    DeleteObject(hBmp); DeleteDC(hDC);
}

void DrawTimelineView(HDC hDC, int w, int h) {
    int effW = max(w, iCanvasWidth);
    int cols = GetColCount();
    int dColW = (effW - iTimeColWidth) / cols;

    HPEN hPenHr = CreatePen(PS_SOLID, 1, RGB_HEX(0xE0E0E0));
    HPEN hPenHf = CreatePen(PS_DOT, 1, RGB_HEX(0xF0F0F0));
    HFONT hFontTime = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hFontEv = CreateFont(13, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    for (int i = 0; i <= 24; i++) {
        int y = (int)round((i * 60) * fZoom) - iScrollY + iSubHeaderH;
        if (y >= iSubHeaderH - 10 && y <= h) {
            SelectObject(hDC, hPenHr);
            MoveToEx(hDC, iTimeColWidth - iScrollX, y, NULL); LineTo(hDC, effW - iScrollX, y);
            if (i > 0 && i < 24) {
                char tm[16]; sprintf(tm, "%d %s", i > 12 ? i - 12 : (i == 0 ? 12 : i), i >= 12 ? "PM" : "AM");
                if (i == 12) strcpy(tm, "12 PM");
                SelectObject(hDC, hFontTime); SetTextColor(hDC, RGB_HEX(0x70757A));
                RECT tr = {0, y - 10, iTimeColWidth - 8 - iScrollX, y + 10};
                DrawTextA(hDC, tm, -1, &tr, DT_RIGHT | DT_SINGLELINE);
            }
            if (i < 24) {
                int hfy = (int)round(((i * 60) + 30) * fZoom) - iScrollY + iSubHeaderH;
                if (hfy >= iSubHeaderH && hfy <= h) {
                    SelectObject(hDC, hPenHf);
                    MoveToEx(hDC, iTimeColWidth - iScrollX, hfy, NULL); LineTo(hDC, effW - iScrollX, hfy);
                }
            }
        }
    }
    SelectObject(hDC, hPenHr);
    for (int c = 0; c <= cols; c++) {
        int cx = iTimeColWidth + c * dColW - iScrollX;
        MoveToEx(hDC, cx, iSubHeaderH, NULL); LineTo(hDC, cx, h);
    }

    SelectObject(hDC, hFontEv);
    for (int i = 0; i < numEvents; i++) {
        if (aEvents[i].color == 2) continue;
        RECT r; GetEventScreenRect(i, &r);
        if (r.bottom > iSubHeaderH && r.top < h && r.right > r.left) {
            HBRUSH hb = CreateSolidBrush(aEvents[i].color);
            HPEN hp = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i].color, 20));
            SelectObject(hDC, hb); SelectObject(hDC, hp);
            RoundRect(hDC, r.left, r.top, r.right, r.bottom, 8, 8);
            if (i != iEditingIndex && (r.bottom - r.top) > 18) {
                SetTextColor(hDC, CONTRAST_COLOR(aEvents[i].color));
                RECT tr = {r.left + 8, r.top + 4, r.right - 4, r.bottom - 2};
                char t1[16], t2[16], dispText[1080];
                MinToTimeString(aEvents[i].startMin, t1);
                MinToTimeString(aEvents[i].startMin + aEvents[i].duration, t2);
                sprintf(dispText, "%s\r\n%s - %s", aEvents[i].title, t1, t2);
                DrawTextA(hDC, dispText, -1, &tr, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
            }
            if ((r.bottom - r.top) > 24) {
                HPEN hPenGrip = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i].color, 35));
                SelectObject(hDC, hPenGrip);
                int midX = r.left + ((r.right - r.left) / 2);
                MoveToEx(hDC, midX - 12, r.top + 3, NULL); LineTo(hDC, midX + 12, r.top + 3);
                MoveToEx(hDC, midX - 12, r.bottom - 3, NULL); LineTo(hDC, midX + 12, r.bottom - 3);
                DeleteObject(hPenGrip);
            }
            DeleteObject(hb); DeleteObject(hp);
        }
    }

    char todayStr[16]; GetCalcDate(time(NULL), todayStr);
    int redCol = -1;
    if (IsPeopleView() && strcmp(sCurrentDate, todayStr) == 0) redCol = -2;
    else if (!IsPeopleView()) {
        int diff = DateDiffDays(sCurrentDate, todayStr);
        if (diff >= 0 && diff < cols) redCol = diff;
    }
    if (redCol != -1) {
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        int curMin = (tm->tm_hour * 60) + tm->tm_min;
        int nowY = (int)round(curMin * fZoom) - iScrollY + iSubHeaderH;
        if (nowY >= iSubHeaderH && nowY <= h) {
            HPEN hPenRed = CreatePen(PS_SOLID, 2, RGB_HEX(0xEA4335));
            HBRUSH hBrushRed = CreateSolidBrush(RGB_HEX(0xEA4335));
            SelectObject(hDC, hPenRed); SelectObject(hDC, hBrushRed);
            int rLeft = (redCol == -2) ? (iTimeColWidth - iScrollX) : (iTimeColWidth + (redCol * dColW) - iScrollX);
            int rRight = (redCol == -2) ? (effW - iScrollX) : (rLeft + dColW);
            MoveToEx(hDC, rLeft, nowY, NULL); LineTo(hDC, rRight, nowY);
            Ellipse(hDC, rLeft - 5, nowY - 5, rLeft + 5, nowY + 5);
            DeleteObject(hPenRed); DeleteObject(hBrushRed);
        }
    }

    RECT rSub = {0, 0, w, iSubHeaderH};
    HBRUSH hSubBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
    FillRect(hDC, &rSub, hSubBg); DeleteObject(hSubBg);

    SelectObject(hDC, hFontEv); SetTextColor(hDC, RGB_HEX(0x3C4043));
    for (int c = 0; c < cols; c++) {
        RECT tr = {iTimeColWidth + c * dColW - iScrollX, 6, iTimeColWidth + (c + 1) * dColW - iScrollX, 25};
        char hdr[64] = {0};
        if (IsPeopleView()) strcpy(hdr, c < numPeople ? aPeople[c] : "Person");
        else {
            char dt[16]; strcpy(dt, sCurrentDate); DateAdd(dt, 'd', c);
            FormatDayHeader(dt, hdr);
        }
        DrawTextA(hDC, hdr, -1, &tr, DT_CENTER | DT_SINGLELINE);
    }

    DeleteObject(hPenHr); DeleteObject(hPenHf); DeleteObject(hFontTime); DeleteObject(hFontEv);
}

void DrawMonthView(HDC hDC, int w, int h) {
    int yr, m, d;
    sscanf(sCurrentDate, "%d/%d/%d", &yr, &m, &d);
    int daysInMonth = GetDaysInMonth(yr, m);
    int startDay = GetDayOfWeek(yr, m, 1);
    int colW = w / 7;
    int rowH = (h - iSubHeaderH) / 6;

    RECT rSub = {0, 0, w, iSubHeaderH};
    HBRUSH hSubBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
    FillRect(hDC, &rSub, hSubBg); DeleteObject(hSubBg);

    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    HFONT hBold = CreateFont(13, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hDayFont = CreateFont(12, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hEvFont = CreateFont(11, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    SelectObject(hDC, hBold); SetTextColor(hDC, RGB_HEX(0x3C4043));
    for (int i = 0; i < 7; i++) {
        RECT rc = {i * colW, 6, (i + 1) * colW, iSubHeaderH};
        DrawTextA(hDC, days[i], -1, &rc, DT_CENTER | DT_SINGLELINE);
    }

    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB_HEX(0xDADCE0));
    HBRUSH hBrushToday = CreateSolidBrush(RGB(254, 247, 224));
    HBRUSH hBrushGrey = CreateSolidBrush(RGB_HEX(0xF1F3F4));
    SelectObject(hDC, hPenBorder);

    char todayStr[16]; GetCalcDate(time(NULL), todayStr);

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            int cellIdx = (row * 7) + col + 1;
            int dayNum = cellIdx - startDay + 1;
            int x1 = col * colW, y1 = iSubHeaderH + (row * rowH);
            int x2 = x1 + colW, y2 = y1 + rowH;
            RECT cellRect = {x1, y1, x2, y2};

            if (dayNum >= 1 && dayNum <= daysInMonth) {
                char cellDate[16]; sprintf(cellDate, "%04d/%02d/%02d", yr, m, dayNum);
                if (strcmp(cellDate, todayStr) == 0) FillRect(hDC, &cellRect, hBrushToday);

                SelectObject(hDC, hDayFont);
                SetTextColor(hDC, (strcmp(cellDate, todayStr) == 0) ? RGB_HEX(0x1A73E8) : RGB_HEX(0x3C4043));
                RECT numRect = {x1, y1 + 4, x2 - 6, y1 + 22};
                char sDay[8]; sprintf(sDay, "%d", dayNum);
                DrawTextA(hDC, sDay, -1, &numRect, DT_RIGHT | DT_SINGLELINE);

                int dayEvents[100]; int count = 0;
                for (int i = 0; i < numEvents && count < 100; i++) {
                    if (aEvents[i].color != 2 && strcmp(aEvents[i].date, cellDate) == 0) dayEvents[count++] = i;
                }
                for (int i = 0; i < count - 1; i++) {
                    for (int j = i + 1; j < count; j++) {
                        if (aEvents[dayEvents[j]].startMin < aEvents[dayEvents[i]].startMin) {
                            int tmp = dayEvents[i]; dayEvents[i] = dayEvents[j]; dayEvents[j] = tmp;
                        }
                    }
                }

                int maxVisible = (rowH - 22) / 15;
                if (count > maxVisible) maxVisible -= 1;

                SelectObject(hDC, hEvFont);
                for (int p = 0; p < count; p++) {
                    if (p < maxVisible) {
                        int eIdx = dayEvents[p];
                        int pTop = y1 + 22 + (p * 15);
                        int pBottom = pTop + 13;
                        int pLeft = x1 + 4, pRight = x2 - 4;

                        HBRUSH hBrushEv = CreateSolidBrush(aEvents[eIdx].color);
                        HPEN hPenEv = CreatePen(PS_SOLID, 1, DARKEN(aEvents[eIdx].color, 15));
                        SelectObject(hDC, hPenEv); SelectObject(hDC, hBrushEv);
                        RoundRect(hDC, pLeft, pTop, pRight, pBottom, 4, 4);

                        if (eIdx != iEditingIndex) {
                            SetTextColor(hDC, CONTRAST_COLOR(aEvents[eIdx].color));
                            RECT evRect = {pLeft + 5, pTop, pRight - 5, pBottom};
                            DrawTextA(hDC, aEvents[eIdx].title, -1, &evRect, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
                        }
                        DeleteObject(hPenEv); DeleteObject(hBrushEv);
                    } else if (p == maxVisible) {
                        SetTextColor(hDC, RGB_HEX(0x5F6368));
                        RECT moreRect = {x1 + 6, y1 + 22 + (p * 15), x2 - 6, y2};
                        char sMore[32]; sprintf(sMore, "+%d more", count - maxVisible);
                        DrawTextA(hDC, sMore, -1, &moreRect, DT_LEFT | DT_SINGLELINE);
                        break;
                    }
                }
            } else {
                FillRect(hDC, &cellRect, hBrushGrey);
            }
            SelectObject(hDC, hPenBorder);
            MoveToEx(hDC, x1, y1, NULL); LineTo(hDC, x2, y1);
            MoveToEx(hDC, x1, y1, NULL); LineTo(hDC, x1, y2);
        }
    }
    DeleteObject(hBold); DeleteObject(hDayFont); DeleteObject(hEvFont);
    DeleteObject(hPenBorder); DeleteObject(hBrushToday); DeleteObject(hBrushGrey);
}

void DrawUpcomingView(HDC hDC, int w, int h) {
    int effW = max(w, iCanvasWidth);
    HFONT hFontTitle = CreateFont(20, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hFontText = CreateFont(15, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    int upIndices[2048];
    int count = GetSortedUpcomingIndices(upIndices, 2048);

    int y = 20 - iScrollY;
    for (int i = 0; i < count; i++) {
        int e = upIndices[i];
        if (y + 100 > 0 && y < h) {
            int cardLeft = 20 - iScrollX;
            int cardRight = effW - 20 - iScrollX;
            int cardTop = y, cardBottom = y + 95;

            HBRUSH hBrushEv = CreateSolidBrush(aEvents[e].color);
            RECT colorRect = {cardLeft, cardTop, cardLeft + 15, cardBottom};
            FillRect(hDC, &colorRect, hBrushEv); DeleteObject(hBrushEv);

            HBRUSH hBrushBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
            RECT bgRect = {cardLeft + 15, cardTop, cardRight, cardBottom};
            FillRect(hDC, &bgRect, hBrushBg); DeleteObject(hBrushBg);

            HPEN hPenLine = CreatePen(PS_SOLID, 1, RGB_HEX(0xDADCE0));
            SelectObject(hDC, hPenLine);
            MoveToEx(hDC, cardLeft, cardTop, NULL); LineTo(hDC, cardRight, cardTop);
            LineTo(hDC, cardRight, cardBottom); LineTo(hDC, cardLeft, cardBottom);
            LineTo(hDC, cardLeft, cardTop); DeleteObject(hPenLine);

            if (e != iEditingIndex) {
                SelectObject(hDC, hFontTitle); SetTextColor(hDC, RGB_HEX(0x202124));
                RECT tTitle = {cardLeft + 35, y + 16, cardRight - 15, y + 50};
                DrawTextA(hDC, aEvents[e].title, -1, &tTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            }

            char sTime1[32], sTime2[32];
            MinToTimeString(aEvents[e].startMin, sTime1);
            MinToTimeString(aEvents[e].startMin + aEvents[e].duration, sTime2);
            char sPerson[64] = "";
            if (aEvents[e].personIdx < numPeople) strcpy(sPerson, aPeople[aEvents[e].personIdx]);

            char desc[256];
            sprintf(desc, "%s       %s - %s       %s", aEvents[e].date, sTime1, sTime2, sPerson);
            SelectObject(hDC, hFontText); SetTextColor(hDC, RGB_HEX(0x5F6368));
            RECT tDesc = {cardLeft + 35, y + 56, cardRight - 15, y + 86};
            DrawTextA(hDC, desc, -1, &tDesc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        y += 115;
    }
    DeleteObject(hFontTitle); DeleteObject(hFontText);
}

/* --- Export --- */
void ExportUpcomingSchedule() {
    OPENFILENAME ofn = {0}; char szFile[MAX_PATH] = "Upcoming_Schedule.txt";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hMainGUI;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileName(&ofn)) {
        FILE* fp = fopen(szFile, "w");
        if (fp) {
            fprintf(fp, "========================================\n           UPCOMING SCHEDULE            \n========================================\n\n");
            int upIndices[2048];
            int count = GetSortedUpcomingIndices(upIndices, 2048);
            for (int i = 0; i < count; i++) {
                int e = upIndices[i];
                char sTime1[32], sTime2[32];
                MinToTimeString(aEvents[e].startMin, sTime1);
                MinToTimeString(aEvents[e].startMin + aEvents[e].duration, sTime2);
                char sPerson[64] = "";
                if (aEvents[e].personIdx < numPeople) strcpy(sPerson, aPeople[aEvents[e].personIdx]);
                fprintf(fp, "%s\n%s       %s - %s       %s\n----------------------------------------\n",
                        aEvents[e].title, aEvents[e].date, sTime1, sTime2, sPerson);
            }
            fclose(fp);
            MessageBox(hMainGUI, "Export Successful", "Success", MB_OK | MB_ICONINFORMATION);
        }
    }
}

/* --- Printing --- */
void PrintSchedule() {
    PRINTDLG pd = {0};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hMainGUI;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;

    if (PrintDlg(&pd) && pd.hDC) {
        DOCINFO di = { sizeof(DOCINFO), "Calendar Schedule Vector Print" };
        StartDoc(pd.hDC, &di);
        StartPage(pd.hDC);
        SetBkMode(pd.hDC, TRANSPARENT);

        int dpiX = GetDeviceCaps(pd.hDC, LOGPIXELSX);
        int dpiY = GetDeviceCaps(pd.hDC, LOGPIXELSY);
        int w = GetDeviceCaps(pd.hDC, HORZRES);
        int h = GetDeviceCaps(pd.hDC, VERTRES);

        RECT rPage = {dpiX / 4, dpiY / 4, w - (dpiX / 4), h - (dpiY / 4)};
        if (iViewMode <= 5) PrintTimelineVector(pd.hDC, rPage, dpiX, dpiY);
        else if (iViewMode == 6) PrintMonthVector(pd.hDC, rPage, dpiX, dpiY);
        else if (iViewMode == 7) PrintUpcomingVector(pd.hDC, rPage, dpiX, dpiY);

        EndPage(pd.hDC);
        EndDoc(pd.hDC);
        DeleteDC(pd.hDC);
    }
}

void PrintTimelineVector(HDC hDC, RECT rPage, int dpiX, int dpiY) {
    SetBkMode(hDC, TRANSPARENT);
    int titleH = (int)(dpiY * 0.45);
    int subH = (int)(dpiY * 0.35);
    int gridTop = rPage.top + titleH + subH;
    int gridH = rPage.bottom - gridTop;

    int cols = GetColCount();
    int timeColW = (int)(dpiX * 0.75);
    int dayColW = (rPage.right - rPage.left - timeColW) / cols;

    HFONT hfTitle = CreateFont(-MulDiv(18, dpiY, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfHead = CreateFont(-MulDiv(11, dpiY, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfHour = CreateFont(-MulDiv(9, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfEv = CreateFont(-MulDiv(9, dpiY, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    SelectObject(hDC, hfTitle);
    SetTextColor(hDC, RGB_HEX(0x202124));
    char title[128]; GetWindowTextA(hLblDateTitle, title, 128);
    RECT rt = {rPage.left, rPage.top, rPage.right, rPage.top + titleH};
    DrawTextA(hDC, title, -1, &rt, DT_LEFT | DT_TOP | DT_SINGLELINE);

    RECT rSubBg = {rPage.left + timeColW, rPage.top + titleH, rPage.right, gridTop};
    HBRUSH hSubBrush = CreateSolidBrush(RGB_HEX(0xF8F9FA));
    FillRect(hDC, &rSubBg, hSubBrush); DeleteObject(hSubBrush);

    HPEN hPenHr = CreatePen(PS_SOLID, 1, RGB_HEX(0xCCCCCC));
    HPEN hPenHf = CreatePen(PS_DOT, 1, RGB_HEX(0xE8E8E8));

    for (int i = 0; i <= 24; i++) {
        int y = gridTop + (int)((i * 60) * ((float)gridH / 1440.0f));
        SelectObject(hDC, hPenHr);
        MoveToEx(hDC, rPage.left + timeColW, y, NULL); LineTo(hDC, rPage.right, y);
        if (i > 0 && i < 24) {
            char tm[16]; sprintf(tm, "%d %s", i > 12 ? i - 12 : (i == 0 ? 12 : i), i >= 12 ? "PM" : "AM");
            if (i == 12) strcpy(tm, "12 PM");
            SelectObject(hDC, hfHour);
            SetTextColor(hDC, RGB_HEX(0x70757A));
            int fontH = MulDiv(12, dpiY, 72);
            RECT rtm = {rPage.left, y - fontH, rPage.left + timeColW - (int)(dpiX * 0.08f), y + fontH};
            DrawTextA(hDC, tm, -1, &rtm, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
        if (i < 24) {
            int hfy = gridTop + (int)((i * 60 + 30) * ((float)gridH / 1440.0f));
            SelectObject(hDC, hPenHf);
            MoveToEx(hDC, rPage.left + timeColW, hfy, NULL); LineTo(hDC, rPage.right, hfy);
        }
    }

    SelectObject(hDC, hPenHr);
    for (int c = 0; c <= cols; c++) {
        int x = rPage.left + timeColW + (c * dayColW);
        MoveToEx(hDC, x, rPage.top + titleH, NULL); LineTo(hDC, x, rPage.bottom);
        if (c < cols) {
            SelectObject(hDC, hfHead);
            SetTextColor(hDC, RGB_HEX(0x3C4043));
            RECT rh = {x, rPage.top + titleH, x + dayColW, gridTop};
            char hdr[64] = {0};
            if (IsPeopleView()) strcpy(hdr, c < numPeople ? aPeople[c] : "Person");
            else { strcpy(hdr, sCurrentDate); DateAdd(hdr, 'd', c); }
            DrawTextA(hDC, hdr, -1, &rh, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    SelectObject(hDC, hfEv);
    for (int i = 0; i < numEvents; i++) {
        if (aEvents[i].color == 2) continue;
        int colIdx = GetEventColumnIndex(i);
        if (colIdx >= 0 && colIdx < cols) {
            int top = gridTop + (int)(aEvents[i].startMin * ((float)gridH / 1440.0f));
            int bot = gridTop + (int)((aEvents[i].startMin + aEvents[i].duration) * ((float)gridH / 1440.0f));
            int left = rPage.left + timeColW + (colIdx * dayColW) + (int)(dpiX * 0.03f);
            int right = left + dayColW - (int)(dpiX * 0.06f);
            
            HBRUSH hb = CreateSolidBrush(aEvents[i].color);
            HPEN hp = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i].color, 20));
            SelectObject(hDC, hb); SelectObject(hDC, hp);
            
            RoundRect(hDC, left, top, right, bot, MulDiv(8, dpiX, 96), MulDiv(8, dpiY, 96));
            
            SetTextColor(hDC, CONTRAST_COLOR(aEvents[i].color));
            RECT rev = {left + (int)(dpiX * 0.06f), top + (int)(dpiY * 0.03f), right - (int)(dpiX * 0.06f), bot - (int)(dpiY * 0.02f)};
            
            char t1[16], t2[16], dispText[1080];
            MinToTimeString(aEvents[i].startMin, t1);
            MinToTimeString(aEvents[i].startMin + aEvents[i].duration, t2);
            sprintf(dispText, "%s\r\n%s - %s", aEvents[i].title, t1, t2);
            
            if ((bot - top) > MulDiv(18, dpiY, 72)) {
                DrawTextA(hDC, dispText, -1, &rev, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
            }
            DeleteObject(hb); DeleteObject(hp);
        }
    }
    DeleteObject(hfTitle); DeleteObject(hfHead); DeleteObject(hfHour); DeleteObject(hfEv); 
    DeleteObject(hPenHr); DeleteObject(hPenHf);
}

void PrintMonthVector(HDC hDC, RECT rPage, int dpiX, int dpiY) {
    SetBkMode(hDC, TRANSPARENT);
    int titleH = (int)(dpiY * 0.45);
    int subH = (int)(dpiY * 0.35);
    int gridTop = rPage.top + titleH + subH;
    int gridH = rPage.bottom - gridTop;
    int colW = (rPage.right - rPage.left) / 7;
    int rowH = gridH / 6;

    int yr, m, d;
    sscanf(sCurrentDate, "%d/%d/%d", &yr, &m, &d);
    int daysInMonth = GetDaysInMonth(yr, m);
    int startDay = GetDayOfWeek(yr, m, 1);

    HFONT hfTitle = CreateFont(-MulDiv(18, dpiY, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfHead = CreateFont(-MulDiv(11, dpiY, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfDay = CreateFont(-MulDiv(10, dpiY, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfEv = CreateFont(-MulDiv(8, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");

    SelectObject(hDC, hfTitle);
    SetTextColor(hDC, RGB_HEX(0x202124));
    char title[128]; GetWindowTextA(hLblDateTitle, title, 128);
    RECT rt = {rPage.left, rPage.top, rPage.right, rPage.top + titleH};
    DrawTextA(hDC, title, -1, &rt, DT_LEFT | DT_TOP | DT_SINGLELINE);

    RECT rSub = {rPage.left, rPage.top + titleH, rPage.left + 7 * colW, gridTop};
    HBRUSH hSubBg = CreateSolidBrush(RGB_HEX(0xF8F9FA));
    FillRect(hDC, &rSub, hSubBg); DeleteObject(hSubBg);

    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    SelectObject(hDC, hfHead);
    SetTextColor(hDC, RGB_HEX(0x3C4043));
    HPEN hPenBorder = CreatePen(PS_SOLID, 1, RGB_HEX(0xCCCCCC));
    SelectObject(hDC, hPenBorder);
    
    MoveToEx(hDC, rPage.left, rPage.top + titleH, NULL); LineTo(hDC, rPage.left + 7 * colW, rPage.top + titleH);
    MoveToEx(hDC, rPage.left, gridTop, NULL); LineTo(hDC, rPage.left + 7 * colW, gridTop);

    for (int i = 0; i < 7; i++) {
        int x1 = rPage.left + i * colW;
        int x2 = x1 + colW;
        RECT rc = {x1, rPage.top + titleH, x2, gridTop};
        DrawTextA(hDC, days[i], -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        MoveToEx(hDC, x1, rPage.top + titleH, NULL); LineTo(hDC, x1, gridTop);
    }
    MoveToEx(hDC, rPage.left + 7 * colW, rPage.top + titleH, NULL); LineTo(hDC, rPage.left + 7 * colW, gridTop);

    HBRUSH hBrushToday = CreateSolidBrush(RGB(254, 247, 224));
    HBRUSH hBrushGrey = CreateSolidBrush(RGB_HEX(0xF1F3F4));
    char todayStr[16]; GetCalcDate(time(NULL), todayStr);

    int pillH = (int)(dpiY * 0.18f);
    int pillGap = (int)(dpiY * 0.03f);
    int dayTopOffset = (int)(dpiY * 0.22f);

    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 7; col++) {
            int cellIdx = (row * 7) + col + 1;
            int dayNum = cellIdx - startDay + 1;
            int x1 = rPage.left + col * colW;
            int y1 = gridTop + row * rowH;
            int x2 = x1 + colW;
            int y2 = y1 + rowH;
            RECT cellRect = {x1, y1, x2, y2};

            if (dayNum >= 1 && dayNum <= daysInMonth) {
                char cellDate[16]; sprintf(cellDate, "%04d/%02d/%02d", yr, m, dayNum);
                if (strcmp(cellDate, todayStr) == 0) FillRect(hDC, &cellRect, hBrushToday);

                SelectObject(hDC, hfDay);
                SetTextColor(hDC, (strcmp(cellDate, todayStr) == 0) ? RGB_HEX(0x1A73E8) : RGB_HEX(0x3C4043));
                RECT numRect = {x1, y1 + (int)(dpiY * 0.03f), x2 - (int)(dpiX * 0.05f), y1 + dayTopOffset};
                char sDay[8]; sprintf(sDay, "%d", dayNum);
                DrawTextA(hDC, sDay, -1, &numRect, DT_RIGHT | DT_SINGLELINE);

                int dayEvents[100]; int count = 0;
                for (int i = 0; i < numEvents && count < 100; i++) {
                    if (aEvents[i].color != 2 && strcmp(aEvents[i].date, cellDate) == 0) dayEvents[count++] = i;
                }
                for (int i = 0; i < count - 1; i++) {
                    for (int j = i + 1; j < count; j++) {
                        if (aEvents[dayEvents[j]].startMin < aEvents[dayEvents[i]].startMin) {
                            int tmp = dayEvents[i]; dayEvents[i] = dayEvents[j]; dayEvents[j] = tmp;
                        }
                    }
                }

                int maxVisible = (rowH - dayTopOffset) / (pillH + pillGap);
                if (count > maxVisible && maxVisible > 0) maxVisible -= 1;

                SelectObject(hDC, hfEv);
                for (int p = 0; p < count; p++) {
                    if (p < maxVisible) {
                        int eIdx = dayEvents[p];
                        int pTop = y1 + dayTopOffset + (p * (pillH + pillGap));
                        int pBottom = pTop + pillH;
                        int pLeft = x1 + (int)(dpiX * 0.03f);
                        int pRight = x2 - (int)(dpiX * 0.03f);

                        HBRUSH hBrushEv = CreateSolidBrush(aEvents[eIdx].color);
                        HPEN hPenEv = CreatePen(PS_SOLID, 1, DARKEN(aEvents[eIdx].color, 15));
                        SelectObject(hDC, hPenEv); SelectObject(hDC, hBrushEv);
                        
                        RoundRect(hDC, pLeft, pTop, pRight, pBottom, MulDiv(4, dpiX, 96), MulDiv(4, dpiY, 96));

                        SetTextColor(hDC, CONTRAST_COLOR(aEvents[eIdx].color));
                        RECT evRect = {pLeft + (int)(dpiX * 0.04f), pTop, pRight - (int)(dpiX * 0.04f), pBottom};
                        DrawTextA(hDC, aEvents[eIdx].title, -1, &evRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

                        DeleteObject(hPenEv); DeleteObject(hBrushEv);
                    } else if (p == maxVisible) {
                        SetTextColor(hDC, RGB_HEX(0x5F6368));
                        RECT moreRect = {x1 + (int)(dpiX * 0.04f), y1 + dayTopOffset + (p * (pillH + pillGap)), x2 - (int)(dpiX * 0.04f), y2};
                        char sMore[32]; sprintf(sMore, "+%d more", count - maxVisible);
                        DrawTextA(hDC, sMore, -1, &moreRect, DT_LEFT | DT_SINGLELINE);
                        break;
                    }
                }
            } else {
                FillRect(hDC, &cellRect, hBrushGrey);
            }

            SelectObject(hDC, hPenBorder);
            MoveToEx(hDC, x1, y1, NULL); LineTo(hDC, x2, y1);
            MoveToEx(hDC, x1, y1, NULL); LineTo(hDC, x1, y2);
        }
    }
    MoveToEx(hDC, rPage.left, gridTop + 6 * rowH, NULL); LineTo(hDC, rPage.left + 7 * colW, gridTop + 6 * rowH);
    MoveToEx(hDC, rPage.left + 7 * colW, gridTop, NULL); LineTo(hDC, rPage.left + 7 * colW, gridTop + 6 * rowH);

    DeleteObject(hfTitle); DeleteObject(hfHead); DeleteObject(hfDay); DeleteObject(hfEv);
    DeleteObject(hPenBorder); DeleteObject(hBrushToday); DeleteObject(hBrushGrey);
}

void PrintUpcomingVector(HDC hDC, RECT rPage, int dpiX, int dpiY) {
    SetBkMode(hDC, TRANSPARENT);
    HFONT hfTitle = CreateFont(-MulDiv(18, dpiY, 72), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    HFONT hfEv = CreateFont(-MulDiv(11, dpiY, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "Segoe UI");
    
    SelectObject(hDC, hfTitle);
    SetTextColor(hDC, RGB_HEX(0x202124));
    char title[128] = "Upcoming Schedule Print";
    RECT rt = {rPage.left, rPage.top, rPage.right, rPage.top + (int)(dpiY * 0.5)};
    DrawTextA(hDC, title, -1, &rt, DT_LEFT | DT_TOP | DT_SINGLELINE);
    
    int upIndices[2048];
    int count = GetSortedUpcomingIndices(upIndices, 2048);
    
    int y = rPage.top + (int)(dpiY * 0.6);
    SelectObject(hDC, hfEv);
    SetTextColor(hDC, RGB_HEX(0x3C4043));
    
    for (int i = 0; i < count; i++) {
        if (y < rPage.bottom) {
            int e = upIndices[i];
            char line[512]; char sTime1[32], sTime2[32];
            MinToTimeString(aEvents[e].startMin, sTime1); MinToTimeString(aEvents[e].startMin + aEvents[e].duration, sTime2);
            char sPerson[64] = "";
            if (aEvents[e].personIdx < numPeople) strcpy(sPerson, aPeople[aEvents[e].personIdx]);
            sprintf(line, "%s       %s (%s - %s)       %s", aEvents[e].date, aEvents[e].title, sTime1, sTime2, sPerson);
            RECT rLine = {rPage.left, y, rPage.right, y + (int)(dpiY * 0.3)};
            DrawTextA(hDC, line, -1, &rLine, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += (int)(dpiY * 0.35);
        }
    }
    DeleteObject(hfTitle); DeleteObject(hfEv);
}