#pragma once
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal RFC-4180 style parser: quoted fields, "" escapes, CRLF/LF, UTF-8 BOM.
inline std::vector<std::vector<std::string>> parseCsv(std::string content) {
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF)
        content.erase(0, 3);

    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < content.size(); ++i) {
        char c = content[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < content.size() && content[i + 1] == '"') { field += '"'; ++i; }
                else inQuotes = false;
            } else field += c;
        } else if (c == '"') inQuotes = true;
        else if (c == ',') { row.push_back(field); field.clear(); }
        else if (c == '\r') { /* ignore */ }
        else if (c == '\n') {
            row.push_back(field); field.clear();
            if (!(row.size() == 1 && row[0].empty())) rows.push_back(row);  // skip blank lines
            row.clear();
        } else field += c;
    }
    if (!field.empty() || !row.empty()) { row.push_back(field); rows.push_back(row); }
    return rows;
}

inline std::vector<std::vector<std::string>> readCsv(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open CSV file: " + path);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseCsv(std::move(content));
}
