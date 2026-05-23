// Client.cpp
// CourseSystem GUI Client with real multi-page navigation.
// Entry point is int main(), not WinMain.
// Replace the old Client.cpp with this file.
//
// Project files needed in CourseClient:
//   Client.cpp
//   json.hpp
//
// Visual Studio:
//   C/C++ -> Language -> ISO C++17
//   This file uses main() but opens a Windows GUI window.
//   The pragma below sets Windows subsystem and mainCRTStartup.
//
// Run order:
//   1. Run CourseServer first.
//   2. Run this client.
//   3. Click Connect.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>

#include "json.hpp"
#include "CryptoUtils.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#pragma comment(linker, "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

using json = nlohmann::json;

// ------------------------------- IDs -------------------------------

#define IDC_ACCOUNT        1001
#define IDC_PASSWORD       1002
#define IDC_HOST           1003
#define IDC_PORT           1004
#define IDC_CONNECT        1005
#define IDC_DISCONNECT     1006
#define IDC_STATUS         1007
#define IDC_LOGIN          1008

#define IDC_DASH_SEARCH    1101
#define IDC_DASH_CODE      1102
#define IDC_DASH_SECTION   1103
#define IDC_DASH_LIST      1104
#define IDC_DASH_DETAIL    1105
#define IDC_DASH_SCHEDULE  1106
#define IDC_DASH_TABLE     1107
#define IDC_DASH_LOG       1108
#define IDC_DASH_CODE_SEARCH      1109
#define IDC_DASH_INST_SEARCH      1110
#define IDC_DASH_SEARCH_CODE_BTN  1111
#define IDC_DASH_SEARCH_INST_BTN  1112
#define IDC_DASH_SEMESTER_INPUT  1113
#define IDC_DASH_SEMESTER_BTN    1114
#define IDC_DASH_CATEGORY_SEARCH  1115
#define IDC_DASH_SEARCH_CAT_BTN   1116


#define IDC_COURSE_CODE    1201
#define IDC_COURSE_SECTION 1202
#define IDC_COURSE_TITLE   1203
#define IDC_COURSE_INST    1204
#define IDC_COURSE_UNITS   1205
#define IDC_COURSE_TYPE    1206
#define IDC_COURSE_DESC    1207
#define IDC_COURSE_ADD     1208
#define IDC_COURSE_UPDATE  1209
#define IDC_COURSE_DELETE  1210
#define IDC_COURSE_RELOAD  1211
#define IDC_COURSE_DETAIL  1216
#define IDC_COURSE_SAMPLE  1212
#define IDC_COURSE_CLEAR   1213
#define IDC_COURSE_TABLE   1214
#define IDC_COURSE_LOG     1215

#define IDC_SCHED_CODE     1301
#define IDC_SCHED_SECTION  1302
#define IDC_SCHED_GET      1303
#define IDC_SCHED_SAMPLE   1304
#define IDC_SCHED_CLEAR    1305
#define IDC_SCHED_TABLE    1306
#define IDC_SCHED_INFO     1307
#define IDC_SCHED_LOG      1308

#define IDC_SCHED_ID       1309
#define IDC_SCHED_DAY      1310
#define IDC_SCHED_START    1311
#define IDC_SCHED_END      1312
#define IDC_SCHED_ROOM     1313
#define IDC_SCHED_ADD      1314
#define IDC_SCHED_UPDATE   1315
#define IDC_SCHED_DELETE   1316


#define IDC_RAW_EDIT       1401
#define IDC_RAW_SEND       1402
#define IDC_RAW_SAMPLE1    1403
#define IDC_RAW_SAMPLE2    1404
#define IDC_RAW_LOG        1405

#define IDC_G_COMMON       1501
#define IDC_G_DASH_CTRL    1502
#define IDC_G_DASH_RESULT  1503
#define IDC_G_DASH_LOG     1504
#define IDC_G_COURSE_EDIT  1505
#define IDC_G_COURSE_REC   1506
#define IDC_G_COURSE_LOG   1507
#define IDC_G_SCHED_EDIT   1508
#define IDC_G_SCHED_RESULT 1509
#define IDC_G_SCHED_LOG    1510
#define IDC_G_RAW_TEST     1511
#define IDC_G_RAW_LOG      1512

// ------------------------------- Page -------------------------------

enum Page {
    PAGE_DASHBOARD = 0,
    PAGE_COURSES = 1,
    PAGE_SCHEDULE = 2,
    PAGE_JSON = 3
};

// ------------------------------- Globals -------------------------------

static HINSTANCE gInst = nullptr;
static HWND gMain = nullptr;
static SOCKET gSock = INVALID_SOCKET;
static bool gConnected = false;
static bool gLoggedIn = false;
static bool gIsAdmin = false;
static Page gPage = PAGE_DASHBOARD;
static HFONT gFont = nullptr;
static HFONT gFontBold = nullptr;
static HFONT gFontTitle = nullptr;
static HFONT gFontMono = nullptr;
static HBRUSH gBgBrush = nullptr;

static std::vector<HWND> gCommon;
static std::vector<HWND> gDashboard;
static std::vector<HWND> gCourses;
static std::vector<HWND> gSchedule;
static std::vector<HWND> gJson;
static std::vector<HWND>* gBuild = nullptr;

struct CourseRow {
    std::string code;
    std::string section;
    std::string title;
    std::string instructor;
    std::string units;
    std::string category;
};

static std::vector<CourseRow> gCachedCourses;

static bool RequireLogin();
static bool RequireAdmin();

// ------------------------------- Layout -------------------------------

static const int WINDOW_W = 1320;
static const int WINDOW_H = 820;
static const int SIDEBAR_W = 220;
static const int MAIN_X = 250;

static const COLORREF C_BG = RGB(245, 247, 251);
static const COLORREF C_SIDEBAR = RGB(15, 23, 42);
static const COLORREF C_CARD = RGB(255, 255, 255);
static const COLORREF C_BLUE = RGB(37, 99, 235);
static const COLORREF C_TEXT = RGB(17, 24, 39);
static const COLORREF C_MUTED = RGB(100, 116, 139);

// ------------------------------- UI helpers -------------------------------

static void Track(HWND hwnd) {
    if (hwnd && gBuild) {
        gBuild->push_back(hwnd);
    }
}

static HWND MakeCtrl(
    const char* cls,
    const char* text,
    DWORD style,
    int x,
    int y,
    int w,
    int h,
    int id = 0,
    HFONT font = nullptr,
    DWORD exStyle = 0
) {
    HWND ctrl = CreateWindowExA(
        exStyle,
        cls,
        text,
        style,
        x,
        y,
        w,
        h,
        gMain,
        id ? (HMENU)(INT_PTR)id : nullptr,
        gInst,
        nullptr
    );

    SendMessageA(ctrl, WM_SETFONT, (WPARAM)(font ? font : gFont), TRUE);
    Track(ctrl);
    return ctrl;
}

static HWND Label(const char* text, int x, int y, int w, int h, HFONT font = nullptr) {
    return MakeCtrl("STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, x, y, w, h, 0, font);
}

static HWND Edit(const char* text, int x, int y, int w, int h, int id, bool multiline = false, bool readonly = false, bool password = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL;

    if (multiline) {
        style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    }

    if (readonly) {
        style |= ES_READONLY;
    }

    if (password) {
        style |= ES_PASSWORD;
    }

    HWND ctrl = MakeCtrl("EDIT", text, style, x, y, w, h, id, multiline ? gFontMono : gFont, WS_EX_CLIENTEDGE);
    SendMessageA(ctrl, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(8, 8));
    return ctrl;
}

static HWND Combo(const char* text, int x, int y, int w, int h, int id, const std::vector<std::string>& items, bool allowCustom = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_HASSTRINGS | CBS_AUTOHSCROLL;
    style |= allowCustom ? CBS_DROPDOWN : CBS_DROPDOWNLIST;

    HWND ctrl = MakeCtrl("COMBOBOX", "", style, x, y, w, h, id, gFont, WS_EX_CLIENTEDGE);

    for (size_t i = 0; i < items.size(); ++i) {
        SendMessageA(ctrl, CB_ADDSTRING, 0, (LPARAM)items[i].c_str());
    }

    if (text && text[0]) {
        int idx = (int)SendMessageA(ctrl, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)text);
        if (idx != CB_ERR) {
            SendMessageA(ctrl, CB_SETCURSEL, idx, 0);
        }
        else {
            SetWindowTextA(ctrl, text);
        }
    }

    Track(ctrl);
    return ctrl;
}

static std::vector<std::string> UnitOptions() {
    return { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };
}

static std::vector<std::string> DayOptions() {
    return { "1", "2", "3", "4", "5", "6", "7" };
}

static std::vector<std::string> TimeOptions() {
    return {
        "08:00", "08:30", "09:00", "09:30",
        "10:00", "10:30", "11:00", "11:30",
        "12:00", "12:30", "13:00", "13:30",
        "14:00", "14:30", "15:00", "15:30",
        "16:00", "16:30", "17:00", "17:30",
        "18:00", "18:30", "19:00", "19:30",
        "20:00", "20:30", "21:00"
    };
}

static std::vector<std::string> CategoryOptions() {
    return {
        "MR",
        "UC",
        "WPE"
    };
}

static std::vector<std::string> SemesterOptions() {
    return {
        "2025-2026 S1",
        "2025-2026 S2",
        "2024-2025 S1",
        "2024-2025 S2",
        "2023-2024 S1",
        "2023-2024 S2"
    };
}

static std::vector<std::string> ClassroomOptions() {
    return {
        "T7-302",
        "T7-303",
        "T5-301",
        "T6-502",
        "T5-402",
        "Golf Field",
        "CC-132",
        "Music Room 101"
    };
}

static HWND Button(const char* text, int x, int y, int w, int h, int id) {
    return MakeCtrl("BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, x, y, w, h, id, gFontBold);
}

static HWND GroupBox(const char* text, int x, int y, int w, int h, int id = 0) {
    return MakeCtrl("BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, w, h, id, gFontBold);
}

static HWND ListViewBox(int x, int y, int w, int h, int id) {
    HWND list = MakeCtrl(
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | WS_HSCROLL | WS_VSCROLL | LVS_REPORT | LVS_SINGLESEL,
        x,
        y,
        w,
        h,
        id,
        gFont,
        WS_EX_CLIENTEDGE
    );

    SendMessageA(list, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    return list;
}

static void ShowGroup(std::vector<HWND>& group, bool visible) {
    for (HWND h : group) {
        ShowWindow(h, visible ? SW_SHOW : SW_HIDE);
    }
}

static bool IsComboBox(HWND h) {
    if (!h) return false;

    char cls[32]{};
    GetClassNameA(h, cls, sizeof(cls));
    return _stricmp(cls, "ComboBox") == 0;
}

static std::string GetText(int id) {
    HWND h = GetDlgItem(gMain, id);
    if (!h) return "";

    int len = GetWindowTextLengthA(h);
    std::string s((size_t)len, '\0');
    GetWindowTextA(h, s.data(), len + 1);

    if (s.empty() && IsComboBox(h)) {
        int idx = (int)SendMessageA(h, CB_GETCURSEL, 0, 0);
        if (idx != CB_ERR) {
            int itemLen = (int)SendMessageA(h, CB_GETLBTEXTLEN, idx, 0);
            if (itemLen > 0) {
                std::string item((size_t)itemLen, '\0');
                SendMessageA(h, CB_GETLBTEXT, idx, (LPARAM)item.data());
                return item;
            }
        }
    }

    return s;
}

static void SetText(int id, const std::string& value) {
    HWND h = GetDlgItem(gMain, id);
    if (!h) return;

    if (IsComboBox(h)) {
        if (value.empty()) {
            SendMessageA(h, CB_SETCURSEL, (WPARAM)-1, 0);
            SetWindowTextA(h, "");
            return;
        }

        int idx = (int)SendMessageA(h, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)value.c_str());
        if (idx != CB_ERR) {
            SendMessageA(h, CB_SETCURSEL, idx, 0);
            return;
        }
    }

    SetWindowTextA(h, value.c_str());
}

static int GetInt(int id) {
    try {
        return std::stoi(GetText(id));
    }
    catch (...) {
        return 0;
    }
}


static std::string TrimCopy(const std::string& value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static bool IsBlankString(const std::string& value) {
    return TrimCopy(value).empty();
}

static std::string ToLowerCopy(std::string value) {
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = (char)std::tolower((unsigned char)value[i]);
    }
    return value;
}

static bool TryParseInt(const std::string& text, int& value) {
    try {
        std::string trimmed = TrimCopy(text);
        if (trimmed.empty()) return false;

        size_t used = 0;
        int parsed = std::stoi(trimmed, &used);
        if (used != trimmed.size()) return false;

        value = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

static bool IsValidTimeHHMM(const std::string& value) {
    if (value.size() != 5 || value[2] != ':') return false;

    if (!std::isdigit((unsigned char)value[0]) ||
        !std::isdigit((unsigned char)value[1]) ||
        !std::isdigit((unsigned char)value[3]) ||
        !std::isdigit((unsigned char)value[4])) {
        return false;
    }

    int hour = (value[0] - '0') * 10 + (value[1] - '0');
    int minute = (value[3] - '0') * 10 + (value[4] - '0');

    return hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59;
}

static int TimeToMinutes(const std::string& value) {
    return ((value[0] - '0') * 10 + (value[1] - '0')) * 60 +
           ((value[3] - '0') * 10 + (value[4] - '0'));
}

static bool ValidateCourseForm() {
    if (IsBlankString(GetText(IDC_COURSE_CODE)) ||
        IsBlankString(GetText(IDC_COURSE_SECTION)) ||
        IsBlankString(GetText(IDC_COURSE_TITLE)) ||
        IsBlankString(GetText(IDC_COURSE_INST)) ||
        IsBlankString(GetText(IDC_COURSE_TYPE))) {
        MessageBoxA(gMain, "Course Code, Section, Course Name, Instructor and Category are required.", "Course", MB_OK | MB_ICONWARNING);
        return false;
    }

    int units = 0;
    if (!TryParseInt(GetText(IDC_COURSE_UNITS), units) || units <= 0 || units > 10) {
        MessageBoxA(gMain, "Units must be an integer between 1 and 10.", "Course", MB_OK | MB_ICONWARNING);
        return false;
    }

    return true;
}

static bool ValidateScheduleForm(bool requireScheduleId) {
    if (requireScheduleId && IsBlankString(GetText(IDC_SCHED_ID))) {
        MessageBoxA(gMain, "Please double-click one row in Schedule Results first.", "Schedule", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (IsBlankString(GetText(IDC_SCHED_CODE)) ||
        IsBlankString(GetText(IDC_SCHED_SECTION)) ||
        IsBlankString(GetText(IDC_SCHED_ROOM))) {
        MessageBoxA(gMain, "Course Code, Section and Classroom are required.", "Schedule", MB_OK | MB_ICONWARNING);
        return false;
    }

    int day = 0;
    if (!TryParseInt(GetText(IDC_SCHED_DAY), day) || day < 1 || day > 7) {
        MessageBoxA(gMain, "Day of Week must be an integer between 1 and 7.", "Schedule", MB_OK | MB_ICONWARNING);
        return false;
    }

    std::string startTime = GetText(IDC_SCHED_START);
    std::string endTime = GetText(IDC_SCHED_END);

    if (!IsValidTimeHHMM(startTime) || !IsValidTimeHHMM(endTime)) {
        MessageBoxA(gMain, "Start Time and End Time must use HH:MM format, for example 09:00.", "Schedule", MB_OK | MB_ICONWARNING);
        return false;
    }

    if (TimeToMinutes(startTime) >= TimeToMinutes(endTime)) {
        MessageBoxA(gMain, "Start Time must be earlier than End Time.", "Schedule", MB_OK | MB_ICONWARNING);
        return false;
    }

    return true;
}

static void SetStatus(const std::string& value) {
    HWND h = GetDlgItem(gMain, IDC_STATUS);
    SetWindowTextA(h, "");
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    SetWindowTextA(h, value.c_str());
    RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
}

static void EnableConnectedControls(bool enabled) {
    // Connection only allows Login. All data/view/edit buttons require a successful login.
    int viewIds[] = {
        IDC_DASH_LIST, IDC_DASH_SEMESTER_BTN, IDC_DASH_SEARCH_CODE_BTN, IDC_DASH_SEARCH_INST_BTN, IDC_DASH_SEARCH_CAT_BTN,
        IDC_COURSE_DETAIL,
        IDC_SCHED_GET,
        IDC_RAW_SEND, IDC_RAW_SAMPLE1, IDC_RAW_SAMPLE2
    };

    for (int id : viewIds) {
        EnableWindow(GetDlgItem(gMain, id), enabled && gLoggedIn);
    }

    // Write operations require successful admin login.
    bool canAdminWrite = enabled && gLoggedIn && gIsAdmin;
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_ADD), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_UPDATE), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_DELETE), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_RELOAD), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_SCHED_ADD), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_SCHED_UPDATE), canAdminWrite);
    EnableWindow(GetDlgItem(gMain, IDC_SCHED_DELETE), canAdminWrite);


    EnableWindow(GetDlgItem(gMain, IDC_LOGIN), enabled);
    EnableWindow(GetDlgItem(gMain, IDC_CONNECT), !enabled);
    EnableWindow(GetDlgItem(gMain, IDC_DISCONNECT), enabled);
}

// ------------------------------- ListView helpers -------------------------------

static void AddColumn(HWND list, int index, const char* title, int width) {
    LVCOLUMNA col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.pszText = (LPSTR)title;
    col.cx = width;
    col.iSubItem = index;
    SendMessageA(list, LVM_INSERTCOLUMNA, index, (LPARAM)&col);
}

static void SetupCourseTable(HWND list) {
    AddColumn(list, 0, "Code", 85);
    AddColumn(list, 1, "Section", 65);
    AddColumn(list, 2, "Course / Day", 235);
    AddColumn(list, 3, "Instructor / Time", 155);
    AddColumn(list, 4, "Units", 55);
    AddColumn(list, 5, "Type / Room", 100);
}


static void SetupScheduleTable(HWND list) {
    AddColumn(list, 0, "ID", 0);
    AddColumn(list, 1, "Code", 80);
    AddColumn(list, 2, "Section", 60);
    AddColumn(list, 3, "Day", 55);
    AddColumn(list, 4, "Start", 70);
    AddColumn(list, 5, "End", 70);
    AddColumn(list, 6, "Room", 100);
}


static void SetupLogTable(HWND list) {
    AddColumn(list, 0, "#", 42);
    AddColumn(list, 1, "Command", 130);
    AddColumn(list, 2, "Status", 80);
    AddColumn(list, 3, "Message", 300);
    AddColumn(list, 4, "Request", 260);
}

static void SetupInfoTable(HWND list) {
    AddColumn(list, 0, "Item", 160);
    AddColumn(list, 1, "Value", 400);
}

static void ClearList(HWND list) {
    SendMessageA(list, LVM_DELETEALLITEMS, 0, 0);
}

static void AddRow(HWND list, const std::vector<std::string>& cells) {
    int row = (int)SendMessageA(list, LVM_GETITEMCOUNT, 0, 0);

    LVITEMA item{};
    item.mask = LVIF_TEXT;
    item.iItem = row;
    item.iSubItem = 0;
    item.pszText = (LPSTR)(cells.empty() ? "" : cells[0].c_str());
    SendMessageA(list, LVM_INSERTITEMA, 0, (LPARAM)&item);

    for (int i = 1; i < (int)cells.size(); ++i) {
        LVITEMA sub{};
        sub.iSubItem = i;
        sub.pszText = (LPSTR)cells[i].c_str();
        SendMessageA(list, LVM_SETITEMTEXTA, (WPARAM)row, (LPARAM)&sub);
    }
}

static std::string ShortText(const std::string& text, size_t maxLen) {
    std::string out;
    for (char c : text) {
        if (c == '\r' || c == '\n' || c == '\t') {
            out += ' ';
        }
        else {
            out += c;
        }
    }

    if (out.size() > maxLen) {
        out = out.substr(0, maxLen) + "...";
    }

    return out;
}

static HWND ActiveLogTable() {
    if (gPage == PAGE_DASHBOARD) return GetDlgItem(gMain, IDC_DASH_LOG);
    if (gPage == PAGE_COURSES) return GetDlgItem(gMain, IDC_COURSE_LOG);
    if (gPage == PAGE_SCHEDULE) return GetDlgItem(gMain, IDC_SCHED_LOG);
    return GetDlgItem(gMain, IDC_RAW_LOG);
}

static HWND ActiveCourseTable() {
    if (gPage == PAGE_COURSES) return GetDlgItem(gMain, IDC_COURSE_TABLE);
    if (gPage == PAGE_SCHEDULE) return GetDlgItem(gMain, IDC_SCHED_TABLE);
    return GetDlgItem(gMain, IDC_DASH_TABLE);
}

static void AddLog(const std::string& command, const std::string& status, const std::string& message, const std::string& request) {
    HWND log = ActiveLogTable();
    if (!log) return;

    int count = (int)SendMessageA(log, LVM_GETITEMCOUNT, 0, 0) + 1;

    AddRow(log, {
        std::to_string(count),
        command,
        status,
        ShortText(message, 100),
        ShortText(request, 100)
        });
}

static void SetInfo(HWND info, const std::vector<std::pair<std::string, std::string>>& rows) {
    ClearList(info);
    for (const auto& r : rows) {
        AddRow(info, { r.first, r.second });
    }
}

// ------------------------------- JSON helpers -------------------------------

static std::string JsonGet(const json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) return "";
    if (obj[key].is_string()) return obj[key].get<std::string>();
    if (obj[key].is_number_integer()) return std::to_string(obj[key].get<int>());
    if (obj[key].is_number_unsigned()) return std::to_string(obj[key].get<unsigned int>());
    if (obj[key].is_number_float()) {
        std::ostringstream out;
        out << obj[key].get<double>();
        return out.str();
    }
    return obj[key].dump();
}

static json MakeListRequest() {
    json req;
    req["command"] = "LIST_COURSES";
    return req;
}

static json MakeListBySemesterRequest(const std::string& semester) {
    json req;
    req["command"] = "LIST_COURSES_BY_SEMESTER";
    req["data"] = {
        {"semester", semester}
    };
    return req;
}

static json MakeSearchByCodeRequest(const std::string& code) {
    json req;
    req["command"] = "SEARCH_COURSES_BY_CODE";
    req["data"] = {
        {"course_code", code}
    };
    return req;
}

static json MakeSearchByInstructorRequest(const std::string& instructor) {
    json req;
    req["command"] = "SEARCH_COURSES_BY_INSTRUCTOR";
    req["data"] = {
        {"instructor", instructor}
    };
    return req;
}


static json MakeSearchByCategoryRequest(const std::string& category) {
    json req;
    req["command"] = "SEARCH_COURSES_BY_CATEGORY";
    req["data"] = {
        {"course_category", category}
    };
    return req;
}

static json MakeGetCourseRequest(const std::string& code, const std::string& section) {
    json req;
    req["command"] = "GET_COURSE";
    req["data"] = {
        {"course_code", code},
        {"section", section}
    };
    return req;
}

static json MakeGetScheduleRequest(const std::string& code, const std::string& section) {
    json req;
    req["command"] = "GET_SCHEDULE";
    req["data"] = {
        {"course_code", code},
        {"section", section}
    };
    return req;
}

static json MakeAddScheduleRequest() {
    json req;
    req["command"] = "ADD_SCHEDULE";
    req["data"] = {
        {"course_code", GetText(IDC_SCHED_CODE)},
        {"section", GetText(IDC_SCHED_SECTION)},
        {"day_of_week", GetInt(IDC_SCHED_DAY)},
        {"start_time", GetText(IDC_SCHED_START)},
        {"end_time", GetText(IDC_SCHED_END)},
        {"classroom", GetText(IDC_SCHED_ROOM)}
    };
    return req;
}

static json MakeUpdateScheduleRequest() {
    json req;
    req["command"] = "UPDATE_SCHEDULE";
    req["data"] = {
        {"schedule_id", GetInt(IDC_SCHED_ID)},
        {"day_of_week", GetInt(IDC_SCHED_DAY)},
        {"start_time", GetText(IDC_SCHED_START)},
        {"end_time", GetText(IDC_SCHED_END)},
        {"classroom", GetText(IDC_SCHED_ROOM)}
    };
    return req;
}

static json MakeDeleteScheduleRequest() {
    json req;
    req["command"] = "DELETE_SCHEDULE";
    req["data"] = {
        {"schedule_id", GetInt(IDC_SCHED_ID)}
    };
    return req;
}


static json MakeAddCourseRequest() {
    json req;
    req["command"] = "ADD_COURSE";
    req["data"] = {
        {"course_code", GetText(IDC_COURSE_CODE)},
        {"course_title", GetText(IDC_COURSE_TITLE)},
        {"section", GetText(IDC_COURSE_SECTION)},
        {"instructor", GetText(IDC_COURSE_INST)},
        {"units", GetInt(IDC_COURSE_UNITS)},
        {"course_category", GetText(IDC_COURSE_TYPE)},
        {"description", GetText(IDC_COURSE_DESC)}
    };
    return req;
}

static json MakeUpdateCourseRequest() {
    json req;
    req["command"] = "UPDATE_COURSE";
    req["data"] = {
        {"course_code", GetText(IDC_COURSE_CODE)},
        {"section", GetText(IDC_COURSE_SECTION)},
        {"course_title", GetText(IDC_COURSE_TITLE)},
        {"instructor", GetText(IDC_COURSE_INST)},
        {"units", GetInt(IDC_COURSE_UNITS)},
        {"course_category", GetText(IDC_COURSE_TYPE)},
        {"description", GetText(IDC_COURSE_DESC)}
    };
    return req;
}

static json MakeDeleteRequest() {
    json req;
    req["command"] = "DELETE_COURSE";
    req["data"] = {
        {"course_code", GetText(IDC_COURSE_CODE)},
        {"section", GetText(IDC_COURSE_SECTION)}
    };
    return req;
}

static json MakeReloadRequest() {
    json req;
    req["command"] = "RELOAD_DATA";
    return req;
}

static json MakeLoginRequest() {
    json req;
    req["command"] = "LOGIN";
    req["data"] = {
        {"name", GetText(IDC_ACCOUNT)},
        {"password", GetText(IDC_PASSWORD)}
    };
    return req;
}

// ------------------------------- Socket -------------------------------

static std::string SendRequest(const json& req) {
    if (!gConnected || gSock == INVALID_SOCKET) {
        return R"({"status":"ERROR","message":"Not connected to server"})";
    }

    std::string payload = req.dump();

    if (!sendSecureString(gSock, payload)) {
        gConnected = false;
        gLoggedIn = false;
        gIsAdmin = false;
        SetStatus("Secure send failed");
        EnableConnectedControls(false);
        return R"({"status":"ERROR","message":"secure send failed"})";
    }

    std::string response;
    if (!recvSecureString(gSock, response)) {
        gConnected = false;
        gLoggedIn = false;
        gIsAdmin = false;
        SetStatus("Secure receive failed");
        EnableConnectedControls(false);
        return R"({"status":"ERROR","message":"secure receive failed"})";
    }

    return response;
}

static void DisconnectServer() {
    if (gSock != INVALID_SOCKET) {
        if (gConnected) {
            json req;
            req["command"] = "QUIT";
            std::string msg = req.dump();
            sendSecureString(gSock, msg);
        }

        closesocket(gSock);
        gSock = INVALID_SOCKET;
    }

    WSACleanup();
    gConnected = false;
    gLoggedIn = false;
    gIsAdmin = false;
    SetStatus("Disconnected");
    EnableConnectedControls(false);
}

static void ConnectServer() {
    if (gConnected) return;

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SetStatus("WSA failed");
        AddLog("CONNECT", "ERROR", "WSAStartup failed", "-");
        return;
    }

    gSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gSock == INVALID_SOCKET) {
        SetStatus("Socket failed");
        AddLog("CONNECT", "ERROR", "socket() failed", "-");
        WSACleanup();
        return;
    }

    int port = 50000;
    try {
        port = std::stoi(GetText(IDC_PORT));
    }
    catch (...) {
        port = 50000;
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons((u_short)port);

    std::string host = GetText(IDC_HOST);
    if (host == "localhost") {
        server.sin_addr.s_addr = inet_addr("127.0.0.1");
    }
    else if (inet_pton(AF_INET, host.c_str(), &server.sin_addr) <= 0) {
        SetStatus("Invalid host");
        AddLog("CONNECT", "ERROR", "Invalid host", host);
        closesocket(gSock);
        gSock = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    if (connect(gSock, (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
        SetStatus("Connect failed");
        AddLog("CONNECT", "ERROR", "Start CourseServer first", host + ":" + std::to_string(port));
        closesocket(gSock);
        gSock = INVALID_SOCKET;
        WSACleanup();
        return;
    }

    gConnected = true;
    gLoggedIn = false;
    gIsAdmin = false;
    SetStatus("Connected");
    EnableConnectedControls(true);
    AddLog("CONNECT", "OK", "Connected to CourseServer", host + ":" + std::to_string(port));
}

// ------------------------------- Rendering -------------------------------

static void RenderCourseRows(HWND table, const std::vector<CourseRow>& rows) {
    ClearList(table);

    for (const auto& r : rows) {
        AddRow(table, { r.code, r.section, r.title, r.instructor, r.units, r.category });
    }
}

static void ParseCourses(const std::string& response, HWND table) {
    gCachedCourses.clear();

    try {
        json j = json::parse(response);

        if (!j.contains("courses") || !j["courses"].is_array()) {
            AddLog("PARSE", "ERROR", "No courses array returned", "-");
            return;
        }

        for (const auto& c : j["courses"]) {
            CourseRow r;
            r.code = JsonGet(c, "course_code");
            r.section = JsonGet(c, "section");
            r.title = JsonGet(c, "course_title");
            r.instructor = JsonGet(c, "instructor");
            r.units = JsonGet(c, "units");
            r.category = JsonGet(c, "course_category");
            gCachedCourses.push_back(r);
        }

        RenderCourseRows(table, gCachedCourses);

        if (gPage == PAGE_DASHBOARD) {
            SetInfo(GetDlgItem(gMain, IDC_DASH_LOG), {});
        }
    }
    catch (...) {
        AddLog("PARSE", "ERROR", "Could not parse course list", "-");
    }
}

static void ParseCourseDetail(const std::string& response, HWND table) {
    ClearList(table);

    try {
        json j = json::parse(response);

        if (!j.contains("course") || !j["course"].is_object()) {
            AddLog("PARSE", "ERROR", "No course object returned", "-");
            return;
        }

        const json& c = j["course"];

        CourseRow r;
        r.code = JsonGet(c, "course_code");
        r.section = JsonGet(c, "section");
        r.title = JsonGet(c, "course_title");
        r.instructor = JsonGet(c, "instructor");
        r.units = JsonGet(c, "units");
        r.category = JsonGet(c, "course_category");

        AddRow(table, { r.code, r.section, r.title, r.instructor, r.units, r.category });

        SetText(IDC_COURSE_CODE, r.code);
        SetText(IDC_COURSE_SECTION, r.section);
        SetText(IDC_COURSE_TITLE, r.title);
        SetText(IDC_COURSE_INST, r.instructor);
        SetText(IDC_COURSE_UNITS, r.units);
        SetText(IDC_COURSE_TYPE, r.category);
        SetText(IDC_COURSE_DESC, JsonGet(c, "description"));
    }
    catch (...) {
        AddLog("PARSE", "ERROR", "Could not parse course detail", "-");
    }
}

static void ParseSchedule(const std::string& response, HWND table, const std::string& code, const std::string& section) {
    ClearList(table);

    try {
        json j = json::parse(response);

        if (!j.contains("schedule") || !j["schedule"].is_array()) {
            AddLog("PARSE", "ERROR", "No schedule array returned", "-");
            return;
        }

        for (const auto& s : j["schedule"]) {
            AddRow(table, {
                JsonGet(s, "schedule_id"),
                code,
                section,
                JsonGet(s, "day_of_week"),
                JsonGet(s, "start_time"),
                JsonGet(s, "end_time"),
                JsonGet(s, "classroom")
                });
        }
    }
    catch (...) {
        AddLog("PARSE", "ERROR", "Could not parse schedule", "-");
    }
}


static void LogResponse(const json& req, const std::string& response) {
    std::string status = "-";
    std::string message = "response received";

    try {
        json r = json::parse(response);
        status = JsonGet(r, "status");

        if (!JsonGet(r, "message").empty()) {
            message = JsonGet(r, "message");
        }
        else if (r.contains("courses")) {
            message = "course list returned";
        }
        else if (r.contains("course")) {
            message = "course detail returned";
        }
        else if (r.contains("schedule")) {
            message = "schedule returned";
        }
    }
    catch (...) {
        status = "ERROR";
        message = "invalid response";
    }

    AddLog(req.value("command", "-"), status.empty() ? "-" : status, message, req.dump());
}

// ------------------------------- Actions -------------------------------

static void ListCourses(HWND table) {
    json req = MakeListRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourses(response, table);
}

static void ListCoursesBySemester(HWND table) {
    if (!RequireLogin()) return;

    std::string semester = GetText(IDC_DASH_SEMESTER_INPUT);

    if (semester.empty()) {
        MessageBoxA(gMain, "Please enter a semester.", "Semester", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeListBySemesterRequest(semester);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourses(response, table);
}

static void SearchCoursesByCode(HWND table) {
    if (!RequireLogin()) return;

    std::string code = GetText(IDC_DASH_CODE_SEARCH);
    if (code.empty()) {
        MessageBoxA(gMain, "Please enter a course code.", "Search", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeSearchByCodeRequest(code);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourses(response, table);
}

static void SearchCoursesByInstructor(HWND table) {
    if (!RequireLogin()) return;

    std::string instructor = GetText(IDC_DASH_INST_SEARCH);
    if (instructor.empty()) {
        MessageBoxA(gMain, "Please enter an instructor name.", "Search", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeSearchByInstructorRequest(instructor);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourses(response, table);
}


static void SearchCoursesByCategory(HWND table) {
    if (!RequireLogin()) return;

    std::string category = GetText(IDC_DASH_CATEGORY_SEARCH);
    if (category.empty()) {
        MessageBoxA(gMain, "Please enter a course category.", "Search", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeSearchByCategoryRequest(category);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourses(response, table);
}

static void GetDetailFromDashboard() {
    std::string code = GetText(IDC_DASH_CODE);
    std::string section = GetText(IDC_DASH_SECTION);

    json req = MakeGetCourseRequest(code, section);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourseDetail(response, GetDlgItem(gMain, IDC_DASH_TABLE));
}

static void GetDetailFromCoursesPage() {
    std::string code = GetText(IDC_COURSE_CODE);
    std::string section = GetText(IDC_COURSE_SECTION);

    json req = MakeGetCourseRequest(code, section);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseCourseDetail(response, GetDlgItem(gMain, IDC_COURSE_TABLE));
}

static void GetScheduleFromDashboard() {
    std::string code = GetText(IDC_DASH_CODE);
    std::string section = GetText(IDC_DASH_SECTION);

    json req = MakeGetScheduleRequest(code, section);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseSchedule(response, GetDlgItem(gMain, IDC_DASH_TABLE), code, section);
}

static void GetScheduleFromSchedulePage() {
    if (!RequireLogin()) return;
    std::string code = GetText(IDC_SCHED_CODE);
    std::string section = GetText(IDC_SCHED_SECTION);

    if (IsBlankString(code) || IsBlankString(section)) {
        MessageBoxA(gMain, "Please enter Course Code and Section first.", "Get Schedule", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeGetScheduleRequest(code, section);
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ParseSchedule(response, GetDlgItem(gMain, IDC_SCHED_TABLE), code, section);
}
static void AddScheduleFromSchedulePage() {
    if (!RequireAdmin()) return;
    if (!ValidateScheduleForm(false)) return;

    json req = MakeAddScheduleRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);

    if (!GetText(IDC_SCHED_CODE).empty() && !GetText(IDC_SCHED_SECTION).empty()) {
        GetScheduleFromSchedulePage();
    }
}

static void UpdateScheduleFromSchedulePage() {
    if (!RequireAdmin()) return;
    if (!ValidateScheduleForm(true)) return;

    json req = MakeUpdateScheduleRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);

    if (!GetText(IDC_SCHED_CODE).empty() && !GetText(IDC_SCHED_SECTION).empty()) {
        GetScheduleFromSchedulePage();
    }
}

static void DeleteScheduleFromSchedulePage() {
    if (!RequireAdmin()) return;

    if (IsBlankString(GetText(IDC_SCHED_ID))) {
        MessageBoxA(gMain, "Please double-click one row in Schedule Results first.", "Delete Schedule", MB_OK | MB_ICONWARNING);
        return;
    }

    if (MessageBoxA(gMain, "Delete the selected schedule record?", "Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    json req = MakeDeleteScheduleRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);

    if (!GetText(IDC_SCHED_CODE).empty() && !GetText(IDC_SCHED_SECTION).empty()) {
        GetScheduleFromSchedulePage();
    }
}

static bool RequireLogin() {
    if (!gLoggedIn) {
        MessageBoxA(gMain, "Please login with a valid account and password first.", "Login required", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}

static bool RequireAdmin() {
    if (!gIsAdmin) {
        MessageBoxA(gMain, "Administrator access required.", "Permission denied", MB_OK | MB_ICONWARNING);
        return false;
    }
    return true;
}


static void Login() {
    if (!gConnected || gSock == INVALID_SOCKET) {
        MessageBoxA(gMain, "Please connect to server first.", "Login", MB_OK | MB_ICONWARNING);
        return;
    }

    json req = MakeLoginRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);

    try {
        json r = json::parse(response);
        std::string status = JsonGet(r, "status");
        std::string type = JsonGet(r, "type");
        std::string message = JsonGet(r, "message");

        if (status == "OK") {
            gLoggedIn = true;
            gIsAdmin = (type == "admin");

            if (gIsAdmin) {
                SetStatus("Admin logged in");
                MessageBoxA(gMain, "Admin login successful. All admin functions are enabled.", "Login", MB_OK | MB_ICONINFORMATION);
            }
            else {
                SetStatus("User logged in");
                MessageBoxA(gMain, "Login successful. View functions are enabled. Course Editor requires admin.", "Login", MB_OK | MB_ICONINFORMATION);
            }

            EnableConnectedControls(true);
        }
        else {
            gLoggedIn = false;
            gIsAdmin = false;
            EnableConnectedControls(true);
            MessageBoxA(gMain, message.empty() ? "Login failed. Check account or password." : message.c_str(), "Login failed", MB_OK | MB_ICONERROR);
        }
    }
    catch (...) {
        gLoggedIn = false;
        gIsAdmin = false;
        EnableConnectedControls(true);
        MessageBoxA(gMain, "Login response parse failed.", "Login failed", MB_OK | MB_ICONERROR);
    }
}

static void AddCourse() {
    if (!RequireAdmin()) return;
    if (!ValidateCourseForm()) return;

    json req = MakeAddCourseRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ListCourses(GetDlgItem(gMain, IDC_COURSE_TABLE));
}

static void UpdateCourse() {
    if (!RequireAdmin()) return;
    if (!ValidateCourseForm()) return;

    json req = MakeUpdateCourseRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ListCourses(GetDlgItem(gMain, IDC_COURSE_TABLE));
}

static void DeleteCourse() {
    if (!RequireAdmin()) return;

    if (IsBlankString(GetText(IDC_COURSE_CODE)) || IsBlankString(GetText(IDC_COURSE_SECTION))) {
        MessageBoxA(gMain, "Course Code and Section are required to delete a course.", "Delete Course", MB_OK | MB_ICONWARNING);
        return;
    }

    if (MessageBoxA(gMain, "Delete this course and its schedule records?", "Confirm Delete", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    json req = MakeDeleteRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ListCourses(GetDlgItem(gMain, IDC_COURSE_TABLE));
}

static void ReloadData() {
    if (!RequireAdmin()) return;

    if (MessageBoxA(gMain, "Reload Data will reset courses, schedules and users to the initial sample data. Continue?", "Confirm Reload Data", MB_YESNO | MB_ICONWARNING) != IDYES) {
        return;
    }

    json req = MakeReloadRequest();
    std::string response = SendRequest(req);
    LogResponse(req, response);
    ListCourses(GetDlgItem(gMain, IDC_COURSE_TABLE));
}

static void RawSend() {
    std::string raw = GetText(IDC_RAW_EDIT);

    try {
        json req = json::parse(raw);
        std::string response = SendRequest(req);
        LogResponse(req, response);
    }
    catch (const std::exception& e) {
        AddLog("RAW", "ERROR", e.what(), raw);
    }
}

static void ClearCourseForm() {
    SetText(IDC_COURSE_CODE, "");
    SetText(IDC_COURSE_SECTION, "");
    SetText(IDC_COURSE_TITLE, "");
    SetText(IDC_COURSE_INST, "");
    SetText(IDC_COURSE_UNITS, "");
    SetText(IDC_COURSE_TYPE, "");
    SetText(IDC_COURSE_DESC, "");
}

static void FillSample() {
    SetText(IDC_COURSE_CODE, "COMP3003");
    SetText(IDC_COURSE_SECTION, "1003");
    SetText(IDC_COURSE_TITLE, "Data Communications and Networking");
    SetText(IDC_COURSE_INST, "New Instructor");
    SetText(IDC_COURSE_UNITS, "3");
    SetText(IDC_COURSE_TYPE, "MR");
    SetText(IDC_COURSE_DESC, "Sample data for GUI testing.");
}

static void FillScheduleSample() {
    SetText(IDC_SCHED_CODE, "COMP3003");
    SetText(IDC_SCHED_SECTION, "1003");
    SetText(IDC_SCHED_ID, "");
    SetText(IDC_SCHED_DAY, "2");
    SetText(IDC_SCHED_START, "18:00");
    SetText(IDC_SCHED_END, "19:00");
    SetText(IDC_SCHED_ROOM, "T7-999");
}


static void FilterDashboardCourses() {
    // Case-sensitive search:
    // "comp3033" will NOT match "COMP3033".
    // Users must type the same uppercase/lowercase letters as the data.
    std::string q = ToLowerCopy(GetText(IDC_DASH_SEARCH));

    std::vector<CourseRow> filtered;

    for (const auto& r : gCachedCourses) {
        std::string target = ToLowerCopy(r.code + " " + r.section + " " + r.title + " " + r.instructor + " " + r.category);

        if (q.empty() || target.find(q) != std::string::npos) {
            filtered.push_back(r);
        }
    }

    RenderCourseRows(GetDlgItem(gMain, IDC_DASH_TABLE), filtered);
}

// ------------------------------- Drawing -------------------------------

static void DrawTextSimple(HDC hdc, const char* text, RECT rc, HFONT font, COLORREF color, UINT format) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HGDIOBJ oldFont = SelectObject(hdc, font);
    DrawTextA(hdc, text, -1, &rc, format);
    SelectObject(hdc, oldFont);
}

static void DrawRound(HDC hdc, RECT rc, COLORREF fill) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);

    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 16, 16);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);

    DeleteObject(brush);
    DeleteObject(pen);
}

static HBRUSH UiBgBrush() {
    if (!gBgBrush) {
        gBgBrush = CreateSolidBrush(C_BG);
    }
    return gBgBrush;
}

static bool IsGroupBoxControl(HWND hwnd) {
    if (!hwnd) return false;

    char cls[32]{};
    GetClassNameA(hwnd, cls, sizeof(cls));

    LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    return lstrcmpiA(cls, "Button") == 0 && ((style & BS_GROUPBOX) == BS_GROUPBOX);
}

static LRESULT PaintCleanControlText(WPARAM wParam) {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, C_TEXT);
    return (LRESULT)UiBgBrush();
}

static const char* PageTitle() {
    if (gPage == PAGE_COURSES) return "Course Management";
    if (gPage == PAGE_SCHEDULE) return "Course Schedule Lookup";
    if (gPage == PAGE_JSON) return "Protocol Test";
    return "Course Catalog";
}

static const char* PageSubtitle() {
    if (gPage == PAGE_COURSES) return "Manage course records: add, update, delete, and reload database data.";
    if (gPage == PAGE_SCHEDULE) return "Search timetable information by course code and section.";
    if (gPage == PAGE_JSON) return "Manually test JSON protocol requests sent to the socket server.";
    return "Overview page for listing and filtering courses. Detail and schedule are separated into their own pages.";
}

static void PaintMain(HDC hdc, RECT rc) {
    HBRUSH bg = CreateSolidBrush(C_BG);
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    RECT side{ 0, 0, SIDEBAR_W, rc.bottom };
    HBRUSH sb = CreateSolidBrush(C_SIDEBAR);
    FillRect(hdc, &side, sb);
    DeleteObject(sb);

    RECT brand{ 18, 30, SIDEBAR_W - 18, 95 };
    DrawRound(hdc, brand, RGB(30, 41, 59));

    RECT b1{ 38, 42, SIDEBAR_W - 25, 64 };
    DrawTextSimple(hdc, "CourseSystem", b1, gFontBold, RGB(255, 255, 255), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT b2{ 38, 66, SIDEBAR_W - 25, 88 };
    DrawTextSimple(hdc, "Timetable Management", b2, gFont, RGB(203, 213, 225), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    struct Nav { Page page; int y; const char* text; };
    Nav navs[] = {
        { PAGE_DASHBOARD, 140, "Course Catalog" },
        { PAGE_COURSES,   200, "Course Management" },
        { PAGE_SCHEDULE,  260, "Course Schedule Lookup" },
        { PAGE_JSON,      320, "Protocol Test" }
    };

    for (auto& nav : navs) {
        RECT item{ 18, nav.y, SIDEBAR_W - 18, nav.y + 42 };

        if (gPage == nav.page) {
            DrawRound(hdc, item, C_BLUE);
            DrawTextSimple(hdc, nav.text, item, gFontBold, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        else {
            RECT text{ 38, nav.y, SIDEBAR_W - 20, nav.y + 42 };
            DrawTextSimple(hdc, nav.text, text, gFont, RGB(203, 213, 225), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    RECT footer{ 18, rc.bottom - 90, SIDEBAR_W - 18, rc.bottom - 25 };
    DrawRound(hdc, footer, RGB(2, 6, 23));
    RECT ft1{ 38, rc.bottom - 80, SIDEBAR_W - 25, rc.bottom - 56 };
    DrawTextSimple(hdc, "Client-Server Demo", ft1, gFont, RGB(255, 255, 255), DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    RECT ft2{ 38, rc.bottom - 56, SIDEBAR_W - 25, rc.bottom - 32 };
    DrawTextSimple(hdc, "SQLite + Socket + JSON", ft2, gFont, RGB(148, 163, 184), DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT title{ MAIN_X, 30, MAIN_X + 640, 65 };
    DrawTextSimple(hdc, PageTitle(), title, gFontTitle, C_TEXT, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    RECT sub{ MAIN_X, 70, MAIN_X + 850, 95 };
    DrawTextSimple(hdc, PageSubtitle(), sub, gFont, C_MUTED, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
}


static std::string GetListViewText(HWND list, int row, int col) {
    char buf[512]{};
    LVITEMA item{};
    item.iSubItem = col;
    item.pszText = buf;
    item.cchTextMax = sizeof(buf);
    SendMessageA(list, LVM_GETITEMTEXTA, (WPARAM)row, (LPARAM)&item);
    return std::string(buf);
}

static void FillCourseFormFromRow(HWND list, int row) {
    if (!list || row < 0) return;
    SetText(IDC_COURSE_CODE, GetListViewText(list, row, 0));
    SetText(IDC_COURSE_SECTION, GetListViewText(list, row, 1));
    SetText(IDC_COURSE_TITLE, GetListViewText(list, row, 2));
    SetText(IDC_COURSE_INST, GetListViewText(list, row, 3));
    SetText(IDC_COURSE_UNITS, GetListViewText(list, row, 4));
    SetText(IDC_COURSE_TYPE, GetListViewText(list, row, 5));
}

static void FillScheduleFormFromRow(HWND list, int row) {
    if (!list || row < 0) return;
    SetText(IDC_SCHED_ID, GetListViewText(list, row, 0));
    SetText(IDC_SCHED_CODE, GetListViewText(list, row, 1));
    SetText(IDC_SCHED_SECTION, GetListViewText(list, row, 2));
    SetText(IDC_SCHED_DAY, GetListViewText(list, row, 3));
    SetText(IDC_SCHED_START, GetListViewText(list, row, 4));
    SetText(IDC_SCHED_END, GetListViewText(list, row, 5));
    SetText(IDC_SCHED_ROOM, GetListViewText(list, row, 6));
}

static void SetColumnWidth(HWND list, int col, int width) {
    if (list && width >= 0) {
        SendMessageA(list, LVM_SETCOLUMNWIDTH, (WPARAM)col, (LPARAM)width);
    }
}

static void ResizeCourseColumns(HWND list, int width) {
    if (!list) return;
    int available = width - 32;
    if (available < 560) available = 560;
    int code = 90;
    int section = 75;
    int instructor = 180;
    int units = 60;
    int category = 105;
    int title = available - code - section - instructor - units - category;
    if (title < 180) title = 180;
    SetColumnWidth(list, 0, code);
    SetColumnWidth(list, 1, section);
    SetColumnWidth(list, 2, title);
    SetColumnWidth(list, 3, instructor);
    SetColumnWidth(list, 4, units);
    SetColumnWidth(list, 5, category);
}

static void ResizeScheduleColumns(HWND list, int width) {
    if (!list) return;
    int available = width - 32;
    if (available < 430) available = 430;
    int code = 90;
    int section = 75;
    int day = 65;
    int start = 75;
    int end = 75;
    int room = available - code - section - day - start - end;
    if (room < 120) room = 120;
    SetColumnWidth(list, 0, 0);
    SetColumnWidth(list, 1, code);
    SetColumnWidth(list, 2, section);
    SetColumnWidth(list, 3, day);
    SetColumnWidth(list, 4, start);
    SetColumnWidth(list, 5, end);
    SetColumnWidth(list, 6, room);
}

static void ResizeLogColumns(HWND list, int width) {
    if (!list) return;
    int available = width - 32;
    if (available < 620) available = 620;
    SetColumnWidth(list, 0, 45);
    SetColumnWidth(list, 1, 150);
    SetColumnWidth(list, 2, 85);
    SetColumnWidth(list, 3, available / 2 - 100);
    SetColumnWidth(list, 4, available / 2 - 180);
}

static void Relayout(HWND hwnd) {
    if (!hwnd) return;

    RECT rc{};
    GetClientRect(hwnd, &rc);

    int clientW = rc.right - rc.left;
    int clientH = rc.bottom - rc.top;
    int mainW = clientW - MAIN_X - 30;
    if (mainW < 620) mainW = 620;
    int bottomMargin = 25;
    int logY = clientH - 140;
    if (logY < 645) logY = 645;
    int logGroupH = clientH - logY - bottomMargin;
    if (logGroupH < 95) logGroupH = 95;

    MoveWindow(GetDlgItem(hwnd, IDC_G_COMMON), MAIN_X, 110, mainW, 105, TRUE);

    {
        int topY = 235;
        int leftW = 420;
        int gap = 30;
        int rightX = MAIN_X + leftW + gap;
        int rightW = clientW - rightX - 30;
        if (rightW < 420) rightW = 420;
        int resultH = logY - topY - 25;
        if (resultH < 260) resultH = 260;
        MoveWindow(GetDlgItem(hwnd, IDC_G_DASH_CTRL), MAIN_X, topY, leftW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_DASH_RESULT), rightX, topY, rightW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DASH_TABLE), rightX + 25, topY + 40, rightW - 50, resultH - 80, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_DASH_LOG), MAIN_X, logY, mainW, logGroupH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DASH_LOG), MAIN_X + 25, logY + 35, mainW - 50, logGroupH - 55, TRUE);
        ResizeCourseColumns(GetDlgItem(hwnd, IDC_DASH_TABLE), rightW - 50);
        ResizeLogColumns(GetDlgItem(hwnd, IDC_DASH_LOG), mainW - 50);
    }

    {
        int topY = 235;
        int leftW = 420;
        int gap = 30;
        int rightX = MAIN_X + leftW + gap;
        int rightW = clientW - rightX - 30;
        if (rightW < 420) rightW = 420;
        int resultH = logY - topY - 25;
        if (resultH < 260) resultH = 260;
        MoveWindow(GetDlgItem(hwnd, IDC_G_COURSE_EDIT), MAIN_X, topY, leftW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_COURSE_REC), rightX, topY, rightW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_COURSE_TABLE), rightX + 25, topY + 40, rightW - 50, resultH - 80, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_COURSE_LOG), MAIN_X, logY, mainW, logGroupH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_COURSE_LOG), MAIN_X + 25, logY + 35, mainW - 50, logGroupH - 55, TRUE);
        ResizeCourseColumns(GetDlgItem(hwnd, IDC_COURSE_TABLE), rightW - 50);
        ResizeLogColumns(GetDlgItem(hwnd, IDC_COURSE_LOG), mainW - 50);
    }

    {
        int topY = 235;
        int leftW = 420;
        int gap = 30;
        int rightX = MAIN_X + leftW + gap;
        int rightW = clientW - rightX - 30;
        if (rightW < 420) rightW = 420;
        int resultH = logY - topY - 25;
        if (resultH < 260) resultH = 260;
        MoveWindow(GetDlgItem(hwnd, IDC_G_SCHED_EDIT), MAIN_X, topY, leftW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_SCHED_RESULT), rightX, topY, rightW, resultH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SCHED_TABLE), rightX + 25, topY + 40, rightW - 50, resultH - 80, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_SCHED_LOG), MAIN_X, logY, mainW, logGroupH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SCHED_LOG), MAIN_X + 25, logY + 35, mainW - 50, logGroupH - 55, TRUE);
        ResizeScheduleColumns(GetDlgItem(hwnd, IDC_SCHED_TABLE), rightW - 50);
        ResizeLogColumns(GetDlgItem(hwnd, IDC_SCHED_LOG), mainW - 50);
    }

    {
        int topY = 235;
        int rawH = 260;
        int rawLogY = topY + rawH + 25;
        int rawLogH = clientH - rawLogY - bottomMargin;
        if (rawLogH < 190) rawLogH = 190;
        MoveWindow(GetDlgItem(hwnd, IDC_G_RAW_TEST), MAIN_X, topY, mainW, rawH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_RAW_EDIT), MAIN_X + 30, topY + 70, mainW - 60, 90, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_G_RAW_LOG), MAIN_X, rawLogY, mainW, rawLogH, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_RAW_LOG), MAIN_X + 25, rawLogY + 35, mainW - 50, rawLogH - 65, TRUE);
        ResizeLogColumns(GetDlgItem(hwnd, IDC_RAW_LOG), mainW - 50);
    }

    InvalidateRect(hwnd, nullptr, TRUE);
}

// ------------------------------- Pages -------------------------------

static void ShowPage(Page page) {
    gPage = page;

    ShowGroup(gDashboard, page == PAGE_DASHBOARD);
    ShowGroup(gCourses, page == PAGE_COURSES);
    ShowGroup(gSchedule, page == PAGE_SCHEDULE);
    ShowGroup(gJson, page == PAGE_JSON);

    Relayout(gMain);
    InvalidateRect(gMain, nullptr, TRUE);
}

static void HandleNavClick(int x, int y) {
    if (x > SIDEBAR_W) return;

    if (y >= 140 && y <= 182) ShowPage(PAGE_DASHBOARD);
    else if (y >= 200 && y <= 242) ShowPage(PAGE_COURSES);
    else if (y >= 260 && y <= 302) ShowPage(PAGE_SCHEDULE);
    else if (y >= 320 && y <= 362) ShowPage(PAGE_JSON);
}

// ------------------------------- Layout creation -------------------------------

static void CreateCommon() {
    gBuild = &gCommon;

    GroupBox("Server Connection", MAIN_X, 110, 980, 105, IDC_G_COMMON);

    Label("Account", MAIN_X + 28, 143, 70, 20);
    Edit("", MAIN_X + 100, 137, 150, 28, IDC_ACCOUNT);

    Label("Password", MAIN_X + 280, 143, 80, 20);
    Edit("", MAIN_X + 365, 137, 150, 28, IDC_PASSWORD, false, false, true);
    Button("Login", MAIN_X + 530, 135, 90, 32, IDC_LOGIN);

    Label("Host", MAIN_X + 28, 179, 45, 20);
    Edit("127.0.0.1", MAIN_X + 75, 173, 140, 28, IDC_HOST);

    Label("Port", MAIN_X + 240, 179, 45, 20);
    Edit("50000", MAIN_X + 285, 173, 90, 28, IDC_PORT);

    Button("Connect", MAIN_X + 410, 171, 120, 32, IDC_CONNECT);
    Button("Disconnect", MAIN_X + 545, 171, 120, 32, IDC_DISCONNECT);

    Label("Status", MAIN_X + 700, 179, 55, 20);
    Edit("Disconnected", MAIN_X + 760, 173, 150, 28, IDC_STATUS, false, true);

    // Semester controls belong to the Dashboard page only.
    // Keeping them here creates duplicate controls on all pages and overlaps page content.

    gBuild = nullptr;
}

static void CreateDashboardPage() {
    gBuild = &gDashboard;

    // Fixed layout constants
    const int leftX = MAIN_X;
    const int topY = 235;
    const int leftW = 450;
    const int rowH = 80;

    const int rightX = MAIN_X + 480;
    const int rightW = 760;

    // Left panel
    GroupBox("Dashboard Controls", leftX, topY, leftW, 520, IDC_G_DASH_CTRL);

    Label("Local Filter", leftX + 30, topY + 45, 160, 24);
    Edit("", leftX + 30, topY + 75, 250, 32, IDC_DASH_SEARCH);
    Button("List All", leftX + 300, topY + 73, 120, 36, IDC_DASH_LIST);

    Label("Course Code", leftX + 30, topY + 45 + rowH, 160, 24);
    Edit("", leftX + 30, topY + 75 + rowH, 250, 32, IDC_DASH_CODE_SEARCH);
    Button("Search", leftX + 300, topY + 73 + rowH, 120, 36, IDC_DASH_SEARCH_CODE_BTN);

    Label("Instructor", leftX + 30, topY + 45 + rowH * 2, 160, 24);
    Edit("", leftX + 30, topY + 75 + rowH * 2, 250, 32, IDC_DASH_INST_SEARCH);
    Button("Search", leftX + 300, topY + 73 + rowH * 2, 120, 36, IDC_DASH_SEARCH_INST_BTN);

    Label("Category", leftX + 30, topY + 45 + rowH * 3, 160, 24);
    Combo("", leftX + 30, topY + 75 + rowH * 3, 250, 180, IDC_DASH_CATEGORY_SEARCH, CategoryOptions(), true);
    Button("Search", leftX + 300, topY + 73 + rowH * 3, 120, 36, IDC_DASH_SEARCH_CAT_BTN);

    Label("Semester", leftX + 30, topY + 45 + rowH * 4, 160, 24);
    Combo("2025-2026 S1", leftX + 30, topY + 75 + rowH * 4, 250, 160, IDC_DASH_SEMESTER_INPUT, SemesterOptions(), true);
    Button("View", leftX + 300, topY + 73 + rowH * 4, 120, 36, IDC_DASH_SEMESTER_BTN);

    Label(
        "Use the buttons above to query courses.",
        leftX + 30,
        topY + 470,
        370,
        24
    );

    // Right panel
    GroupBox("Course Results", rightX, topY, rightW, 520, IDC_G_DASH_RESULT);
    HWND table = ListViewBox(rightX + 25, topY + 40, rightW - 50, 440, IDC_DASH_TABLE);
    SetupCourseTable(table);

    // Bottom log
    GroupBox("Recent Request Log", MAIN_X, 780, 1240, 120, IDC_G_DASH_LOG);
    HWND log = ListViewBox(MAIN_X + 25, 815, 1190, 60, IDC_DASH_LOG);
    SetupLogTable(log);

    gBuild = nullptr;
}



static void CreateCoursesPage() {
    gBuild = &gCourses;

    const int leftX = MAIN_X;
    const int topY = 235;
    const int leftW = 450;
    const int rightX = MAIN_X + 480;
    const int rightW = 760;

    GroupBox("Course Editor", leftX, topY, leftW, 430, IDC_G_COURSE_EDIT);

    int x = leftX + 30;
    int y = topY + 40;

    Label("Course Code", x, y, 110, 20);
    Edit("", x, y + 24, 145, 28, IDC_COURSE_CODE);

    Label("Section", x + 170, y, 80, 20);
    Edit("", x + 170, y + 24, 85, 28, IDC_COURSE_SECTION);

    Label("Units", x + 280, y, 60, 20);
    Combo("3", x + 280, y + 24, 70, 180, IDC_COURSE_UNITS, UnitOptions(), false);

    y += 72;
    Label("Course Name", x, y, 120, 20);
    Edit("", x, y + 24, 380, 28, IDC_COURSE_TITLE);

    y += 72;
    Label("Instructor", x, y, 100, 20);
    Edit("", x, y + 24, 220, 28, IDC_COURSE_INST);

    Label("Category", x + 245, y, 90, 20);
    Combo("", x + 245, y + 24, 135, 200, IDC_COURSE_TYPE, CategoryOptions(), true);

    y += 72;
    Label("Description", x, y, 120, 20);
    Edit("", x, y + 24, 380, 52, IDC_COURSE_DESC, true);

    Button("Get Detail", x, topY + 345, 105, 32, IDC_COURSE_DETAIL);
    Button("Reload Data", x + 120, topY + 345, 115, 32, IDC_COURSE_RELOAD);
    Button("Clear", x + 250, topY + 345, 80, 32, IDC_COURSE_CLEAR);

    Button("Add Course", x, topY + 385, 105, 32, IDC_COURSE_ADD);
    Button("Update", x + 120, topY + 385, 95, 32, IDC_COURSE_UPDATE);
    Button("Delete", x + 230, topY + 385, 85, 32, IDC_COURSE_DELETE);
    Button("Sample", x + 330, topY + 385, 80, 32, IDC_COURSE_SAMPLE);

    GroupBox("Course Records", rightX, topY, rightW, 430, IDC_G_COURSE_REC);
    HWND table = ListViewBox(rightX + 25, topY + 40, rightW - 50, 325, IDC_COURSE_TABLE);
    SetupCourseTable(table);

    GroupBox("Course Operation Log", MAIN_X, 690, 1240, 120, IDC_G_COURSE_LOG);
    HWND log = ListViewBox(MAIN_X + 25, 725, 1190, 60, IDC_COURSE_LOG);
    SetupLogTable(log);

    gBuild = nullptr;
}

static void CreateSchedulePage() {
    gBuild = &gSchedule;

    const int leftX = MAIN_X;
    const int topY = 235;
    const int leftW = 450;
    const int rightX = MAIN_X + 480;
    const int rightW = 760;

    GroupBox("Schedule Management", leftX, topY, leftW, 430, IDC_G_SCHED_EDIT);

    int x = leftX + 30;
    int y = topY + 45;
    int labelW = 110;
    int editX = leftX + 150;

    Label("Course Code", x, y, labelW, 20);
    Edit("", editX, y - 6, 180, 28, IDC_SCHED_CODE);

    y += 40;
    Label("Section", x, y, labelW, 20);
    Edit("", editX, y - 6, 180, 28, IDC_SCHED_SECTION);

    HWND hiddenScheduleId = Edit("", 0, 0, 1, 1, IDC_SCHED_ID);
    ShowWindow(hiddenScheduleId, SW_HIDE);

    y += 40;
    Label("Day of Week", x, y, labelW, 20);
    Combo("", editX, y - 6, 180, 160, IDC_SCHED_DAY, DayOptions(), false);

    y += 40;
    Label("Start Time", x, y, labelW, 20);
    Combo("", editX, y - 6, 180, 220, IDC_SCHED_START, TimeOptions(), true);

    y += 40;
    Label("End Time", x, y, labelW, 20);
    Combo("", editX, y - 6, 180, 220, IDC_SCHED_END, TimeOptions(), true);

    y += 40;
    Label("Classroom", x, y, labelW, 20);
    Combo("", editX, y - 6, 180, 200, IDC_SCHED_ROOM, ClassroomOptions(), true);

    Button("Get", x, topY + 340, 95, 32, IDC_SCHED_GET);
    Button("Sample", x + 110, topY + 340, 95, 32, IDC_SCHED_SAMPLE);
    Button("Clear", x + 220, topY + 340, 95, 32, IDC_SCHED_CLEAR);

    Button("Add", x, topY + 380, 95, 32, IDC_SCHED_ADD);
    Button("Update", x + 110, topY + 380, 95, 32, IDC_SCHED_UPDATE);
    Button("Delete", x + 220, topY + 380, 95, 32, IDC_SCHED_DELETE);

    GroupBox("Schedule Results", rightX, topY, rightW, 430, IDC_G_SCHED_RESULT);
    HWND table = ListViewBox(rightX + 25, topY + 40, rightW - 50, 325, IDC_SCHED_TABLE);
    SetupScheduleTable(table);

    GroupBox("Schedule Request Log", MAIN_X, 690, 1240, 120, IDC_G_SCHED_LOG);
    HWND log = ListViewBox(MAIN_X + 25, 725, 1190, 60, IDC_SCHED_LOG);
    SetupLogTable(log);

    gBuild = nullptr;
}

static void CreateJsonPage() {
    gBuild = &gJson;

    const int leftX = MAIN_X;
    const int topY = 235;
    const int fullW = 1240;

    GroupBox("Raw JSON Protocol Tester", leftX, topY, fullW, 260, IDC_G_RAW_TEST);

    Label("Manual JSON Request", leftX + 30, topY + 45, 180, 20);
    Edit("{\"command\":\"LIST_COURSES\"}", leftX + 30, topY + 70, fullW - 60, 90, IDC_RAW_EDIT, true);

    Button("Send Raw JSON", leftX + 30, topY + 180, 140, 32, IDC_RAW_SEND);
    Button("Sample: LIST", leftX + 185, topY + 180, 125, 32, IDC_RAW_SAMPLE1);
    Button("Sample: GET", leftX + 325, topY + 180, 125, 32, IDC_RAW_SAMPLE2);

    Label("This page is for demonstrating the same JSON protocol used by the GUI buttons.", leftX + 30, topY + 220, 760, 22);

    GroupBox("Protocol Request / Response Log", leftX, 520, fullW, 290, IDC_G_RAW_LOG);
    HWND log = ListViewBox(leftX + 25, 555, fullW - 50, 225, IDC_RAW_LOG);
    SetupLogTable(log);

    gBuild = nullptr;
}

static void CreateLayout(HWND hwnd) {
    gMain = hwnd;

    gBgBrush = CreateSolidBrush(C_BG);

    gFont = CreateFontA(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );

    gFontBold = CreateFontA(
        -15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );

    gFontTitle = CreateFontA(
        -24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI"
    );

    gFontMono = CreateFontA(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_MODERN, "Consolas"
    );

    CreateCommon();
    CreateDashboardPage();
    CreateCoursesPage();
    CreateSchedulePage();
    CreateJsonPage();

    ShowPage(PAGE_DASHBOARD);
    EnableConnectedControls(false);

    // Buttons that are allowed before connection.
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_CLEAR), TRUE);
    EnableWindow(GetDlgItem(gMain, IDC_COURSE_SAMPLE), TRUE);
    EnableWindow(GetDlgItem(gMain, IDC_SCHED_SAMPLE), TRUE);
    EnableWindow(GetDlgItem(gMain, IDC_SCHED_CLEAR), TRUE);

    Relayout(hwnd);
}

// ------------------------------- Window Procedure -------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        CreateLayout(hwnd);
        return 0;

    case WM_SIZE:
        Relayout(hwnd);
        return 0;

    case WM_LBUTTONDOWN:
        HandleNavClick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_CTLCOLORSTATIC: {
        char cls[32]{};
        GetClassNameA((HWND)lParam, cls, sizeof(cls));

        // STATIC labels get the page background instead of the default gray block.
        // Read-only EDIT controls also send WM_CTLCOLORSTATIC, so leave them unchanged.
        if (lstrcmpiA(cls, "Static") == 0) {
            return PaintCleanControlText(wParam);
        }
        break;
    }

    case WM_CTLCOLORBTN:
        // Keep normal push buttons unchanged; only clean group-box caption background.
        if (IsGroupBoxControl((HWND)lParam)) {
            return PaintCleanControlText(wParam);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_CONNECT:
            ConnectServer();
            break;

        case IDC_DISCONNECT:
            DisconnectServer();
            break;

        case IDC_LOGIN:
            Login();
            break;

        case IDC_DASH_LIST:
            ListCourses(GetDlgItem(gMain, IDC_DASH_TABLE));
            break;

        case IDC_DASH_SEMESTER_BTN:
            ListCoursesBySemester(GetDlgItem(gMain, IDC_DASH_TABLE));
            break;

        case IDC_DASH_SEARCH_CODE_BTN:
            SearchCoursesByCode(GetDlgItem(gMain, IDC_DASH_TABLE));
            break;

        case IDC_DASH_SEARCH_INST_BTN:
            SearchCoursesByInstructor(GetDlgItem(gMain, IDC_DASH_TABLE));
            break;

        case IDC_DASH_SEARCH_CAT_BTN:
            SearchCoursesByCategory(GetDlgItem(gMain, IDC_DASH_TABLE));
            break;

        case IDC_DASH_SEARCH:
            if (HIWORD(wParam) == EN_CHANGE) {
                FilterDashboardCourses();
            }
            break;

        case IDC_COURSE_DETAIL:
            GetDetailFromCoursesPage();
            break;

        case IDC_COURSE_ADD:
            AddCourse();
            break;

        case IDC_COURSE_UPDATE:
            UpdateCourse();
            break;

        case IDC_COURSE_DELETE:
            DeleteCourse();
            break;

        case IDC_COURSE_RELOAD:
            ReloadData();
            break;

        case IDC_COURSE_SAMPLE:
            FillSample();
            break;

        case IDC_COURSE_CLEAR:
            ClearCourseForm();
            break;

        case IDC_SCHED_GET:
            GetScheduleFromSchedulePage();
            break;
        case IDC_SCHED_ADD:
            AddScheduleFromSchedulePage();
            break;

        case IDC_SCHED_UPDATE:
            UpdateScheduleFromSchedulePage();
            break;

        case IDC_SCHED_DELETE:
            DeleteScheduleFromSchedulePage();
            break;


        case IDC_SCHED_SAMPLE:
            FillScheduleSample();
            break;

        case IDC_SCHED_CLEAR:
            SetText(IDC_SCHED_CODE, "");
            SetText(IDC_SCHED_SECTION, "");
            SetText(IDC_SCHED_ID, "");
            SetText(IDC_SCHED_DAY, "");
            SetText(IDC_SCHED_START, "");
            SetText(IDC_SCHED_END, "");
            SetText(IDC_SCHED_ROOM, "");
            ClearList(GetDlgItem(gMain, IDC_SCHED_TABLE));
            break;


        case IDC_RAW_SEND:
            RawSend();
            break;

        case IDC_RAW_SAMPLE1:
            SetText(IDC_RAW_EDIT, "{\"command\":\"LIST_COURSES\"}");
            break;

        case IDC_RAW_SAMPLE2:
            SetText(IDC_RAW_EDIT, "{\"command\":\"GET_COURSE\",\"data\":{\"course_code\":\"COMP3003\",\"section\":\"1003\"}}");
            break;

        default:
            break;
        }
        return 0;

    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->code == NM_DBLCLK) {
            LPNMITEMACTIVATE item = (LPNMITEMACTIVATE)lParam;
            if (item->iItem >= 0 && item->hdr.idFrom == IDC_DASH_TABLE) {
                ShowPage(PAGE_COURSES);
                FillCourseFormFromRow(item->hdr.hwndFrom, item->iItem);
            }
            else if (item->iItem >= 0 && item->hdr.idFrom == IDC_COURSE_TABLE) {
                FillCourseFormFromRow(item->hdr.hwndFrom, item->iItem);
            }
            else if (item->iItem >= 0 && item->hdr.idFrom == IDC_SCHED_TABLE) {
                FillScheduleFormFromRow(item->hdr.hwndFrom, item->iItem);
            }
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc{};
        GetClientRect(hwnd, &rc);
        PaintMain(hdc, rc);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        DisconnectServer();

        if (gFont) DeleteObject(gFont);
        if (gFontBold) DeleteObject(gFontBold);
        if (gFontTitle) DeleteObject(gFontTitle);
        if (gFontMono) DeleteObject(gFontMono);
        if (gBgBrush) DeleteObject(gBgBrush);

        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

// ------------------------------- main -------------------------------

int main() {
    gInst = GetModuleHandleA(nullptr);

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = gInst;
    wc.lpszClassName = "CourseSystemRealPagesClient";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassExA(&wc)) {
        return 0;
    }

    HWND hwnd = CreateWindowExA(
        0,
        "CourseSystemRealPagesClient",
        "CourseSystem Table Client UI",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        WINDOW_W,
        WINDOW_H,
        nullptr,
        nullptr,
        gInst,
        nullptr
    );

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
