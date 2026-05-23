// Server.cpp
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <thread>
#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstdlib>
#include <atomic>
#include <cctype>

#include "json.hpp"
#include "CryptoUtils.h"
using json = nlohmann::json;

extern "C" {
#include "sqlite3.h"
}

#define DEFAULT_PORT 50000
#define MAX_CONNECTIONS 20      // 最大并发连接数
#define RETRY_MAX_ATTEMPTS 20   // 应用层重试次数
#define RETRY_SLEEP_MS 10       // 重试间隔（毫秒）

static std::atomic<int> g_activeConnections{ 0 };

// ====================== Retry Helpers ======================

int sqlite3_step_with_retry(sqlite3_stmt* stmt) {
    int rc;
    for (int i = 0; i <= RETRY_MAX_ATTEMPTS; ++i) {
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_BUSY) break;
        if (i < RETRY_MAX_ATTEMPTS) sqlite3_sleep(RETRY_SLEEP_MS);
    }
    return rc;
}

int sqlite3_exec_with_retry(sqlite3* db, const char* sql) {
    int rc;
    for (int i = 0; i <= RETRY_MAX_ATTEMPTS; ++i) {
        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        if (rc != SQLITE_BUSY) break;
        if (i < RETRY_MAX_ATTEMPTS) sqlite3_sleep(RETRY_SLEEP_MS);
    }
    return rc;
}

//服务器端，数据库内用哈希需要
std::string simple_hash(const std::string& str) {
    unsigned long long hash = 5381ULL;
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    // 转成字符串
    return std::to_string(hash);
}

//初始化数据库（创建空的表）
void initializeDatabase(sqlite3* db) {
    int rc;
    char* errMsg = nullptr;

    // 1. 创建课程基本信息表 - 使用(course_code, section)作为复合主键
    const char* createCoursesTable =
        "CREATE TABLE IF NOT EXISTS courses ("
        "course_code TEXT NOT NULL, "
        "course_title TEXT NOT NULL, "
        "section TEXT NOT NULL, "
        "instructor TEXT, "
        "units INTEGER, "
        "course_category TEXT, "
        "description TEXT, "
        "semester TEXT NOT NULL DEFAULT '2025-2026 S1', "
        "PRIMARY KEY (course_code, section)"
        ");";


    rc = sqlite3_exec(db, createCoursesTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create courses table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Created courses table successfully. Primary key: (course_code, section)\n";

    // 2. 创建课程时间安排表 - 包含course_code和section作为外键
    const char* createSchedulesTable =
        "CREATE TABLE IF NOT EXISTS course_schedules ("
        "schedule_id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "course_code TEXT NOT NULL, "
        "section TEXT NOT NULL, "
        "day_of_week INTEGER NOT NULL, "
        "start_time TEXT NOT NULL, "
        "end_time TEXT NOT NULL, "
        "classroom TEXT NOT NULL, "
        "FOREIGN KEY (course_code, section) REFERENCES courses(course_code, section) ON DELETE CASCADE"
        ");";


    rc = sqlite3_exec(db, createSchedulesTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create course_schedules table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Created course_schedules table successfully.\n";

    // 3. 创建用户信息表，用于登入
    const char* createUserTable =
        "CREATE TABLE IF NOT EXISTS users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "  // 内部 ID
        "name TEXT NOT NULL UNIQUE, "             // 用户名（唯一）
        "password_hash TEXT NOT NULL, "           // 密码哈希
        "type TEXT NOT NULL CHECK(type IN ('admin', 'student'))"
        ");";

    rc = sqlite3_exec(db, createUserTable, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to create users table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Created users table successfully.\n";
}

// 清空所有数据
// 由于外键约束，要先清空子表，再清空主表
void clearAllCourseData(sqlite3* db) {
    char* errMsg = nullptr;

    int rc = sqlite3_exec(db, "DELETE FROM course_schedules;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to clear course_schedules table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    int schedulesDeleted = sqlite3_changes(db);

    rc = sqlite3_exec(db, "DELETE FROM courses;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to clear courses table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    int coursesDeleted = sqlite3_changes(db);

    rc = sqlite3_exec(db, "DELETE FROM users;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to clear users table: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    int usersDeleted = sqlite3_changes(db);


    std::cout << "Cleared " << coursesDeleted << " courses and "
        << schedulesDeleted << " schedule and " << usersDeleted << " user records.\n";
}

// 插入初始课程数据
void reloadInitialCourseData(sqlite3* db) {
    char* errMsg = nullptr;

    clearAllCourseData(db);

    const char* insertCoursesSQL =
        "INSERT INTO courses "
        "(course_code, course_title, section, instructor, units, course_category, description, semester) "
        "VALUES "
        "('COMP3003', 'Data Communications and Networking', '1003', 'Dr. Wentao CHENG', 3, 'MR', 'Teach you how the computer network runs', '2025-2026 S1'), "
        "('COMP3023', 'Design and Analysis of Algorithms', '1003', 'Dr. Wentao CHENG', 3, 'MR', 'Teach you how to design a good Algorithm', '2025-2026 S1'), "
        "('COMP2073', 'Data Programming Workshop', '1003', 'Dr. Jing ZHAO', 3, 'MR', 'Teach you python, and some data analysis methods', '2025-2026 S1'), "
        "('UCLC1033', 'English for Academic Purposes III', '1041', 'Dr. Ian Yick Yan LIU', 3, 'UC', 'Teach you English', '2025-2026 S1'), "
        "('COMP3033', 'Operating Systems', '1001', 'Dr. Xiao DONG', 3, 'MR', 'Operating systems course', '2025-2026 S1'), "
        "('CHI1063', 'Chinese Culture and Modern China', '1012', 'Dr. Lea Zhen LUO', 3, 'UC', 'Chinese culture course', '2025-2026 S1'), "
        "('UCHL1243', 'Golf', '1003', 'Dr. Cheng Wa CHEONG', 1, 'UC', 'Golf course', '2025-2026 S1'), "
        "('WPEX20138001', 'Experiential Arts (Ancient Court Music II)', '1013', 'Mr. Cheng-lin KO', 1, 'WPE', 'Experiential arts course', '2025-2026 S1');";

    int rc = sqlite3_exec(db, insertCoursesSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to insert course data: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Inserted " << sqlite3_changes(db) << " courses.\n";

    const char* insertSchedulesSQL =
        "INSERT INTO course_schedules "
        "(course_code, section, day_of_week, start_time, end_time, classroom) VALUES "
        "('COMP3003', '1003', 1, '09:00', '11:00', 'T7-302'), "
        "('COMP3003', '1003', 3, '13:00', '14:00', 'T7-303'), "
        "('COMP3023', '1003', 2, '10:00', '12:00', 'T7-503'), "
        "('COMP3023', '1003', 4, '14:00', '15:00', 'T6-503'), "
        "('COMP2073', '1003', 3, '15:00', '17:00', 'T4-301'), "
        "('COMP2073', '1003', 5, '09:00', '10:00', 'T4-302'), "
        "('UCLC1033', '1041', 4, '13:00', '15:00', 'T5-301'), "
        "('UCLC1033', '1041', 1, '16:00', '17:00', 'T6-502'), "
        "('COMP3033', '1001', 5, '08:00', '10:00', 'T29-401'), "
        "('COMP3033', '1001', 2, '14:00', '15:00', 'T7-603'), "
        "('CHI1063', '1012', 1, '09:00', '11:00', 'T6-201'), "
        "('CHI1063', '1012', 3, '11:00', '12:00', 'T29-505'), "
        "('UCHL1243', '1003', 2, '15:00', '17:00', 'T5-402'), "
        "('UCHL1243', '1003', 4, '10:00', '11:00', 'Golf Field'), "
        "('WPEX20138001', '1013', 3, '10:00', '12:00', 'CC-132'), "
        "('WPEX20138001', '1013', 5, '13:00', '14:00', 'Music Room 101');";

    rc = sqlite3_exec(db, insertSchedulesSQL, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to insert schedule data: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Inserted " << sqlite3_changes(db) << " schedule records.\n";


    //  用 simple_hash 生成密码哈希
    std::string admin_hash = simple_hash("123456");
    std::string student_hash = simple_hash("123456");
    std::string insertUsersSQL =
        "INSERT INTO users (name, password_hash, type) VALUES "
        "('admin1',   '" + admin_hash + "', 'admin'), "
        "('student1', '" + student_hash + "', 'student');";

    rc = sqlite3_exec(db, insertUsersSQL.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to insert users: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return;
    }
    std::cout << "Inserted " << sqlite3_changes(db) << " user records.\n";

    std::cout << "Initial data loaded successfully.\n";
}

std::string loginUser(sqlite3* db, const std::string& name, const std::string& password) {
    std::string input_hash = simple_hash(password);

    const char* sql = "SELECT password_hash FROM users WHERE name = ?;";
    sqlite3_stmt* stmt;

    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string db_hash =
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        sqlite3_finalize(stmt);
        std::cout << input_hash << std::endl;
        std::cout << db_hash << std::endl;
        return (input_hash == db_hash) ? "OK" : "WRONG_PASSWORD";
    }

    sqlite3_finalize(stmt);
    return "USER_NOT_FOUND";
}

// ====================== Utility Functions ======================

std::string safeColumnText(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    if (text == nullptr) return "";
    return reinterpret_cast<const char*>(text);
}

std::string jsonOkMessage(const std::string& message) {
    json result;
    result["status"] = "OK";
    result["message"] = message;
    return result.dump();
}

std::string jsonErrorMessage(const std::string& message) {
    json result;
    result["status"] = "ERROR";
    result["message"] = message;
    return result.dump();
}

bool hasField(const json& data, const std::string& fieldName) {
    return data.contains(fieldName) && !data[fieldName].is_null();
}

static std::string trimCopy(const std::string& value) {
    size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

static bool isBlank(const std::string& value) {
    return trimCopy(value).empty();
}

static bool readIntField(const json& data, const char* fieldName, int& outValue) {
    if (!data.contains(fieldName) || data[fieldName].is_null()) return false;

    try {
        if (data[fieldName].is_number_integer()) {
            outValue = data[fieldName].get<int>();
            return true;
        }

        if (data[fieldName].is_string()) {
            std::string text = trimCopy(data[fieldName].get<std::string>());
            if (text.empty()) return false;

            size_t used = 0;
            int value = std::stoi(text, &used);
            if (used != text.size()) return false;

            outValue = value;
            return true;
        }
    }
    catch (...) {
        return false;
    }

    return false;
}

static bool isValidTimeHHMM(const std::string& value) {
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

static int timeToMinutes(const std::string& value) {
    return ((value[0] - '0') * 10 + (value[1] - '0')) * 60 +
           ((value[3] - '0') * 10 + (value[4] - '0'));
}

static std::string validateCourseData(const json& data) {
    if (!hasField(data, "course_code") ||
        !hasField(data, "section") ||
        !hasField(data, "course_title") ||
        !hasField(data, "instructor") ||
        !hasField(data, "units") ||
        !hasField(data, "course_category") ||
        !hasField(data, "description")) {
        return "Missing course fields";
    }

    try {
        if (isBlank(data["course_code"].get<std::string>())) return "Course code cannot be empty";
        if (isBlank(data["section"].get<std::string>())) return "Section cannot be empty";
        if (isBlank(data["course_title"].get<std::string>())) return "Course title cannot be empty";
        if (isBlank(data["instructor"].get<std::string>())) return "Instructor cannot be empty";
        if (isBlank(data["course_category"].get<std::string>())) return "Course category cannot be empty";

        int units = 0;
        if (!readIntField(data, "units", units)) return "Units must be an integer";
        if (units <= 0 || units > 10) return "Units must be between 1 and 10";
    }
    catch (...) {
        return "Invalid course data type";
    }

    return "";
}

static std::string validateScheduleData(const json& data, bool requireCourseFields) {
    if (requireCourseFields) {
        if (!hasField(data, "course_code") || !hasField(data, "section")) {
            return "Missing course_code or section";
        }

        try {
            if (isBlank(data["course_code"].get<std::string>())) return "Course code cannot be empty";
            if (isBlank(data["section"].get<std::string>())) return "Section cannot be empty";
        }
        catch (...) {
            return "Invalid course code or section";
        }
    }

    if (!hasField(data, "day_of_week") ||
        !hasField(data, "start_time") ||
        !hasField(data, "end_time") ||
        !hasField(data, "classroom")) {
        return "Missing schedule fields";
    }

    try {
        int day = 0;
        if (!readIntField(data, "day_of_week", day)) return "Day of week must be an integer";
        if (day < 1 || day > 7) return "Day of week must be between 1 and 7";

        std::string startTime = data["start_time"].get<std::string>();
        std::string endTime = data["end_time"].get<std::string>();
        std::string classroom = data["classroom"].get<std::string>();

        if (!isValidTimeHHMM(startTime)) return "Start time must use HH:MM format";
        if (!isValidTimeHHMM(endTime)) return "End time must use HH:MM format";
        if (timeToMinutes(startTime) >= timeToMinutes(endTime)) return "Start time must be earlier than end time";
        if (isBlank(classroom)) return "Classroom cannot be empty";
    }
    catch (...) {
        return "Invalid schedule data type";
    }

    return "";
}

static bool hasClassroomConflict(sqlite3* db,
    int day,
    const std::string& startTime,
    const std::string& endTime,
    const std::string& classroom,
    int ignoreScheduleId,
    std::string& conflictMessage) {

    const char* sql =
        "SELECT course_code, section, start_time, end_time "
        "FROM course_schedules "
        "WHERE day_of_week = ? "
        "AND UPPER(classroom) = UPPER(?) "
        "AND schedule_id <> ? "
        "AND start_time < ? "
        "AND end_time > ? "
        "LIMIT 1;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        conflictMessage = "Failed to check classroom conflict";
        return true;
    }

    sqlite3_bind_int(stmt, 1, day);
    sqlite3_bind_text(stmt, 2, classroom.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, ignoreScheduleId);
    sqlite3_bind_text(stmt, 4, endTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, startTime.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        std::string courseCode = safeColumnText(stmt, 0);
        std::string section = safeColumnText(stmt, 1);
        std::string oldStart = safeColumnText(stmt, 2);
        std::string oldEnd = safeColumnText(stmt, 3);

        conflictMessage = "Classroom conflict with " + courseCode + "-" + section +
            " from " + oldStart + " to " + oldEnd;
        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}


// ====================== Business Logic ======================

std::string listCourses(sqlite3* db) {
    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category "
        "FROM courses ORDER BY course_code, section;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare LIST_COURSES query");

    json result;
    result["status"] = "OK";
    result["courses"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);
        result["courses"].push_back(course);
    }
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while reading courses");
    }
    sqlite3_finalize(stmt);
    return result.dump();
}
std::string listCoursesBySemester(sqlite3* db, const std::string& semester) {
    if (semester.empty()) {
        return jsonErrorMessage("Semester cannot be empty");
    }

    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category "
        "FROM courses "
        "WHERE semester = ? "
        "ORDER BY course_code, section;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return jsonErrorMessage(
            std::string("Failed to prepare LIST_COURSES_BY_SEMESTER query: ") + sqlite3_errmsg(db)
        );
    }

    sqlite3_bind_text(stmt, 1, semester.c_str(), -1, SQLITE_TRANSIENT);

    json result;
    result["status"] = "OK";
    result["semester"] = semester;
    result["courses"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);

        result["courses"].push_back(course);
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while listing courses by semester");
    }

    sqlite3_finalize(stmt);
    return result.dump();
}

std::string searchCoursesByCode(sqlite3* db, const std::string& keyword) {
    if (keyword.empty()) {
        return jsonErrorMessage("Course code cannot be empty");
    }

    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category "
        "FROM courses "
        "WHERE UPPER(course_code) LIKE UPPER(?) "
        "ORDER BY course_code, section;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare SEARCH_COURSES_BY_CODE query");
    }

    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    json result;
    result["status"] = "OK";
    result["courses"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);
        result["courses"].push_back(course);
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while searching courses by code");
    }

    sqlite3_finalize(stmt);
    return result.dump();
}

std::string searchCoursesByInstructor(sqlite3* db, const std::string& keyword) {
    if (keyword.empty()) {
        return jsonErrorMessage("Instructor cannot be empty");
    }

    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category "
        "FROM courses "
        "WHERE UPPER(instructor) LIKE UPPER(?) "
        "ORDER BY instructor, course_code, section;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare SEARCH_COURSES_BY_INSTRUCTOR query");
    }

    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    json result;
    result["status"] = "OK";
    result["courses"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);
        result["courses"].push_back(course);
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while searching courses by instructor");
    }

    sqlite3_finalize(stmt);
    return result.dump();
}


std::string searchCoursesByCategory(sqlite3* db, const std::string& keyword) {
    if (keyword.empty()) {
        return jsonErrorMessage("Course category cannot be empty");
    }

    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category "
        "FROM courses "
        "WHERE UPPER(course_category) LIKE UPPER(?) "
        "ORDER BY course_category, course_code, section;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare SEARCH_COURSES_BY_CATEGORY query");
    }

    std::string pattern = "%" + keyword + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);

    json result;
    result["status"] = "OK";
    result["courses"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);
        result["courses"].push_back(course);
    }

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while searching courses by category");
    }

    sqlite3_finalize(stmt);
    return result.dump();
}

std::string getCourse(sqlite3* db, const std::string& courseCode, const std::string& section) {
    const char* sql =
        "SELECT course_code, course_title, section, instructor, units, course_category, description "
        "FROM courses WHERE course_code = ? AND section = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare GET_COURSE query");

    sqlite3_bind_text(stmt, 1, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, section.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        json course;
        course["course_code"] = safeColumnText(stmt, 0);
        course["course_title"] = safeColumnText(stmt, 1);
        course["section"] = safeColumnText(stmt, 2);
        course["instructor"] = safeColumnText(stmt, 3);
        course["units"] = sqlite3_column_int(stmt, 4);
        course["course_category"] = safeColumnText(stmt, 5);
        course["description"] = safeColumnText(stmt, 6);

        json result;
        result["status"] = "OK";
        result["course"] = course;
        sqlite3_finalize(stmt);
        return result.dump();
    }
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) return jsonErrorMessage("Course not found");
    return jsonErrorMessage("Failed while reading course");
}

std::string getSchedule(sqlite3* db, const std::string& courseCode, const std::string& section) {
    const char* sql =
        "SELECT schedule_id, day_of_week, start_time, end_time, classroom "
        "FROM course_schedules WHERE course_code = ? AND section = ? "
        "ORDER BY day_of_week, start_time;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare GET_SCHEDULE query");

    sqlite3_bind_text(stmt, 1, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, section.c_str(), -1, SQLITE_TRANSIENT);

    json result;
    result["status"] = "OK";
    result["schedule"] = json::array();

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        json row;
        row["schedule_id"] = sqlite3_column_int(stmt, 0);
        row["day_of_week"] = sqlite3_column_int(stmt, 1);
        row["start_time"] = safeColumnText(stmt, 2);
        row["end_time"] = safeColumnText(stmt, 3);
        row["classroom"] = safeColumnText(stmt, 4);
        result["schedule"].push_back(row);
    }
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return jsonErrorMessage("Failed while reading schedule");
    }
    sqlite3_finalize(stmt);
    if (result["schedule"].empty()) return jsonErrorMessage("Schedule not found");
    return result.dump();
}
std::string addSchedule(sqlite3* db, const json& data) {
    std::string validation = validateScheduleData(data, true);
    if (!validation.empty()) return jsonErrorMessage(validation);

    int day = 0;
    readIntField(data, "day_of_week", day);

    std::string startTime = data["start_time"].get<std::string>();
    std::string endTime = data["end_time"].get<std::string>();
    std::string classroom = data["classroom"].get<std::string>();

    std::string conflict;
    if (hasClassroomConflict(db, day, startTime, endTime, classroom, -1, conflict)) {
        return jsonErrorMessage(conflict);
    }

    const char* sql =
        "INSERT INTO course_schedules "
        "(course_code, section, day_of_week, start_time, end_time, classroom) "
        "VALUES (?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare ADD_SCHEDULE query");
    }

    sqlite3_bind_text(stmt, 1, data["course_code"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, data["section"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, day);
    sqlite3_bind_text(stmt, 4, startTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, endTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, classroom.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_DONE) {
        return jsonOkMessage("Schedule added");
    }

    if (rc == SQLITE_CONSTRAINT) {
        return jsonErrorMessage("Course does not exist or schedule constraint failed");
    }

    if (rc == SQLITE_BUSY) {
        return jsonErrorMessage("Database busy after many retries");
    }

    return jsonErrorMessage("Failed to add schedule");
}

std::string updateSchedule(sqlite3* db, const json& data) {
    if (!hasField(data, "schedule_id")) {
        return jsonErrorMessage("Missing schedule_id for UPDATE_SCHEDULE");
    }

    int scheduleId = 0;
    if (!readIntField(data, "schedule_id", scheduleId) || scheduleId <= 0) {
        return jsonErrorMessage("Invalid schedule_id");
    }

    std::string validation = validateScheduleData(data, false);
    if (!validation.empty()) return jsonErrorMessage(validation);

    int day = 0;
    readIntField(data, "day_of_week", day);

    std::string startTime = data["start_time"].get<std::string>();
    std::string endTime = data["end_time"].get<std::string>();
    std::string classroom = data["classroom"].get<std::string>();

    std::string conflict;
    if (hasClassroomConflict(db, day, startTime, endTime, classroom, scheduleId, conflict)) {
        return jsonErrorMessage(conflict);
    }

    const char* sql =
        "UPDATE course_schedules "
        "SET day_of_week = ?, start_time = ?, end_time = ?, classroom = ? "
        "WHERE schedule_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare UPDATE_SCHEDULE query");
    }

    sqlite3_bind_int(stmt, 1, day);
    sqlite3_bind_text(stmt, 2, startTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, endTime.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, classroom.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, scheduleId);

    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_BUSY) {
        return jsonErrorMessage("Database busy after many retries");
    }

    if (rc != SQLITE_DONE) {
        return jsonErrorMessage("Failed to update schedule");
    }

    if (sqlite3_changes(db) == 0) {
        return jsonErrorMessage("Schedule record not found");
    }

    return jsonOkMessage("Schedule updated");
}

std::string deleteSchedule(sqlite3* db, const json& data) {
    if (!hasField(data, "schedule_id")) {
        return jsonErrorMessage("Missing schedule_id for DELETE_SCHEDULE");
    }

    const char* sql =
        "DELETE FROM course_schedules WHERE schedule_id = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return jsonErrorMessage("Failed to prepare DELETE_SCHEDULE query");
    }

    sqlite3_bind_int(stmt, 1, data["schedule_id"].get<int>());

    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_BUSY) {
        return jsonErrorMessage("Database busy after many retries");
    }

    if (rc != SQLITE_DONE) {
        return jsonErrorMessage("Failed to delete schedule");
    }

    if (sqlite3_changes(db) == 0) {
        return jsonErrorMessage("Schedule record not found");
    }

    return jsonOkMessage("Schedule deleted");
}

std::string addCourse(sqlite3* db, const json& data) {
    std::string validation = validateCourseData(data);
    if (!validation.empty()) return jsonErrorMessage(validation);

    int units = 0;
    readIntField(data, "units", units);

    const char* sql =
        "INSERT INTO courses "
        "(course_code, course_title, section, instructor, units, course_category, description) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare ADD_COURSE query");

    sqlite3_bind_text(stmt, 1, data["course_code"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, data["course_title"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, data["section"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, data["instructor"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, units);
    sqlite3_bind_text(stmt, 6, data["course_category"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, data["description"].get<std::string>().c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_DONE) return jsonOkMessage("Course added");
    if (rc == SQLITE_CONSTRAINT) return jsonErrorMessage("Course already exists");
    if (rc == SQLITE_BUSY) return jsonErrorMessage("Database busy after many retries");
    return jsonErrorMessage("Failed to add course");
}

std::string updateCourse(sqlite3* db, const json& data) {
    std::string validation = validateCourseData(data);
    if (!validation.empty()) return jsonErrorMessage(validation);

    const char* sql =
        "UPDATE courses "
        "SET course_title = ?, instructor = ?, units = ?, course_category = ?, description = ? "
        "WHERE course_code = ? AND section = ?;";

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return jsonErrorMessage(std::string("Failed to prepare UPDATE_COURSE query: ") + sqlite3_errmsg(db));
    }

    std::string courseTitle = data["course_title"].get<std::string>();
    std::string instructor = data["instructor"].get<std::string>();
    int units = 0;
    readIntField(data, "units", units);
    std::string category = data["course_category"].get<std::string>();
    std::string description = data["description"].get<std::string>();
    std::string courseCode = data["course_code"].get<std::string>();
    std::string section = data["section"].get<std::string>();

    sqlite3_bind_text(stmt, 1, courseTitle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, instructor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, units);
    sqlite3_bind_text(stmt, 4, category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, section.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);

    if (rc == SQLITE_BUSY) return jsonErrorMessage("Database busy after many retries");
    if (rc != SQLITE_DONE) return jsonErrorMessage("Failed to update course");
    if (sqlite3_changes(db) == 0) return jsonErrorMessage("Course not found");

    return jsonOkMessage("Course updated");
}

std::string updateInstructor(sqlite3* db, const std::string& courseCode,
    const std::string& section, const std::string& newInstructor) {
    const char* sql =
        "UPDATE courses SET instructor = ? WHERE course_code = ? AND section = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare UPDATE_INSTRUCTOR query");

    sqlite3_bind_text(stmt, 1, newInstructor.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, section.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_BUSY) return jsonErrorMessage("Database busy after many retries");
    if (rc != SQLITE_DONE) return jsonErrorMessage("Failed to update instructor");
    if (sqlite3_changes(db) == 0) return jsonErrorMessage("Course not found");
    return jsonOkMessage("Instructor updated");
}

std::string deleteCourse(sqlite3* db, const std::string& courseCode, const std::string& section) {
    const char* deleteScheduleSql =
        "DELETE FROM course_schedules WHERE course_code = ? AND section = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db, deleteScheduleSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare DELETE schedule query");

    sqlite3_bind_text(stmt, 1, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, section.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_BUSY) return jsonErrorMessage("Database busy after many retries");
    if (rc != SQLITE_DONE) return jsonErrorMessage("Failed to delete course schedule");

    const char* deleteCourseSql =
        "DELETE FROM courses WHERE course_code = ? AND section = ?;";
    stmt = nullptr;
    rc = sqlite3_prepare_v2(db, deleteCourseSql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return jsonErrorMessage("Failed to prepare DELETE course query");

    sqlite3_bind_text(stmt, 1, courseCode.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, section.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step_with_retry(stmt);
    sqlite3_finalize(stmt);
    if (rc == SQLITE_BUSY) return jsonErrorMessage("Database busy after many retries");
    if (rc != SQLITE_DONE) return jsonErrorMessage("Failed to delete course");
    if (sqlite3_changes(db) == 0) return jsonErrorMessage("Course not found");
    return jsonOkMessage("Course deleted");
}

// ====================== Request Handler ======================
std::string jsonOkWithType(const std::string& userType) {
    json j;
    j["status"] = "OK";
    j["type"] = userType;
    return j.dump();
}
std::string handleRequest(sqlite3* db, const std::string& request) {
    try {
        json req = json::parse(request);
        if (!req.is_object()) return jsonErrorMessage("Request must be a JSON object");
        if (!hasField(req, "command")) return jsonErrorMessage("Missing command");

        std::string command = req["command"].get<std::string>();
        json data = req.contains("data") && req["data"].is_object() ? req["data"] : json::object();

        if (command == "LIST_COURSES") {
            return listCourses(db);
        }
        else if (command == "LIST_COURSES_BY_SEMESTER") {
            if (!hasField(data, "semester")) {
                return jsonErrorMessage("Usage: LIST_COURSES_BY_SEMESTER requires semester");
            }

            return listCoursesBySemester(
                db,
                data["semester"].get<std::string>()
            );
        }
        else if (command == "SEARCH_COURSES_BY_CODE") {
            if (!hasField(data, "course_code")) {
                return jsonErrorMessage("Usage: SEARCH_COURSES_BY_CODE requires course_code");
            }

            return searchCoursesByCode(
                db,
                data["course_code"].get<std::string>()
            );
        }
        else if (command == "SEARCH_COURSES_BY_INSTRUCTOR") {
            if (!hasField(data, "instructor")) {
                return jsonErrorMessage("Usage: SEARCH_COURSES_BY_INSTRUCTOR requires instructor");
            }

            return searchCoursesByInstructor(
                db,
                data["instructor"].get<std::string>()
            );
        }
        else if (command == "SEARCH_COURSES_BY_CATEGORY") {
            if (!hasField(data, "course_category")) {
                return jsonErrorMessage("Usage: SEARCH_COURSES_BY_CATEGORY requires course_category");
            }

            return searchCoursesByCategory(
                db,
                data["course_category"].get<std::string>()
            );
        }
        else if (command == "GET_COURSE") {
            if (!hasField(data, "course_code") || !hasField(data, "section"))
                return jsonErrorMessage("Usage: GET_COURSE requires course_code and section");
            return getCourse(db, data["course_code"], data["section"]);
        }
        else if (command == "GET_SCHEDULE") {
            if (!hasField(data, "course_code") || !hasField(data, "section"))
                return jsonErrorMessage("Usage: GET_SCHEDULE requires course_code and section");
            return getSchedule(db, data["course_code"], data["section"]);
        }
        else if (command == "ADD_SCHEDULE") {
            return addSchedule(db, data);
        }
        else if (command == "UPDATE_SCHEDULE") {
            return updateSchedule(db, data);
        }
        else if (command == "DELETE_SCHEDULE") {
            return deleteSchedule(db, data);
        }

        else if (command == "ADD_COURSE") return addCourse(db, data);
        else if (command == "UPDATE_COURSE") {
            return updateCourse(db, data);
        }
        else if (command == "UPDATE_INSTRUCTOR") {
            if (!hasField(data, "course_code") || !hasField(data, "section") || !hasField(data, "instructor"))
                return jsonErrorMessage("Usage: UPDATE_INSTRUCTOR requires course_code, section and instructor");
            return updateInstructor(db, data["course_code"], data["section"], data["instructor"]);
        }
        else if (command == "DELETE_COURSE") {
            if (!hasField(data, "course_code") || !hasField(data, "section"))
                return jsonErrorMessage("Usage: DELETE_COURSE requires course_code and section");
            return deleteCourse(db, data["course_code"], data["section"]);
        }
        else if (command == "RELOAD_DATA") {
            reloadInitialCourseData(db);
            return jsonOkMessage("Initial course data reloaded");
        }
        else if (command == "QUIT") return jsonOkMessage("bye");
        else if (command == "LOGIN") {
            if (!hasField(data, "name") || !hasField(data, "password")) {
                return jsonErrorMessage("Usage: LOGIN requires name and password");
            }

            std::string name = data["name"].get<std::string>();
            std::string password = data["password"].get<std::string>();

            std::string result = loginUser(db, name, password);

            if (result == "OK") {
                // 查询用户类型
                const char* sql = "SELECT type FROM users WHERE name = ?;";
                sqlite3_stmt* stmt;
                sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    std::string type =
                        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    sqlite3_finalize(stmt);

                    return jsonOkWithType(type); // 你稍后加这个函数
                }

                sqlite3_finalize(stmt);
                return jsonErrorMessage("User type not found");
            }

            // 登录失败
            return jsonErrorMessage(result); // USER_NOT_FOUND / WRONG_PASSWORD
        }
        else return jsonErrorMessage("Unknown command");
    }
    catch (const json::parse_error&) { return jsonErrorMessage("Invalid JSON"); }
    catch (const json::type_error&) { return jsonErrorMessage("Invalid JSON data type"); }
    catch (const json::out_of_range&) { return jsonErrorMessage("Missing JSON field"); }
    catch (...) { return jsonErrorMessage("Failed to handle request"); }
}

// ====================== Per-Thread Database Connection ======================

sqlite3* openDatabaseConnection() {
    sqlite3* db = nullptr;
    int rc = sqlite3_open("database.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Thread: Cannot open database: " << sqlite3_errmsg(db) << std::endl;
        return nullptr;
    }
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db, 5000);
    return db;
}

// ====================== Client Handler Thread ======================

void handleClient(SOCKET msg_sock, sockaddr_in client_addr) {
    std::string request;

    g_activeConnections++;
    printf("Thread started for client %s:%d, active connections: %d\n",
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
        g_activeConnections.load());

    sqlite3* db = openDatabaseConnection();
    if (db == nullptr) {
        const char* errMsg = R"({"status":"ERROR","message":"Database connection failed"})";
        sendSecureString(msg_sock, errMsg);
        closesocket(msg_sock);
        g_activeConnections--;
        return;
    }

    while (true) {
        request.clear();

        if (!recvSecureString(msg_sock, request)) {
            printf("Secure receive failed or client closed connection: %s:%d\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            break;
        }

        printf("Secure message received: %s from %s\n",
            request.c_str(), inet_ntoa(client_addr.sin_addr));

        std::string response = handleRequest(db, request);

        if (!sendSecureString(msg_sock, response)) {
            fprintf(stderr, "secure send failed\n");
            break;
        }

        if (request.find("\"QUIT\"") != std::string::npos) {
            printf("Client %s:%d requested quit.\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            break;
        }
    }

    sqlite3_close(db);
    closesocket(msg_sock);
    g_activeConnections--;
    printf("Thread for client %s:%d finished, active connections: %d\n",
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
        g_activeConnections.load());
}

// ====================== Main Function ======================

int main(int argc, char** argv) {
    // --- Database initialization (run once) ---
    FILE* existingDb = fopen("database.db", "rb");
    bool fileExists = (existingDb != nullptr);
    if (existingDb) fclose(existingDb);
    sqlite3* init_db = nullptr;
    int rc = sqlite3_open("database.db", &init_db);
    if (rc != SQLITE_OK) {
        std::cerr << "Cannot open database for init: " << sqlite3_errmsg(init_db) << std::endl;
        sqlite3_close(init_db);
        return -1;
    }
    sqlite3_exec(init_db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    sqlite3_exec(init_db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Always make sure all required tables exist.
    // CREATE TABLE IF NOT EXISTS is safe for an existing old database.
    if (!fileExists) {
        std::cout << "Database file not found. Creating and initializing.\n";
    }
    initializeDatabase(init_db);

    char c;
    std::cout << "Reload initial test data? (y/n): ";
    std::cin >> c;
    if (c == 'y' || c == 'Y') {
        reloadInitialCourseData(init_db);
    }
    sqlite3_close(init_db);

    // --- Network setup ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", WSAGetLastError());
        return -1;
    }

    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = htons(DEFAULT_PORT);

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup();
        return -1;
    }

    if (bind(listen_sock, (struct sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        fprintf(stderr, "bind() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return -1;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        fprintf(stderr, "listen() failed: %d\n", WSAGetLastError());
        closesocket(listen_sock);
        WSACleanup();
        return -1;
    }

    printf("Server listening on port %d, waiting for connections...\n", DEFAULT_PORT);
    printf("Maximum concurrent connections: %d\n", MAX_CONNECTIONS);

    // --- Main accept loop with connection limit ---
    while (true) {
        struct sockaddr_in client_addr;
        int addr_len = sizeof(client_addr);
        SOCKET client_sock = accept(listen_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) {
            fprintf(stderr, "accept() failed: %d\n", WSAGetLastError());
            break;
        }

        if (g_activeConnections >= MAX_CONNECTIONS) {
            printf("Connection from %s:%d rejected: too many connections (max %d)\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
                MAX_CONNECTIONS);
            const char* rejectMsg = R"({"status":"ERROR","message":"Server busy, too many clients. Try later."})";
            send(client_sock, rejectMsg, (int)strlen(rejectMsg), 0);
            closesocket(client_sock);
            continue;
        }

        printf("Accepted connection from %s, port %d\n",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        std::thread clientThread(handleClient, client_sock, client_addr);
        clientThread.detach();
    }

    closesocket(listen_sock);
    WSACleanup();
    return 0;
}