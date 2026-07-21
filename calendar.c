/* ============================================================================
 * CSV Calendar - C Implementation
 * 
 * COMPILATION INSTRUCTIONS:
 * 
 * With GCC (MinGW-w64):
 *   gcc -Os -s -o calendar.exe calendar.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32 -mwindows
 * 
 * With TCC (Tiny C Compiler):
 *   tcc -o calendar.exe calendar.c -lgdi32 -lole32 -limm32 -lcomdlg32 -lcomctl32
 * 
 * REQUIREMENTS: Windows XP or later
 * DEPENDENCIES: Win32 API only (GDI32, USER32, COMDLG32, OLE32)
 * 
 * NOTES:
 * - Uses ANSI versions of API calls for maximum compatibility
 * - CSV file stored as <executable_name>.csv in same directory
 * - Memory leak fixes and proper resource cleanup added
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

/* --- Event Data Structure --- */
typedef struct {
    char title[1024];
    int startMin;
    int duration;
    COLORREF color;
    char date[16];
    int personIdx;
} Event;

Event* aEvents = NULL;
int numEvents = 0;
int maxEvents = 0;

char sCSVFile[MAX_PATH];

/* --- Interaction State Variables --- */
int iDragMode = 0; // 0=None, 1=Move/Copy, 2=ResizeTop, 3=ResizeBottom
int iDragIndex = -1;
int iDragOffsetY = 0;
int iOrigStart = 0, iOrigDuration = 0;
int bCopyTriggered = 0;
int iEditingIndex = -1;

/* --- UI Handles --- */
HWND hMainGUI, hCanvas, hInPlaceEdit;
HWND hBtnZoomIn, hBtnZoomOut, hBtnPrev, hBtnNext, hBtnPrevDay, hBtnNextDay;
HWND hBtnPrint, hBtnExport, hComboView, hLblDateTitle;
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
void DrawCalendar(HDC hDC);
void DrawTimelineView(HDC hDC, int w, int h);
void DrawMonthView(HDC hDC, int w, int h);
void DrawUpcomingView(HDC hDC, int w, int h);
void PrintSchedule();
void PrintTimelineVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintMonthVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void PrintUpcomingVector(HDC hDC, RECT rPage, int dpiX, int dpiY);
void ExportUpcomingSchedule();
void AddEvent(const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx);
int GetDateFromMonthXY(int x, int y, char* outDate);
void MinToTimeString(int min, char* out);
void FormatDayHeader(const char* inDate, char* outStr);
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK CanvasWndProc(HWND, UINT, WPARAM, LPARAM);

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
    int h = min / 60;
    int m = min % 60;
    int ampm = (h >= 12) ? 1 : 0;
    int dispH = h > 12 ? h - 12 : (h == 0 ? 12 : h);
    sprintf(out, "%02d:%02d %s", dispH, m, ampm ? "PM" : "AM");
}

void FormatDayHeader(const char* inDate, char* outStr) {
    int y, m, d;
    sscanf(inDate, "%d/%d/%d", &y, &m, &d);
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int dow = GetDayOfWeek(y, m, d) - 1;
    sprintf(outStr, "%s, %s %d", days[dow], months[m-1], d);
}

int IsPeopleView() { return (iViewMode == 4 || iViewMode == 5); }
int GetColCount() {
    if (iViewMode == 1) return 1;
    if (iViewMode == 2 || iViewMode == 4) return 4;
    if (iViewMode == 3 || iViewMode == 5) return 7;
    return 1;
}

/* --- Core Management --- */
void AddEvent(const char* title, int start, int dur, COLORREF col, const char* dt, int pIdx) {
    if (numEvents >= maxEvents) {
        maxEvents = maxEvents == 0 ? 32 : maxEvents * 2;
        aEvents = (Event*)realloc(aEvents, maxEvents * sizeof(Event));
    }
    strncpy(aEvents[numEvents].title, title, 1023);
    aEvents[numEvents].startMin = start;
    aEvents[numEvents].duration = dur;
    aEvents[numEvents].color = col;
    strncpy(aEvents[numEvents].date, dt, 15);
    aEvents[numEvents].personIdx = pIdx;
    numEvents++;
}

void ReplaceAll(char* str, const char* search, const char* replace) {
    char buffer[1024];
    char* p;
    if (!(p = strstr(str, search))) return;
    strncpy(buffer, str, p - str);
    buffer[p - str] = '\0';
    sprintf(buffer + (p - str), "%s%s", replace, p + strlen(search));
    strcpy(str, buffer);
    ReplaceAll(str, search, replace);
}

void LoadCSV() {
    FILE* fp = fopen(sCSVFile, "r");
    if (!fp) return;
    char line[2048];
    fgets(line, sizeof(line), fp);
    while (fgets(line, sizeof(line), fp)) {
        char title[1024], dt[16];
        int start, dur, col, pIdx;
        char* token = strtok(line, ",");
        if (!token) continue;
        strcpy(title, token);
        start = atoi(strtok(NULL, ","));
        dur = atoi(strtok(NULL, ","));
        col = atoi(strtok(NULL, ","));
        strcpy(dt, strtok(NULL, ","));
        pIdx = atoi(strtok(NULL, ",\r\n"));
        
        ReplaceAll(title, "%2C", ",");
        ReplaceAll(title, "%0A", "\r\n");
        AddEvent(title, start, dur, col, dt, pIdx);
    }
    fclose(fp);
}

void SaveCSV() {
    FILE* fp = fopen(sCSVFile, "w");
    if (!fp) return;
    fprintf(fp, "Title,StartMin,Duration,Color,Date,PersonIdx\n");
    for (int i = 0; i < numEvents; i++) {
        char title[1024];
        strcpy(title, aEvents[i].title);
        ReplaceAll(title, ",", "%2C");
        ReplaceAll(title, "\r\n", "%0A");
        ReplaceAll(title, "\n", "%0A");
        fprintf(fp, "%s,%d,%d,%d,%s,%d\n", title, aEvents[i].startMin, aEvents[i].duration,
                aEvents[i].color, aEvents[i].date, aEvents[i].personIdx);
    }
    fclose(fp);
}

BOOL CALLBACK SetFontEnumProc(HWND hwnd, LPARAM lParam) {
    SendMessage(hwnd, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

/* --- Entry Point --- */
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    InitCommonControls();
    
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* pExt = strrchr(exePath, '.');
    if (pExt) *pExt = '\0';
    sprintf(sCSVFile, "%s.csv", exePath);

    GetCalcDate(time(NULL), sCurrentDate);
    LoadCSV();

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
    hBtnPrint = CreateWindow("BUTTON", "Print", WS_CHILD | WS_VISIBLE, 330, 12, 60, 26, hMainGUI, (HMENU)107, hInst, NULL);
    hBtnExport = CreateWindow("BUTTON", "Export", WS_CHILD | WS_VISIBLE, 395, 12, 60, 26, hMainGUI, (HMENU)108, hInst, NULL);

    hComboView = CreateWindow("COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 465, 13, 140, 200, hMainGUI, (HMENU)109, hInst, NULL);
    const char* views[] = {"1 Day View", "4 Day View", "Week View", "4 Person View", "7 Person View", "Month View", "Upcoming Schedule"};
    for (int i = 0; i < 7; i++) SendMessage(hComboView, CB_ADDSTRING, 0, (LPARAM)views[i]);
    SendMessage(hComboView, CB_SETCURSEL, 0, 0);

    hLblDateTitle = CreateWindow("STATIC", "", WS_CHILD | WS_VISIBLE, 615, 10, 420, 32, hMainGUI, NULL, hInst, NULL);
    
    EnumChildWindows(hMainGUI, SetFontEnumProc, (LPARAM)hUIFont);
    SendMessage(hLblDateTitle, WM_SETFONT, (WPARAM)hTitleFont, 0);

    hCanvas = CreateWindow("CSVCalendarCanvas", "", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL,
                           0, iHeaderH, iClientW, iClientH - iHeaderH, hMainGUI, NULL, hInst, NULL);

    hInPlaceEdit = CreateWindow("EDIT", "", WS_CHILD | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL,
                                -500, -500, 100, 100, hCanvas, NULL, hInst, NULL);
    SendMessage(hInPlaceEdit, WM_SETFONT, (WPARAM)hUIFont, 0);

    UpdateDateTitle();
    ShowWindow(hMainGUI, show);
    UpdateWindow(hMainGUI);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
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
                iViewMode = SendMessage(hComboView, CB_GETCURSEL, 0, 0) + 1;
                UpdateDateTitle();
                UpdateScrollBars();
                InvalidateRect(hCanvas, NULL, TRUE);
                SetFocus(hMainGUI);
            }
            int id = LOWORD(wParam);
            if (id >= 101 && id <= 108) CloseInPlaceEdit(1);
            if (id == 101) SetZoom(fZoom + 0.2f);
            if (id == 102) SetZoom(fZoom - 0.2f);
            if (id == 103 || id == 104) {
                int dir = (id == 103) ? -1 : 1;
                if (iViewMode == 1 || iViewMode == 4 || iViewMode == 5) DateAdd(sCurrentDate, 'd', 1 * dir);
                else if (iViewMode == 2) DateAdd(sCurrentDate, 'd', 4 * dir);
                else if (iViewMode == 3) DateAdd(sCurrentDate, 'd', 7 * dir);
                else if (iViewMode >= 6) DateAdd(sCurrentDate, 'M', 1 * dir);
                UpdateDateTitle(); UpdateScrollBars(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 105 || id == 106) {
                DateAdd(sCurrentDate, 'd', (id == 105) ? -1 : 1);
                UpdateDateTitle(); InvalidateRect(hCanvas, NULL, TRUE);
            }
            if (id == 107) PrintSchedule();
            if (id == 108) ExportUpcomingSchedule();
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int CompareEvents(const void* a, const void* b) {
    Event* ea = *(Event**)a; Event* eb = *(Event**)b;
    int cmp = strcmp(ea->date, eb->date);
    if (cmp == 0) return ea->startMin - eb->startMin;
    return cmp;
}

void GetEventScreenRect(int eIdx, RECT* r) {
    if (iViewMode <= 5) {
        int colIdx = -1;
        if (IsPeopleView()) {
            if (strcmp(aEvents[eIdx].date, sCurrentDate) == 0 && aEvents[eIdx].personIdx < GetColCount()) colIdx = aEvents[eIdx].personIdx;
        } else {
            int diff = DateDiffDays(sCurrentDate, aEvents[eIdx].date);
            if (diff >= 0 && diff < GetColCount()) colIdx = diff;
        }
        if (colIdx < 0) { SetRectEmpty(r); return; }
        int effW = max(iClientW, iCanvasWidth);
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
        if (y != curY || m != curM) { SetRectEmpty(r); return; }
        
        int startDay = GetDayOfWeek(y, m, 1);
        int cellIdx = d + startDay - 1;
        int row = (cellIdx - 1) / 7;
        int col = (cellIdx - 1) % 7;
        int colW = iClientW / 7;
        int rowH = (iClientH - iHeaderH - iSubHeaderH) / 6;
        
        int dayEvents[100]; int count = 0;
        for (int i = 0; i < numEvents && count < 100; i++) {
            if (strcmp(aEvents[i].date, aEvents[eIdx].date) == 0) dayEvents[count++] = i;
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
        if (pillIdx == -1 || pillIdx >= maxVisible) { SetRectEmpty(r); return; }
        
        r->left = (col * colW) + 4;
        r->top = iSubHeaderH + (row * rowH) + 22 + (pillIdx * 15);
        r->right = (col * colW) + colW - 4;
        r->bottom = r->top + 13;
    } else if (iViewMode == 7) {
        int effW = max(iClientW, iCanvasWidth);
        Event** up = (Event**)malloc(numEvents * sizeof(Event*)); int count = 0;
        for (int i = 0; i < numEvents; i++) {
            if (strcmp(aEvents[i].date, sCurrentDate) >= 0) up[count++] = &aEvents[i];
        }
        qsort(up, count, sizeof(Event*), CompareEvents);
        for (int i = 0; i < count; i++) {
            if (up[i] == &aEvents[eIdx]) {
                r->left = 35 - iScrollX;
                r->top = 20 - iScrollY + (i * 115) + 16;
                r->right = effW - 35;
                r->bottom = r->top + 34;
                free(up);
                return;
            }
        }
        free(up);
        SetRectEmpty(r);
    }
}

int GetDateFromMonthXY(int x, int y, char* outDate) {
    if (y < iSubHeaderH) return 0;
    int colW = iClientW / 7;
    int rowH = (iClientH - iHeaderH - iSubHeaderH) / 6;
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
                    AddEvent(aEvents[iDragIndex].title, aEvents[iDragIndex].startMin, aEvents[iDragIndex].duration,
                             aEvents[iDragIndex].color, aEvents[iDragIndex].date, aEvents[iDragIndex].personIdx);
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
                            int col = (mx - iTimeColWidth) / ((max(iClientW, iCanvasWidth) - iTimeColWidth) / GetColCount());
                            if (col >= 0 && col < GetColCount()) {
                                if (IsPeopleView()) aEvents[iDragIndex].personIdx = col;
                                else { strcpy(aEvents[iDragIndex].date, sCurrentDate); DateAdd(aEvents[iDragIndex].date, 'd', col); }
                            }
                        }
                    } else if (iDragMode == 2) {
                        curMin = max(0, curMin);
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
            if (iDragMode > 0) SaveCSV();
            iDragMode = 0; iDragIndex = -1;
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            int mx = (short)LOWORD(lParam), my = (short)HIWORD(lParam);
            if (my < iSubHeaderH && iViewMode != 7) return 0;
            for (int i = numEvents - 1; i >= 0; i--) {
                RECT r; GetEventScreenRect(i, &r);
                if (mx >= r.left && mx <= r.right && my >= r.top && my <= r.bottom) {
                    OpenInPlaceEdit(i);
                    return 0;
                }
            }
            COLORREF colors[5] = {RGB_HEX(0x039BE5), RGB_HEX(0x33B679), RGB_HEX(0x8E24AA), RGB_HEX(0xF4511E), RGB_HEX(0xE67C73)};
            COLORREF col = colors[numEvents % 5];
            
            if (iViewMode == 6) {
                char clickDate[16];
                if (GetDateFromMonthXY(mx, my, clickDate)) {
                    AddEvent("New Event", 540, 60, col, clickDate, 0);
                    SaveCSV();
                    OpenInPlaceEdit(numEvents - 1);
                }
            } else if (iViewMode == 7) {
                return 0;
            } else if (mx + iScrollX > iTimeColWidth) {
                int effW = max(iClientW, iCanvasWidth);
                int dColW = (effW - iTimeColWidth) / GetColCount();
                int colIdx = (mx + iScrollX - iTimeColWidth) / dColW;
                if (colIdx >= GetColCount()) colIdx = GetColCount() - 1;
                
                int stMin = (int)round((((my + iScrollY - iSubHeaderH) / fZoom) / 30.0f)) * 30;
                if (stMin < 0) stMin = 0;
                if (stMin > 1440 - 60) stMin = 1440 - 60;

                char dt[16]; strcpy(dt, sCurrentDate);
                if (!IsPeopleView()) DateAdd(dt, 'd', colIdx);
                AddEvent("New Event", stMin, 60, col, dt, IsPeopleView() ? colIdx : 0);
                SaveCSV();
                OpenInPlaceEdit(numEvents - 1);
            }
            return 0;
        }
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

void OpenInPlaceEdit(int eIdx) {
    CloseInPlaceEdit(1);
    iEditingIndex = eIdx;
    RECT r; GetEventScreenRect(eIdx, &r);
    int w = max(120, r.right - r.left);
    int h = max(50, r.bottom - r.top);
    SetWindowTextA(hInPlaceEdit, aEvents[eIdx].title);
    MoveWindow(hInPlaceEdit, r.left, r.top, w, h, TRUE);
    ShowWindow(hInPlaceEdit, SW_SHOW); SetFocus(hInPlaceEdit);
}

void CloseInPlaceEdit(int bSave) {
    if (iEditingIndex != -1) {
        if (bSave) {
            GetWindowTextA(hInPlaceEdit, aEvents[iEditingIndex].title, 1024);
            SaveCSV();
        }
        ShowWindow(hInPlaceEdit, SW_HIDE);
        MoveWindow(hInPlaceEdit, -500, -500, 10, 10, FALSE);
        iEditingIndex = -1;
        InvalidateRect(hCanvas, NULL, TRUE);
    }
}

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
            for(int i=0; i<numEvents; i++) if(strcmp(aEvents[i].date, sCurrentDate)>=0) c++;
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
    
    if (iViewMode == 1) sprintf(txt, "%s %d, %d", months[m-1], d, y);
    else if (iViewMode == 2) {
        char end[16]; strcpy(end, sCurrentDate); DateAdd(end, 'd', 3);
        int y2, m2, d2; sscanf(end, "%d/%d/%d", &y2, &m2, &d2);
        sprintf(txt, "%s %d, %d - %s %d, %d", months[m-1], d, y, months[m2-1], d2, y2);
    }
    else if (iViewMode == 3) {
        char end[16]; strcpy(end, sCurrentDate); DateAdd(end, 'd', 6);
        int y2, m2, d2; sscanf(end, "%d/%d/%d", &y2, &m2, &d2);
        sprintf(txt, "%s %d, %d - %s %d, %d", months[m-1], d, y, months[m2-1], d2, y2);
    }
    else if (iViewMode == 4) sprintf(txt, "%s %d, %d (4 Person Team)", months[m-1], d, y);
    else if (iViewMode == 5) sprintf(txt, "%s %d, %d (7 Person Team)", months[m-1], d, y);
    else if (iViewMode == 6) sprintf(txt, "%s %d", months[m-1], y);
    else if (iViewMode == 7) strcpy(txt, "Upcoming Schedule");
    
    SetWindowTextA(hLblDateTitle, txt);
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

    if (iViewMode == 6) {
        DrawMonthView(hDC, w, h);
    } else if (iViewMode == 7) {
        DrawUpcomingView(hDC, w, h);
    } else {
        DrawTimelineView(hDC, w, h);
    }
    
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
                    if (strcmp(aEvents[i].date, cellDate) == 0) dayEvents[count++] = i;
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
    
    Event** up = (Event**)malloc(numEvents * sizeof(Event*)); int count = 0;
    for (int i = 0; i < numEvents; i++) {
        if (strcmp(aEvents[i].date, sCurrentDate) >= 0) up[count++] = &aEvents[i];
    }
    qsort(up, count, sizeof(Event*), CompareEvents);
    
    int y = 20 - iScrollY;
    for (int i = 0; i < count; i++) {
        if (y + 100 > 0 && y < h) {
            int cardLeft = 20 - iScrollX;
            int cardRight = effW - 20 - iScrollX;
            int cardTop = y, cardBottom = y + 95;
            
            HBRUSH hBrushEv = CreateSolidBrush(up[i]->color);
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
            
            if (up[i] != &aEvents[iEditingIndex]) {
                SelectObject(hDC, hFontTitle); SetTextColor(hDC, RGB_HEX(0x202124));
                RECT tTitle = {cardLeft + 35, y + 16, cardRight - 15, y + 50};
                DrawTextA(hDC, up[i]->title, -1, &tTitle, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            
            char sTime1[32], sTime2[32];
            MinToTimeString(up[i]->startMin, sTime1);
            MinToTimeString(up[i]->startMin + up[i]->duration, sTime2);
            char sPerson[64] = "";
            if (up[i]->personIdx < numPeople) strcpy(sPerson, aPeople[up[i]->personIdx]);
            
            char desc[256];
            sprintf(desc, "%s       %s - %s       %s", up[i]->date, sTime1, sTime2, sPerson);
            SelectObject(hDC, hFontText); SetTextColor(hDC, RGB_HEX(0x5F6368));
            RECT tDesc = {cardLeft + 35, y + 56, cardRight - 15, y + 86};
            DrawTextA(hDC, desc, -1, &tDesc, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        y += 115;
    }
    free(up);
    DeleteObject(hFontTitle); DeleteObject(hFontText);
}

/* --- Printing & Export --- */
void ExportUpcomingSchedule() {
    OPENFILENAME ofn = {0}; char szFile[MAX_PATH] = "Upcoming_Schedule.txt";
    ofn.lStructSize = sizeof(ofn); ofn.hwndOwner = hMainGUI;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0"; ofn.lpstrFile = szFile; ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileName(&ofn)) {
        FILE* fp = fopen(szFile, "w");
        if (fp) {
            fprintf(fp, "========================================\n           UPCOMING SCHEDULE            \n========================================\n\n");
            Event** up = (Event**)malloc(numEvents * sizeof(Event*)); int count = 0;
            for(int i=0; i<numEvents; i++) if(strcmp(aEvents[i].date, sCurrentDate)>=0) up[count++] = &aEvents[i];
            qsort(up, count, sizeof(Event*), CompareEvents);
            for (int i = 0; i < count; i++) {
                char sTime1[32], sTime2[32];
                MinToTimeString(up[i]->startMin, sTime1);
                MinToTimeString(up[i]->startMin + up[i]->duration, sTime2);
                char sPerson[64] = "";
                if (up[i]->personIdx < numPeople) strcpy(sPerson, aPeople[up[i]->personIdx]);
                fprintf(fp, "%s\n%s       %s - %s       %s\n----------------------------------------\n",
                        up[i]->title, up[i]->date, sTime1, sTime2, sPerson);
            }
            fclose(fp); free(up);
            MessageBox(hMainGUI, "Export Successful", "Success", MB_OK | MB_ICONINFORMATION);
        }
    }
}

void PrintSchedule() {
    PRINTDLG pd = {0};
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = hMainGUI;
    pd.Flags = PD_RETURNDC | PD_NOPAGENUMS | PD_USEDEVMODECOPIESANDCOLLATE;
    
    if (PrintDlg(&pd) && pd.hDC) {
        DOCINFO di = { sizeof(DOCINFO), "Calendar Schedule Vector Print" };
        StartDoc(pd.hDC, &di);
        StartPage(pd.hDC);
        
        // Ensure transparent text rendering on printers to remove the white opaque box around text
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
    
    // Draw Gray Header Background
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
        int colIdx = -1;
        if (IsPeopleView()) { if (strcmp(aEvents[i].date, sCurrentDate) == 0) colIdx = aEvents[i].personIdx; }
        else { int d = DateDiffDays(sCurrentDate, aEvents[i].date); if (d >= 0 && d < cols) colIdx = d; }
        if (colIdx >= 0 && colIdx < cols) {
            int top = gridTop + (int)(aEvents[i].startMin * ((float)gridH / 1440.0f));
            int bot = gridTop + (int)((aEvents[i].startMin + aEvents[i].duration) * ((float)gridH / 1440.0f));
            int left = rPage.left + timeColW + (colIdx * dayColW) + (int)(dpiX * 0.03f);
            int right = left + dayColW - (int)(dpiX * 0.06f);
            
            HBRUSH hb = CreateSolidBrush(aEvents[i].color);
            HPEN hp = CreatePen(PS_SOLID, 1, DARKEN(aEvents[i].color, 20));
            SelectObject(hDC, hb); SelectObject(hDC, hp);
            
            // Dynamically scale rounded corner radius to printer resolution
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

    // Gray Headers Background
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
                    if (strcmp(aEvents[i].date, cellDate) == 0) dayEvents[count++] = i;
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
                        
                        // Dynamically scale rounded corners for month event pills
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
    
    Event** up = (Event**)malloc(numEvents * sizeof(Event*)); int count = 0;
    for (int i = 0; i < numEvents; i++) {
        if (strcmp(aEvents[i].date, sCurrentDate) >= 0) up[count++] = &aEvents[i];
    }
    qsort(up, count, sizeof(Event*), CompareEvents);
    
    int y = rPage.top + (int)(dpiY * 0.6);
    SelectObject(hDC, hfEv);
    SetTextColor(hDC, RGB_HEX(0x3C4043));
    for (int i = 0; i < count; i++) {
        if (y < rPage.bottom) {
            char line[512]; char sTime1[32], sTime2[32];
            MinToTimeString(up[i]->startMin, sTime1); MinToTimeString(up[i]->startMin + up[i]->duration, sTime2);
            char sPerson[64] = "";
            if (up[i]->personIdx < numPeople) strcpy(sPerson, aPeople[up[i]->personIdx]);
            sprintf(line, "%s       %s (%s - %s)       %s", up[i]->date, up[i]->title, sTime1, sTime2, sPerson);
            RECT rLine = {rPage.left, y, rPage.right, y + (int)(dpiY * 0.3)};
            DrawTextA(hDC, line, -1, &rLine, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += (int)(dpiY * 0.35);
        }
    }
    free(up);
    DeleteObject(hfTitle); DeleteObject(hfEv);
}