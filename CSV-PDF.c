/*
 * CSV-PDF Calendar Generator
 * 
 * Compilation Instructions:
 * gcc -Os -s -o CSV-PDF.exe main.c -mwindows -lcomctl32 -lgdi32 -lcomdlg32
 *
 * Disclaimer:
 * This software is provided "as is", without warranty of any kind. 
 * It is not fit for any commercial use or purpose, and is being released 
 * into the public domain.
 */

#define _WIN32_IE 0x0300
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

// --- Data Structures ---
typedef struct {
    char id[64];
    char title[128];
    int startMin;
    int duration;
    int color;
    int year, month, day;
    int personIdx;
    int version;
    int lastModifiedBy;
} Event;

Event* events = NULL;
int event_count = 0;
int event_capacity = 0;

char people[32][64];
int people_count = 0;

const char* months[] = {"", "January", "February", "March", "April", "May", "June", 
                        "July", "August", "September", "October", "November", "December"};
const char* short_months[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// --- GUI Globals ---
HWND hMain, hTxtCsv, hBtnBrowse, hCboMode, hCboFirstDay, hDateStart, hDateEnd;
HWND hBtnPrevMonth, hBtnNextMonth, hTxtMargH, hTxtMargV;
HWND hTxtWidth, hTxtHeight, hBtnToggle, hBtnCreate, hBtnExit;
int isLandscape = 0;
float page_w_mm = 215.9f; 
float page_h_mm = 279.4f;

const char* view_modes[] = {
    "1 Day View", "4 Day View", "Week View", 
    "4 Person View", "7 Person View", "Month View", "Upcoming Schedule"
};

const char* days_of_week[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};

// --- Utilities ---
void sanitize_pdf_string(char* str) {
    while(*str) {
        if(*str == '(' || *str == ')' || *str == '\\') *str = '_';
        str++;
    }
}

void url_decode(char *str) {
    char *p = str;
    char hex[3] = {0};
    while (*str) {
        if (*str == '%' && *(str+1) && *(str+2)) {
            hex[0] = *(str+1); hex[1] = *(str+2);
            *p++ = (char)strtol(hex, NULL, 16);
            str += 3;
        } else {
            *p++ = *str++;
        }
    }
    *p = '\0';
}

char* next_token(char** ptr) {
    if (!*ptr) return NULL;
    char* start = *ptr;
    while (**ptr) {
        if (**ptr == '|' || (unsigned char)**ptr == 0xA6) {
            **ptr = '\0'; *ptr += 1; return start;
        } else if ((unsigned char)**ptr == 0xC2 && (unsigned char)*(*ptr+1) == 0xA6) {
            **ptr = '\0'; *ptr += 2; return start;
        }
        (*ptr)++;
    }
    *ptr = NULL;
    return start;
}

int load_ini() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    char* p = strrchr(path, '.');
    if (p) strcpy(p, ".ini");
    else strcat(path, ".ini");

    char buf[1024] = {0};
    GetPrivateProfileStringA("People", "Names", "Unknown", buf, sizeof(buf), path);
    
    char* token = strtok(buf, ",");
    people_count = 0;
    while(token && people_count < 32) {
        strncpy(people[people_count++], token, 63);
        token = strtok(NULL, ",");
    }
    return people_count;
}

int parse_csv(const char* filepath) {
    FILE* f = fopen(filepath, "r");
    if (!f) return 0;
    char line[1024];
    
    event_count = 0;
    if(fgets(line, sizeof(line), f)) {} // skip header

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        char* ptr = line;
        char* t_id = next_token(&ptr);
        char* t_title = next_token(&ptr);
        char* t_start = next_token(&ptr);
        char* t_dur = next_token(&ptr);
        char* t_col = next_token(&ptr);
        char* t_date = next_token(&ptr);
        char* t_pidx = next_token(&ptr);
        char* t_ver = next_token(&ptr);
        char* t_lmod = next_token(&ptr);

        if(!t_date) continue;

        if (event_count >= event_capacity) {
            event_capacity = event_capacity == 0 ? 64 : event_capacity * 2;
            events = (Event*)realloc(events, event_capacity * sizeof(Event));
        }

        Event* e = &events[event_count++];
        strncpy(e->id, t_id ? t_id : "", 63);
        strncpy(e->title, t_title ? t_title : "New Event", 127);
        
        url_decode(e->title);
        sanitize_pdf_string(e->title);
        
        e->startMin = t_start ? atoi(t_start) : 0;
        e->duration = t_dur ? atoi(t_dur) : 60;
        e->color = t_col ? atoi(t_col) : 0;
        
        if (t_date) sscanf(t_date, "%d/%d/%d", &e->year, &e->month, &e->day);
        e->personIdx = t_pidx ? atoi(t_pidx) : 0;
        e->version = t_ver ? atoi(t_ver) : 1;
        e->lastModifiedBy = t_lmod ? atoi(t_lmod) : 0;
    }
    fclose(f);
    return event_count;
}

int cmp_events(const void* a, const void* b) {
    Event* ea = (Event*)a;
    Event* eb = (Event*)b;
    if (ea->year != eb->year) return ea->year - eb->year;
    if (ea->month != eb->month) return ea->month - eb->month;
    if (ea->day != eb->day) return ea->day - eb->day;
    return ea->startMin - eb->startMin;
}

// --- PDF Generation Engine ---
long pdf_objects[4096];
int pdf_obj_cnt = 1;

typedef struct { char* data; int len; int cap; } Stream;

void s_app(Stream* s, const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (s->len + n >= s->cap) {
        s->cap = (s->cap == 0 ? 4096 : s->cap * 2) + n + 1024;
        s->data = realloc(s->data, s->cap);
    }
    memcpy(s->data + s->len, buf, n);
    s->len += n;
    s->data[s->len] = 0;
}

void pdf_color(Stream* s, int is_stroke, int hex_color) {
    float r = ((hex_color >> 16) & 0xFF) / 255.0f;
    float g = ((hex_color >> 8) & 0xFF) / 255.0f;
    float b = (hex_color & 0xFF) / 255.0f;
    s_app(s, "%.3f %.3f %.3f %s\n", r, g, b, is_stroke ? "RG" : "rg");
}

void pdf_text_color(Stream* s, int hex_color) {
    int r = (hex_color >> 16) & 0xFF;
    int g = (hex_color >> 8) & 0xFF;
    int b = hex_color & 0xFF;
    float luma = 0.299f*r + 0.587f*g + 0.114f*b;
    if (luma > 186.0f) s_app(s, "0 0 0 rg\n"); 
    else s_app(s, "1 1 1 rg\n"); 
}

void pdf_center_text(Stream* s, const char* text, float x, float y, float w, int font_size, int is_bold) {
    float text_w = strlen(text) * font_size * 0.5f; 
    float offset = x + (w - text_w) / 2.0f;
    if (offset < x) offset = x; 
    s_app(s, "BT %s %d Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", is_bold ? "/F2" : "/F1", font_size, offset, y, text);
}

void pdf_rounded_rect(Stream* s, float x, float y, float w, float h, float r) {
    float k = 0.55228f * r;
    s_app(s, "%.2f %.2f m\n", x + r, y + h);
    s_app(s, "%.2f %.2f l\n", x + w - r, y + h); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w - r + k, y + h, x + w, y + h - r + k, x + w, y + h - r);
    s_app(s, "%.2f %.2f l\n", x + w, y + r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + w, y + r - k, x + w - r + k, y, x + w - r, y);
    s_app(s, "%.2f %.2f l\n", x + r, y); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x + r - k, y, x, y + r - k, x, y + r);
    s_app(s, "%.2f %.2f l\n", x, y + h - r); 
    s_app(s, "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x, y + h - r + k, x + r - k, y + h, x + r, y + h);
    s_app(s, "f\n");
}

int day_of_week(int y, int m, int d) {
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100;
    int j = y / 100;
    int h = (d + 13*(m+1)/5 + k + k/4 + j/4 + 5*j) % 7;
    return (h + 5) % 7;
}

int days_in_month(int y, int m) {
    if (m == 2) return ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : 28;
    if (m==4 || m==6 || m==9 || m==11) return 30;
    return 31;
}

void format_time(int min_since_midnight, char* buf) {
    int h = min_since_midnight / 60;
    int m = min_since_midnight % 60;
    const char* ampm = "AM";
    if (h >= 12) { ampm = "PM"; if (h > 12) h -= 12; }
    if (h == 0) h = 12;
    sprintf(buf, "%02d:%02d %s", h, m, ampm);
}

void generate_pdf(const char* out_file, int mode, int first_day, float margH_pct, float margV_pct, int sY, int sM, int sD, int eY, int eM, int eD) {
    FILE* f = fopen(out_file, "wb");
    if (!f) return;
    
    float pt_w = page_w_mm * 72.0f / 25.4f;
    float pt_h = page_h_mm * 72.0f / 25.4f;

    float m_x = pt_w * (margH_pct / 100.0f);
    float m_y = pt_h * (margV_pct / 100.0f);

    // PDF 1.1 Compatible Header
    fprintf(f, "%%PDF-1.1\n");
    
    // Core Reference Objects
    int info_obj = 1;
    int catalog_obj = 2;
    int pages_obj = 3;
    int font1_obj = 4;
    int font2_obj = 5;
    pdf_obj_cnt = 6;

    // Build Info Dictionary (Metadata)
    const char* title_file = strrchr(out_file, '\\');
    if (!title_file) title_file = strrchr(out_file, '/');
    if (!title_file) title_file = out_file; else title_file++;

    pdf_objects[info_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Title (%s) /Creator (CSV-PDF) /Producer (CSV-PDF) /Author (CSV-PDF) >>\nendobj\n", info_obj, title_file);

    pdf_objects[catalog_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Catalog /Pages %d 0 R >>\nendobj\n", catalog_obj, pages_obj);

    pdf_objects[font1_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n", font1_obj);

    pdf_objects[font2_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>\nendobj\n", font2_obj);

    Event* v_events = malloc(event_count * sizeof(Event));
    int v_count = 0;
    for (int i = 0; i < event_count; i++) {
        long d1 = sY*10000 + sM*100 + sD;
        long d2 = eY*10000 + eM*100 + eD;
        long de = events[i].year*10000 + events[i].month*100 + events[i].day;
        if (de >= d1 && de <= d2) {
            v_events[v_count++] = events[i];
        }
    }
    qsort(v_events, v_count, sizeof(Event), cmp_events);

    int page_list[1024];
    int page_cnt = 0;
    Stream s = {0};
    
    if (mode == 6) { // --- Upcoming Schedule ---
        float y = pt_h - m_y - 40;
        
        int page_obj = pdf_obj_cnt++;
        int stream_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        pdf_center_text(&s, "Upcoming Schedule", m_x, pt_h - m_y - 20, pt_w - m_x*2, 24, 1);

        int last_y = -1, last_m = -1, last_d = -1;
        for (int i = 0; i < v_count; i++) {
            Event* ev = &v_events[i];
            int is_new_day = (ev->year != last_y || ev->month != last_m || ev->day != last_d);
            
            float space_needed = 60;
            if (is_new_day) space_needed += 40;

            if (y < m_y + space_needed) {
                // Flush page
                pdf_objects[stream_obj] = ftell(f);
                fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);
                s.len = 0; 
                
                // Start new page
                page_obj = pdf_obj_cnt++;
                stream_obj = pdf_obj_cnt++;
                page_list[page_cnt++] = page_obj;

                pdf_objects[page_obj] = ftell(f);
                fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
                    page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);
                
                y = pt_h - m_y - 40;
                pdf_center_text(&s, "Upcoming Schedule (Cont.)", m_x, pt_h - m_y - 20, pt_w - m_x*2, 16, 1);
                is_new_day = 1;
            }

            if (is_new_day) {
                y -= 10;
                char date_hd[64];
                sprintf(date_hd, "%s %d, %d", months[ev->month], ev->day, ev->year);
                s_app(&s, "BT /F2 14 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x, y, date_hd);
                y -= 5;
                s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x, y, pt_w - m_x, y);
                y -= 25;
                last_y = ev->year; last_m = ev->month; last_d = ev->day;
            }

            pdf_color(&s, 0, ev->color);
            pdf_rounded_rect(&s, m_x, y - 2, 12, 12, 2);

            s_app(&s, "BT /F2 12 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x + 20, y, ev->title);
            y -= 15;

            char time_start[16], time_end[16];
            format_time(ev->startMin, time_start);
            format_time(ev->startMin + ev->duration, time_end);
            
            char details[256];
            char pname[64];
            if (ev->lastModifiedBy < people_count && strlen(people[ev->lastModifiedBy]) > 0) {
                strcpy(pname, people[ev->lastModifiedBy]);
            } else {
                sprintf(pname, "Person %d", ev->lastModifiedBy + 1);
            }

            sprintf(details, "%s - %s   \\225   %s", time_start, time_end, pname);

            s_app(&s, "BT /F1 10 Tf 0.3 0.3 0.3 rg %.2f %.2f Td (%s) Tj ET\n", m_x + 20, y, details);
            y -= 20;
        }

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);

    } else if (mode == 5) { // --- Month View ---
        int page_obj = pdf_obj_cnt++;
        int stream_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        float gw = pt_w - m_x*2;
        float gh = pt_h - m_y*2 - 60; // Make room for title
        float cw = gw / 7.0f;
        float ch = gh / 6.0f;

        char mo_title[64];
        sprintf(mo_title, "%s %d", months[sM], sY);
        pdf_center_text(&s, mo_title, m_x, pt_h - m_y - 20, gw, 24, 1);

        const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        for(int i=0; i<7; i++) {
            int d_idx = (i + first_day) % 7;
            s_app(&s, "BT /F2 12 Tf 0 0 0 rg %.2f %.2f Td (%s) Tj ET\n", m_x + i*cw + 5, pt_h - m_y - 40, day_names[d_idx]);
        }

        int m_days = days_in_month(sY, sM);
        int start_dow = day_of_week(sY, sM, 1);
        int start_cell = (start_dow - first_day + 7) % 7;

        // Render Light Gray Background for Out-of-Month Days
        for (int i = 0; i < 42; i++) {
            if (i < start_cell || i >= start_cell + m_days) {
                int c = i % 7;
                int r = 5 - (i / 7);
                float cx = m_x + c * cw;
                float cy = m_y + r * ch;
                s_app(&s, "0.9 0.9 0.9 rg %.2f %.2f %.2f %.2f re f\n", cx, cy, cw, ch);
            }
        }

        // Draw Grid Framework
        s_app(&s, "0.7 0.7 0.7 RG 1 w\n");
        for(int i=0; i<=7; i++) { s_app(&s, "%.2f %.2f m %.2f %.2f l S\n", m_x + i*cw, m_y, m_x + i*cw, m_y + gh); }
        for(int i=0; i<=6; i++) { s_app(&s, "%.2f %.2f m %.2f %.2f l S\n", m_x, m_y + i*ch, m_x + gw, m_y + i*ch); }

        int row = 5, col = start_cell;
        for(int d=1; d<=m_days; d++) {
            float cx = m_x + col * cw;
            float cy = m_y + row * ch;
            
            char dstr[4]; sprintf(dstr, "%d", d);
            s_app(&s, "BT /F1 10 Tf 0.2 0.2 0.2 rg %.2f %.2f Td (%s) Tj ET\n", cx + cw - 20, cy + ch - 12, dstr);

            int ev_in_cell = 0;
            float ey = cy + ch - 25;
            for(int i=0; i<v_count; i++) {
                if(v_events[i].year == sY && v_events[i].month == sM && v_events[i].day == d) {
                    if (ev_in_cell < 4) {
                        pdf_color(&s, 0, v_events[i].color);
                        pdf_rounded_rect(&s, cx + 2, ey - 12, cw - 4, 14, 4);
                        
                        pdf_text_color(&s, v_events[i].color);
                        char tt[32];
                        strncpy(tt, v_events[i].title, 20); tt[20]=0;
                        s_app(&s, "BT /F1 8 Tf %.2f %.2f Td (%s) Tj ET\n", cx + 4, ey - 8, tt);
                        ey -= 16;
                    } else if (ev_in_cell == 4) {
                        s_app(&s, "BT /F2 8 Tf 0 0 0 rg %.2f %.2f Td (+ more) Tj ET\n", cx + 4, ey - 8);
                    }
                    ev_in_cell++;
                }
            }
            col++;
            if(col > 6) { col = 0; row--; }
        }

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);

    } else { // --- Dayplanner / Column Views (0, 1, 2, 3, 4) ---
        int page_obj = pdf_obj_cnt++;
        int stream_obj = pdf_obj_cnt++;
        page_list[page_cnt++] = page_obj;

        pdf_objects[page_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] /Contents %d 0 R /Resources << /Font << /F1 %d 0 R /F2 %d 0 R >> >> >>\nendobj\n", 
            page_obj, pages_obj, pt_w, pt_h, stream_obj, font1_obj, font2_obj);

        int num_cols = 1;
        if (mode == 1 || mode == 3) num_cols = 4;
        if (mode == 2 || mode == 4) num_cols = 7;
        
        int is_person_view = (mode == 3 || mode == 4);
        
        float m_top = m_y + 50;
        float time_w = 42.0f; // Expanded dedicated width for time text labels (with leading zeros)
        float m_x_grid = m_x + time_w;
        float gw = pt_w - m_x*2 - time_w; 
        float gh = pt_h - m_y - m_top;
        float cw = gw / num_cols;
        
        // Setup base start date for columns
        struct tm base_t;
        memset(&base_t, 0, sizeof(base_t));
        base_t.tm_year = sY - 1900; 
        base_t.tm_mon = sM - 1; 
        base_t.tm_mday = sD;
        base_t.tm_isdst = -1;
        
        char planner_title[128];
        if (is_person_view) {
            sprintf(planner_title, "Schedule - %s %d, %d", months[sM], sD, sY);
        } else {
            if (num_cols == 1) sprintf(planner_title, "%s %d, %d", months[sM], sD, sY);
            else sprintf(planner_title, "Week of %s %d, %d", short_months[sM], sD, sY);
        }
        pdf_center_text(&s, planner_title, m_x_grid, pt_h - m_y - 20, gw, 20, 1);

        for(int c=0; c<num_cols; c++) {
            char chd[64];
            if(is_person_view) {
                if (c < people_count && strlen(people[c]) > 0) {
                    sprintf(chd, "%s", people[c]);
                } else {
                    sprintf(chd, "Person %d", c + 1);
                }
            } else {
                struct tm t = base_t;
                t.tm_mday += c;
                mktime(&t);
                sprintf(chd, "%s %d", short_months[t.tm_mon + 1], t.tm_mday);
            }
            pdf_center_text(&s, chd, m_x_grid + c*cw, pt_h - m_top + 10, cw, 12, 1);
            
            // Draw column lines
            s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid + c*cw, m_y, m_x_grid + c*cw, m_y + gh);
        }
        s_app(&s, "0.5 0.5 0.5 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid + gw, m_y, m_x_grid + gw, m_y + gh); // Last Line

        // Hourly lines (0 - 24)
        for(int h=0; h<=24; h++) {
            float hy = m_y + gh - (h / 24.0f) * gh;
            s_app(&s, "0.8 0.8 0.8 RG 1 w %.2f %.2f m %.2f %.2f l S\n", m_x_grid, hy, m_x_grid + gw, hy);
            if(h < 24 && h % 2 == 0) { // Labels every 2 hours
                char hl[16]; format_time(h*60, hl);
                // Positioned further left to avoid crowding the grid line at tight margins
                s_app(&s, "BT /F1 8 Tf 0.5 0.5 0.5 rg %.2f %.2f Td (%s) Tj ET\n", m_x_grid - 39, hy - 3, hl);
            }
        }
        
        for(int i=0; i<v_count; i++) {
            Event* ev = &v_events[i];
            int col = -1;
            
            if(is_person_view) {
                if(ev->lastModifiedBy < num_cols) col = ev->lastModifiedBy;
            } else {
                struct tm te;
                memset(&te, 0, sizeof(te));
                te.tm_year = ev->year - 1900; 
                te.tm_mon = ev->month - 1; 
                te.tm_mday = ev->day;
                te.tm_isdst = -1;
                
                time_t t1 = mktime(&base_t);
                time_t t2 = mktime(&te);
                int diff = difftime(t2, t1) / 86400;
                if(diff >= 0 && diff < num_cols) col = diff;
            }
            
            if(col >= 0) {
                float ev_y_start = m_y + gh - (ev->startMin / 1440.0f) * gh;
                float ev_h = (ev->duration / 1440.0f) * gh;
                if (ev_h < 12) ev_h = 12; // Minimum height to display text
                float ev_y = ev_y_start - ev_h;
                
                pdf_color(&s, 0, ev->color);
                pdf_rounded_rect(&s, m_x_grid + col*cw + 2, ev_y, cw - 4, ev_h, 3);
                
                pdf_text_color(&s, ev->color);
                char tt[32]; strncpy(tt, ev->title, 20); tt[20]=0;
                s_app(&s, "BT /F2 8 Tf %.2f %.2f Td (%s) Tj ET\n", m_x_grid + col*cw + 4, ev_y_start - 10, tt);
            }
        }

        pdf_objects[stream_obj] = ftell(f);
        fprintf(f, "%d 0 obj\n<< /Length %d >>\nstream\n%s\nendstream\nendobj\n", stream_obj, s.len, s.data);
    }

    // Write Pages Dictionary Object
    pdf_objects[pages_obj] = ftell(f);
    fprintf(f, "%d 0 obj\n<< /Type /Pages /Count %d /Kids [ ", pages_obj, page_cnt);
    for (int i = 0; i < page_cnt; i++) {
        fprintf(f, "%d 0 R ", page_list[i]);
    }
    fprintf(f, "] >>\nendobj\n");

    // Write XREF Table & Trailer
    long xref_pos = ftell(f);
    fprintf(f, "xref\n0 %d\n0000000000 65535 f \n", pdf_obj_cnt);
    for(int i = 1; i < pdf_obj_cnt; i++) {
        fprintf(f, "%010ld 00000 n \n", pdf_objects[i]);
    }
    
    fprintf(f, "trailer\n<< /Size %d /Root %d 0 R /Info %d 0 R >>\nstartxref\n%ld\n%%%%EOF\n", pdf_obj_cnt, catalog_obj, info_obj, xref_pos);
    
    if(s.data) free(s.data);
    free(v_events);
    fclose(f);
}

// --- GUI Callbacks & Init ---
void CreatePDFAction(HWND hwnd) {
    char csv_path[MAX_PATH];
    GetWindowTextA(hTxtCsv, csv_path, MAX_PATH);
    if (!parse_csv(csv_path)) {
        MessageBoxA(hwnd, "Failed to load/parse CSV.", "Error", MB_ICONERROR);
        return;
    }

    // Default Filename Generation (YYYY-MM_View.pdf)
    char out_path[MAX_PATH] = "";
    char view_text[64] = "";
    int mode = SendMessageA(hCboMode, CB_GETCURSEL, 0, 0);
    SendMessageA(hCboMode, CB_GETLBTEXT, mode, (LPARAM)view_text);
    
    SYSTEMTIME stStart;
    SendMessage(hDateStart, DTM_GETSYSTEMTIME, 0, (LPARAM)&stStart);
    
    sprintf(out_path, "%04d-%02d_%s.pdf", stStart.wYear, stStart.wMonth, view_text);

    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = out_path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    
    if (GetSaveFileNameA(&ofn)) {
        int first_day = SendMessageA(hCboFirstDay, CB_GETCURSEL, 0, 0);
        
        char margH_str[16], margV_str[16];
        GetWindowTextA(hTxtMargH, margH_str, 16);
        GetWindowTextA(hTxtMargV, margV_str, 16);
        float margH_pct = atof(margH_str);
        float margV_pct = atof(margV_str);
        
        SYSTEMTIME stEnd;
        SendMessage(hDateEnd, DTM_GETSYSTEMTIME, 0, (LPARAM)&stEnd);

        generate_pdf(out_path, mode, first_day, margH_pct, margV_pct,
            stStart.wYear, stStart.wMonth, stStart.wDay, 
            stEnd.wYear, stEnd.wMonth, stEnd.wDay);
        
        MessageBoxA(hwnd, "PDF Created Successfully!", "Success", MB_ICONINFORMATION);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CREATE: {
            CreateWindowA("STATIC", "CSV File:", WS_VISIBLE | WS_CHILD, 10, 15, 80, 20, hwnd, NULL, NULL, NULL);
            hTxtCsv = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "calendar.csv", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 90, 15, 200, 22, hwnd, NULL, NULL, NULL);
            hBtnBrowse = CreateWindowA("BUTTON", "Browse...", WS_VISIBLE | WS_CHILD, 300, 15, 80, 22, hwnd, (HMENU)1, NULL, NULL);

            CreateWindowA("STATIC", "View:", WS_VISIBLE | WS_CHILD, 10, 50, 40, 20, hwnd, NULL, NULL, NULL);
            hCboMode = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD, 55, 50, 150, 200, hwnd, NULL, NULL, NULL);
            for(int i = 0; i < 7; i++) SendMessageA(hCboMode, CB_ADDSTRING, 0, (LPARAM)view_modes[i]);
            SendMessageA(hCboMode, CB_SETCURSEL, 5, 0); 

            CreateWindowA("STATIC", "1st Day:", WS_VISIBLE | WS_CHILD, 215, 50, 55, 20, hwnd, NULL, NULL, NULL);
            hCboFirstDay = CreateWindowA("COMBOBOX", "", CBS_DROPDOWNLIST | WS_VISIBLE | WS_CHILD, 275, 50, 100, 200, hwnd, NULL, NULL, NULL);
            for(int i = 0; i < 7; i++) SendMessageA(hCboFirstDay, CB_ADDSTRING, 0, (LPARAM)days_of_week[i]);
            SendMessageA(hCboFirstDay, CB_SETCURSEL, 0, 0); 

            CreateWindowA("STATIC", "Date Range:", WS_VISIBLE | WS_CHILD, 10, 85, 80, 20, hwnd, NULL, NULL, NULL);
            hDateStart = CreateWindowExA(0, DATETIMEPICK_CLASS, "", WS_VISIBLE | WS_CHILD, 90, 85, 95, 22, hwnd, NULL, NULL, NULL);
            hDateEnd = CreateWindowExA(0, DATETIMEPICK_CLASS, "", WS_VISIBLE | WS_CHILD, 195, 85, 95, 22, hwnd, NULL, NULL, NULL);
            
            hBtnPrevMonth = CreateWindowA("BUTTON", "< Prev", WS_VISIBLE | WS_CHILD, 300, 85, 55, 22, hwnd, (HMENU)5, NULL, NULL);
            hBtnNextMonth = CreateWindowA("BUTTON", "Next >", WS_VISIBLE | WS_CHILD, 365, 85, 55, 22, hwnd, (HMENU)6, NULL, NULL);
            
            SYSTEMTIME st; GetLocalTime(&st);
            st.wDay = 1;
            SendMessage(hDateStart, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
            st.wDay = days_in_month(st.wYear, st.wMonth);
            SendMessage(hDateEnd, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);

            CreateWindowA("STATIC", "Size(mm):", WS_VISIBLE | WS_CHILD, 10, 120, 65, 20, hwnd, NULL, NULL, NULL);
            hTxtWidth = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "215.9", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 75, 120, 45, 22, hwnd, NULL, NULL, NULL);
            CreateWindowA("STATIC", "x", WS_VISIBLE | WS_CHILD, 123, 120, 10, 20, hwnd, NULL, NULL, NULL);
            hTxtHeight = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "279.4", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 135, 120, 45, 22, hwnd, NULL, NULL, NULL);
            
            hBtnToggle = CreateWindowA("BUTTON", "Land", WS_VISIBLE | WS_CHILD, 185, 120, 45, 22, hwnd, (HMENU)2, NULL, NULL);

            CreateWindowA("STATIC", "Marg(%):", WS_VISIBLE | WS_CHILD, 240, 120, 55, 20, hwnd, NULL, NULL, NULL);
            hTxtMargH = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "4", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 295, 120, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindowA("STATIC", "H", WS_VISIBLE | WS_CHILD, 330, 120, 15, 20, hwnd, NULL, NULL, NULL);
            hTxtMargV = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "4", WS_VISIBLE | WS_CHILD | ES_AUTOHSCROLL, 345, 120, 30, 22, hwnd, NULL, NULL, NULL);
            CreateWindowA("STATIC", "V", WS_VISIBLE | WS_CHILD, 380, 120, 15, 20, hwnd, NULL, NULL, NULL);

            hBtnCreate = CreateWindowA("BUTTON", "Create PDF", WS_VISIBLE | WS_CHILD, 120, 160, 100, 30, hwnd, (HMENU)3, NULL, NULL);
            hBtnExit = CreateWindowA("BUTTON", "Exit", WS_VISIBLE | WS_CHILD, 240, 160, 100, 30, hwnd, (HMENU)4, NULL, NULL);
            break;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == 1) { 
                OPENFILENAMEA ofn = {0};
                char path[MAX_PATH] = "";
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
                ofn.lpstrFile = path;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if (GetOpenFileNameA(&ofn)) {
                    SetWindowTextA(hTxtCsv, path);
                }
            } else if (LOWORD(wParam) == 2) { 
                isLandscape = !isLandscape;
                SetWindowTextA(hBtnToggle, isLandscape ? "Port" : "Land");
                char w_str[32], h_str[32];
                GetWindowTextA(hTxtWidth, w_str, 32);
                GetWindowTextA(hTxtHeight, h_str, 32);
                SetWindowTextA(hTxtWidth, h_str);
                SetWindowTextA(hTxtHeight, w_str);
                
                page_w_mm = atof(h_str);
                page_h_mm = atof(w_str);
            } else if (LOWORD(wParam) == 3) { 
                char w_str[32], h_str[32];
                GetWindowTextA(hTxtWidth, w_str, 32); GetWindowTextA(hTxtHeight, h_str, 32);
                page_w_mm = atof(w_str); page_h_mm = atof(h_str);
                CreatePDFAction(hwnd);
            } else if (LOWORD(wParam) == 4) { 
                PostQuitMessage(0);
            } else if (LOWORD(wParam) == 5 || LOWORD(wParam) == 6) { 
                SYSTEMTIME st;
                SendMessage(hDateStart, DTM_GETSYSTEMTIME, 0, (LPARAM)&st);
                if (LOWORD(wParam) == 5) {
                    if (st.wMonth == 1) { st.wMonth = 12; st.wYear--; }
                    else { st.wMonth--; }
                } else {
                    if (st.wMonth == 12) { st.wMonth = 1; st.wYear++; }
                    else { st.wMonth++; }
                }
                st.wDay = 1;
                SendMessage(hDateStart, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
                st.wDay = days_in_month(st.wYear, st.wMonth);
                SendMessage(hDateEnd, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&st);
            }
            break;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- Entry Point ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_DATE_CLASSES;
    InitCommonControlsEx(&icex);
    
    load_ini();

    if (__argc > 1) {
        const char* in_csv = "calendar.csv";
        const char* out_pdf = "calendar.pdf";
        int mode = 5; 
        int first_day = 0;
        float margH_pct = 4.0f, margV_pct = 4.0f;
        int sY=2026, sM=1, sD=1, eY=2026, eM=1, eD=31;
        
        if (__argc >= 3) { in_csv = __argv[1]; out_pdf = __argv[2]; }
        
        for (int i = 3; i < __argc; i++) {
            if (strncmp(__argv[i], "-mode=", 6) == 0) {
                mode = atoi(__argv[i] + 6);
            } else if (strncmp(__argv[i], "-start=", 7) == 0) {
                sscanf(__argv[i] + 7, "%4d%2d%2d", &sY, &sM, &sD);
            } else if (strncmp(__argv[i], "-end=", 5) == 0) {
                sscanf(__argv[i] + 5, "%4d%2d%2d", &eY, &eM, &eD);
            }
        }
        
        if (parse_csv(in_csv)) {
            generate_pdf(out_pdf, mode, first_day, margH_pct, margV_pct, sY, sM, sD, eY, eM, eD);
        }
        return 0;
    }

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "CSV_PDF_CLASS";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    hMain = CreateWindowExA(0, wc.lpszClassName, "CSV to Calendar PDF", 
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX, 
        CW_USEDEFAULT, CW_USEDEFAULT, 420, 250, NULL, NULL, hInstance, NULL);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    if(events) free(events);
    return msg.wParam;
}