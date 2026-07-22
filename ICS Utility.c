/*
 * ============================================================================
 * Calendar Database Manager & ICS Tool
 * Public Domain / Open Source
 * ============================================================================
 * 
 * COMPILE INSTRUCTIONS:
 * 
 * [GCC / MinGW]
 * gcc "ICS Utility.c" -Os -s -o "ICS Utility.exe" -mwindows -lcomdlg32
 *
 * [TinyC / TCC]
 * tcc "ICS Utility.c" -o "ICS Utility.exe" -luser32 -lkernel32 -lgdi32 -lcomdlg32 -lshell32
 * 
 * ============================================================================
 */

#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// --- GUI Control IDs ---
#define IDC_INPUT_CSV     101
#define IDC_BTN_BROWSE    102
#define IDC_BTN_EXPORT    103
#define IDC_BTN_IMPORT    104
#define IDC_CHK_SAMENAME  105
#define IDC_BTN_REBUILD   106
#define IDC_BTN_CLEANUP   107
#define IDC_LBL_STATUS    108

// --- Data Structures ---
typedef struct {
    char id[128];
    char title[256];
    int startMin;
    int duration;
    int color;
    char date[16];       // YYYY/MM/DD
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

// --- Globals ---
Event* g_events = NULL;
int g_eventCount = 0;
int g_eventCapacity = 0;
char g_szCSVFile[MAX_PATH] = "calendar02n.csv";

HWND hMain, hInputCSV, hLblStatus, hChkSameName;

// --- Helper Functions ---
void SetStatus(const char* msg) {
    SetWindowTextA(hLblStatus, msg);
}

void EnsureCapacity() {
    if (g_eventCount >= g_eventCapacity) {
        g_eventCapacity = g_eventCapacity == 0 ? 64 : g_eventCapacity * 2;
        g_events = (Event*)realloc(g_events, g_eventCapacity * sizeof(Event));
    }
}

// String replace utility (allocates new string, must be freed)
char* StrReplace(const char* orig, const char* rep, const char* with) {
    char *result;
    const char *ins;
    char *tmp;
    int len_rep, len_with, len_front, count;

    if (!orig && !rep) return NULL;
    len_rep = strlen(rep);
    if (len_rep == 0) return NULL;
    if (!with) with = "";
    len_with = strlen(with);

    ins = orig;
    for (count = 0; (tmp = strstr(ins, rep)); ++count) {
        ins = tmp + len_rep;
    }

    tmp = result = (char*)malloc(strlen(orig) + (len_with - len_rep) * count + 1);
    if (!result) return NULL;

    while (count--) {
        ins = strstr(orig, rep);
        len_front = ins - orig;
        tmp = strncpy(tmp, orig, len_front) + len_front;
        tmp = strcpy(tmp, with) + len_with;
        orig += len_front + len_rep;
    }
    strcpy(tmp, orig);
    return result;
}

// Date addition utility
void AddDays(const char* yyyymmdd, int days, char* outStr) {
    struct tm t = {0};
    int y, m, d;
    if (sscanf(yyyymmdd, "%4d/%2d/%2d", &y, &m, &d) == 3 || sscanf(yyyymmdd, "%4d%2d%2d", &y, &m, &d) == 3) {
        t.tm_year = y - 1900;
        t.tm_mon = m - 1;
        t.tm_mday = d + days;
        mktime(&t);
        sprintf(outStr, "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    } else {
        strcpy(outStr, yyyymmdd);
    }
}

// --- Core Database Functions ---
void LoadCSVToMemory(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    g_eventCount = 0;
    if (!f) {
        SetStatus("Status: CSV file not found. A new one will be created on save.");
        return;
    }

    char line[1024];
    int isFirst = 1;
    while (fgets(line, sizeof(line), f)) {
        if (isFirst) { isFirst = 0; continue; } // Skip header
        
        char* p = line;
        char* tokens[9];
        for (int i = 0; i < 9; i++) tokens[i] = "";
        
        int idx = 0;
        while (p && *p && idx < 9) {
            tokens[idx++] = p;
            char* sep = strstr(p, "\xC2\xA6"); // UTF-8 Broken Pipe
            if (!sep) sep = strstr(p, "\xA6"); // ANSI fallback
            
            if (sep) {
                *sep = '\0';
                p = sep + ((unsigned char)(*sep) == 0xC2 ? 2 : 1);
            } else {
                // Strip newline from last token
                char* nl = strpbrk(p, "\r\n");
                if (nl) *nl = '\0';
                break;
            }
        }

        if (idx >= 9) {
            EnsureCapacity();
            Event* ev = &g_events[g_eventCount++];
            strcpy(ev->id, tokens[0]);
            strcpy(ev->title, tokens[1]);
            ev->startMin = atoi(tokens[2]);
            ev->duration = atoi(tokens[3]);
            ev->color = atoi(tokens[4]);
            strcpy(ev->date, tokens[5]);
            ev->personIdx = atoi(tokens[6]);
            ev->version = atoi(tokens[7]);
            ev->lastModifiedBy = atoi(tokens[8]);
        }
    }
    fclose(f);

    char msg[256];
    sprintf(msg, "Status: Loaded %d event(s) into memory.", g_eventCount);
    SetStatus(msg);
}

int SaveMemoryToCSV(const char* filepath) {
    FILE* f = fopen(filepath, "w");
    if (!f) {
        MessageBoxA(hMain, "Could not open file for writing.", "Error", MB_ICONERROR);
        return 0;
    }

    fprintf(f, "ID,Title,StartMin,Duration,Color,Date,PersonIdx,Version,LastModifiedBy\n");
    for (int i = 0; i < g_eventCount; i++) {
        Event* ev = &g_events[i];
        fprintf(f, "%s\xC2\xA6%s\xC2\xA6%d\xC2\xA6%d\xC2\xA6%d\xC2\xA6%s\xC2\xA6%d\xC2\xA6%d\xC2\xA6%d\n",
            ev->id, ev->title, ev->startMin, ev->duration, ev->color, 
            ev->date, ev->personIdx, ev->version, ev->lastModifiedBy);
    }
    fclose(f);
    return 1;
}

// QSort compare function for Rebuild
int CompareEvents(const void* a, const void* b) {
    Event* ea = (Event*)a;
    Event* eb = (Event*)b;
    int dateCmp = strcmp(ea->date, eb->date);
    if (dateCmp != 0) return dateCmp;
    return ea->startMin - eb->startMin;
}

void RebuildCSV() {
    LoadCSVToMemory(g_szCSVFile);
    if (g_eventCount <= 1) {
        SetStatus("Status: Not enough events in memory to sort.");
        return;
    }
    qsort(g_events, g_eventCount, sizeof(Event), CompareEvents);
    if (SaveMemoryToCSV(g_szCSVFile)) {
        char msg[256];
        sprintf(msg, "Status: Successfully rebuilt and sorted %d event(s) by date.", g_eventCount);
        SetStatus(msg);
        MessageBoxA(hMain, "CSV database has been successfully rebuilt and sorted by date.", "Rebuild Complete", MB_ICONINFORMATION);
    }
}

void CleanupCSV() {
    LoadCSVToMemory(g_szCSVFile);
    int initialCount = g_eventCount;
    int keepCount = 0;

    for (int i = 0; i < g_eventCount; i++) {
        if (g_events[i].color != 2) {
            g_events[keepCount++] = g_events[i];
        }
    }
    g_eventCount = keepCount;
    int removed = initialCount - keepCount;

    if (SaveMemoryToCSV(g_szCSVFile)) {
        char msg[256];
        sprintf(msg, "Status: Cleanup complete. Erased %d event(s) marked with color 2.", removed);
        SetStatus(msg);
        MessageBoxA(hMain, msg, "Cleanup Complete", MB_ICONINFORMATION);
    }
}

// --- ICS Export / Import ---
void ExportToICS() {
    LoadCSVToMemory(g_szCSVFile);
    if (g_eventCount == 0) {
        MessageBoxA(hMain, "No events loaded in memory to export.", "Export ICS", MB_ICONWARNING);
        return;
    }

    char szSavePath[MAX_PATH] = {0};
    if (SendMessage(hChkSameName, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        strcpy(szSavePath, g_szCSVFile);
        char* dot = strrchr(szSavePath, '.');
        if (dot) *dot = '\0';
        strcat(szSavePath, ".ics");
    } else {
        OPENFILENAMEA ofn = {0};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hMain;
        ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0All Files\0*.*\0";
        ofn.lpstrFile = szSavePath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = "ics";
        if (!GetSaveFileNameA(&ofn)) return;
    }

    FILE* f = fopen(szSavePath, "w"); // Assuming UTF-8 default in system
    if (!f) {
        MessageBoxA(hMain, "Could not create ICS file at specified path.", "Error", MB_ICONERROR);
        return;
    }

    fprintf(f, "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:-//CSV Calendar Utility//EN\n");

    time_t now = time(NULL);
    struct tm* tm_now = gmtime(&now);
    char dtstamp[32];
    strftime(dtstamp, sizeof(dtstamp), "%Y%m%dT%H%M%SZ", tm_now);

    for (int i = 0; i < g_eventCount; i++) {
        Event* ev = &g_events[i];
        if (ev->color == 2) continue; // Skip deleted

        char* title1 = StrReplace(ev->title, "%0A", "\\n");
        char* title2 = StrReplace(title1, "%2C", ",");

        char sDateStr[16];
        strcpy(sDateStr, ev->date);
        char* slash;
        while ((slash = strchr(sDateStr, '/')) != NULL) *slash = '*'; // temporary char
        while ((slash = strchr(sDateStr, '*')) != NULL) memmove(slash, slash + 1, strlen(slash));

        int startHour = ev->startMin / 60;
        int startMin = ev->startMin % 60;
        
        int endTotalMin = ev->startMin + ev->duration;
        int endDayExtra = endTotalMin / 1440;
        int endMinOfDay = endTotalMin % 1440;
        int endHour = endMinOfDay / 60;
        int endMinute = endMinOfDay % 60;

        char sEndDateStr[32];
        strcpy(sEndDateStr, sDateStr);
        if (endDayExtra > 0) AddDays(ev->date, endDayExtra, sEndDateStr);

        fprintf(f, "BEGIN:VEVENT\n");
        fprintf(f, "UID:%s@csvcalendar\n", ev->id);
        fprintf(f, "DTSTAMP:%s\n", dtstamp);
        fprintf(f, "DTSTART:%sT%02d%02d00\n", sDateStr, startHour, startMin);
        fprintf(f, "DTEND:%sT%02d%02d00\n", sEndDateStr, endHour, endMinute);
        fprintf(f, "SUMMARY:%s\n", title2 ? title2 : ev->title);
        fprintf(f, "X-CSV-COLOR:%d\n", ev->color);
        fprintf(f, "END:VEVENT\n");

        if (title1) free(title1);
        if (title2) free(title2);
    }
    fprintf(f, "END:VCALENDAR\n");
    fclose(f);
    
    SetStatus("Status: Successfully exported events.");
    MessageBoxA(hMain, "Successfully exported events to ICS.", "Export Successful", MB_ICONINFORMATION);
}

void ExtractICSElement(const char* block, const char* key, char* out, int outMax) {
    out[0] = '\0';
    char search[128];
    sprintf(search, "\n%s:", key);
    
    // Quick search for key, handling case where it's the very first line too
    const char* start = strstr(block, search);
    if (!start) {
        sprintf(search, "%s:", key);
        if (strncmp(block, search, strlen(search)) == 0) start = block;
    } else {
        start++; // skip newline
    }

    if (start) {
        start += strlen(key) + 1;
        const char* end = strpbrk(start, "\r\n");
        int len = end ? (end - start) : strlen(start);
        if (len >= outMax) len = outMax - 1;
        strncpy(out, start, len);
        out[len] = '\0';
    }
}

void ImportFromICS() {
    char szOpenPath[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hMain;
    ofn.lpstrFilter = "iCalendar Files (*.ics)\0*.ics\0All Files\0*.*\0";
    ofn.lpstrFile = szOpenPath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&ofn)) return;

    FILE* f = fopen(szOpenPath, "rb");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* icsContent = (char*)malloc(fsize + 1);
    fread(icsContent, 1, fsize, f);
    fclose(f);
    icsContent[fsize] = '\0';

    int added = 0, updated = 0;
    char* block = strstr(icsContent, "BEGIN:VEVENT");
    
    while (block) {
        char* nextBlock = strstr(block + 1, "BEGIN:VEVENT");
        char* endBlock = strstr(block, "END:VEVENT");
        
        if (endBlock && (!nextBlock || endBlock < nextBlock)) {
            // Terminate current block temporarily for extraction
            *endBlock = '\0';
            
            char summary[512], dtstart[64], dtend[64], color[16], uid[128];
            ExtractICSElement(block, "SUMMARY", summary, sizeof(summary));
            ExtractICSElement(block, "DTSTART", dtstart, sizeof(dtstart));
            ExtractICSElement(block, "DTEND", dtend, sizeof(dtend));
            ExtractICSElement(block, "X-CSV-COLOR", color, sizeof(color));
            ExtractICSElement(block, "UID", uid, sizeof(uid));
            
            // Restore string
            *endBlock = 'E';
            
            if (uid[0] == '\0') sprintf(uid, "%d%d", rand(), (int)time(NULL));
            char* atSign = strchr(uid, '@');
            if (atSign) *atSign = '\0';

            if (dtstart[0] != '\0' && summary[0] != '\0') {
                char dateFormatted[16];
                sprintf(dateFormatted, "%.4s/%.2s/%.2s", dtstart, dtstart+4, dtstart+6);
                
                int startMin = 0;
                char* tPtr = strchr(dtstart, 'T');
                if (tPtr) {
                    char hh[3] = {tPtr[1], tPtr[2], 0};
                    char mm[3] = {tPtr[3], tPtr[4], 0};
                    startMin = (atoi(hh) * 60) + atoi(mm);
                }

                int duration = 60;
                tPtr = strchr(dtend, 'T');
                if (tPtr) {
                    char hh[3] = {tPtr[1], tPtr[2], 0};
                    char mm[3] = {tPtr[3], tPtr[4], 0};
                    int endMin = (atoi(hh) * 60) + atoi(mm);
                    if (endMin > startMin) duration = endMin - startMin;
                }

                int colorVal = color[0] != '\0' ? atoi(color) : 0;
                char* fmtSummary = StrReplace(summary, ",", "%2C");
                
                int exists = 0;
                for (int j = 0; j < g_eventCount; j++) {
                    if (strcmp(g_events[j].id, uid) == 0) {
                        strcpy(g_events[j].title, fmtSummary ? fmtSummary : summary);
                        g_events[j].startMin = startMin;
                        g_events[j].duration = duration;
                        if (color[0] != '\0') g_events[j].color = colorVal;
                        strcpy(g_events[j].date, dateFormatted);
                        g_events[j].version++;
                        exists = 1;
                        updated++;
                        break;
                    }
                }

                if (!exists) {
                    EnsureCapacity();
                    Event* ev = &g_events[g_eventCount++];
                    strcpy(ev->id, uid);
                    strcpy(ev->title, fmtSummary ? fmtSummary : summary);
                    ev->startMin = startMin;
                    ev->duration = duration;
                    ev->color = colorVal;
                    strcpy(ev->date, dateFormatted);
                    ev->personIdx = 0;
                    ev->version = 1;
                    ev->lastModifiedBy = 1;
                    added++;
                }
                
                if (fmtSummary) free(fmtSummary);
            }
        }
        block = nextBlock;
    }
    
    free(icsContent);

    if (added + updated > 0) {
        SaveMemoryToCSV(g_szCSVFile);
        char msg[256];
        sprintf(msg, "Status: Imported ICS. Added %d new, Updated %d existing.", added, updated);
        SetStatus(msg);
        MessageBoxA(hMain, msg, "Import Successful", MB_ICONINFORMATION);
    } else {
        MessageBoxA(hMain, "No valid VEVENT blocks found.", "Import ICS", MB_ICONWARNING);
    }
}

// --- Windows Message Loop ---
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Setup Font
            HFONT hFont = CreateFontA(-12, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, 
                                      DEFAULT_PITCH | FF_SWISS, "Segoe UI");
                                      
            // Row 1
            HWND hLbl = CreateWindowA("STATIC", "Source CSV File:", WS_CHILD | WS_VISIBLE, 
                                      20, 20, 90, 20, hwnd, NULL, NULL, NULL);
            SendMessage(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            hInputCSV = CreateWindowA("EDIT", g_szCSVFile, WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 
                                      120, 18, 270, 24, hwnd, (HMENU)IDC_INPUT_CSV, NULL, NULL);
            SendMessage(hInputCSV, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            HWND hBtnBrw = CreateWindowA("BUTTON", "Browse...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                         400, 17, 95, 26, hwnd, (HMENU)IDC_BTN_BROWSE, NULL, NULL);
            SendMessage(hBtnBrw, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Row 2
            HWND hBtnExp = CreateWindowA("BUTTON", "Export to ICS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                         20, 70, 230, 36, hwnd, (HMENU)IDC_BTN_EXPORT, NULL, NULL);
            SendMessage(hBtnExp, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnImp = CreateWindowA("BUTTON", "Import from ICS", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                         265, 70, 230, 36, hwnd, (HMENU)IDC_BTN_IMPORT, NULL, NULL);
            SendMessage(hBtnImp, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Row 2.5
            hChkSameName = CreateWindowA("BUTTON", "Use same base filename for Export (.ics)", 
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 
                                         20, 115, 300, 20, hwnd, (HMENU)IDC_CHK_SAMENAME, NULL, NULL);
            SendMessage(hChkSameName, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendMessage(hChkSameName, BM_SETCHECK, BST_CHECKED, 0);

            // Row 3
            HWND hBtnReb = CreateWindowA("BUTTON", "Rebuild CSV (Sort by Date)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                         20, 145, 230, 36, hwnd, (HMENU)IDC_BTN_REBUILD, NULL, NULL);
            SendMessage(hBtnReb, WM_SETFONT, (WPARAM)hFont, TRUE);

            HWND hBtnCln = CreateWindowA("BUTTON", "Cleanup CSV (Remove Color 2)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                                         265, 145, 230, 36, hwnd, (HMENU)IDC_BTN_CLEANUP, NULL, NULL);
            SendMessage(hBtnCln, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Status
            hLblStatus = CreateWindowA("STATIC", "Ready. Select a CSV file to begin.", WS_CHILD | WS_VISIBLE, 
                                       20, 205, 475, 40, hwnd, (HMENU)IDC_LBL_STATUS, NULL, NULL);
            SendMessage(hLblStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
            break;
        }
        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            switch (wmId) {
                case IDC_BTN_BROWSE: {
                    OPENFILENAMEA ofn = {0};
                    char szFile[MAX_PATH] = {0};
                    strcpy(szFile, g_szCSVFile);
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hwnd;
                    ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = MAX_PATH;
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
                    if (GetOpenFileNameA(&ofn)) {
                        strcpy(g_szCSVFile, szFile);
                        SetWindowTextA(hInputCSV, g_szCSVFile);
                        LoadCSVToMemory(g_szCSVFile);
                    }
                    break;
                }
                case IDC_BTN_EXPORT: ExportToICS(); break;
                case IDC_BTN_IMPORT: ImportFromICS(); break;
                case IDC_BTN_REBUILD: RebuildCSV(); break;
                case IDC_BTN_CLEANUP: CleanupCSV(); break;
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const char* CLASS_NAME = "CalendarManagerClass";
    
    WNDCLASSA wc = {0};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    // Using a fixed window style roughly equating the autoit resizable dock behavior for brevity
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rect = {0, 0, 520, 260}; 
    AdjustWindowRect(&rect, style, FALSE);

    hMain = CreateWindowA(CLASS_NAME, "Calendar Database Utility", style,
        CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
        NULL, NULL, hInstance, NULL);

    if (hMain == NULL) return 0;

    ShowWindow(hMain, nCmdShow);
    
    // Initial Load
    LoadCSVToMemory(g_szCSVFile);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if (g_events) free(g_events);
    return 0;
}