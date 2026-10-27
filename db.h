#pragma once
#include <sqlite3.h>

#include <stdexcept>
#include <string>

// RAII wrapper around a sqlite3 connection.
class Database {
    sqlite3* db_ = nullptr;

public:
    explicit Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
            std::string m = sqlite3_errmsg(db_);
            sqlite3_close(db_);
            throw std::runtime_error("sqlite open failed: " + m);
        }
        exec("PRAGMA foreign_keys = ON;");
        exec("PRAGMA journal_mode = WAL;");
    }
    ~Database() { if (db_) sqlite3_close(db_); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    sqlite3* handle() const { return db_; }
    int changes() const { return sqlite3_changes(db_); }

    void exec(const std::string& sql) {
        char* err = nullptr;
        if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
            std::string m = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("sqlite exec failed: " + m);
        }
    }
};

// RAII prepared statement.
class Statement {
    sqlite3_stmt* s_ = nullptr;
    sqlite3* db_;

public:
    Statement(Database& d, const std::string& sql) : db_(d.handle()) {
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &s_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("sqlite prepare failed: ") + sqlite3_errmsg(db_));
    }
    ~Statement() { sqlite3_finalize(s_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int i, const std::string& v) {
        sqlite3_bind_text(s_, i, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    Statement& bind(int i, int v) { sqlite3_bind_int(s_, i, v); return *this; }
    Statement& bind(int i, double v) { sqlite3_bind_double(s_, i, v); return *this; }

    // true -> a row is available; false -> finished. Throws on error.
    bool step() {
        int rc = sqlite3_step(s_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(std::string("sqlite step failed: ") + sqlite3_errmsg(db_));
    }
    void reset() { sqlite3_reset(s_); sqlite3_clear_bindings(s_); }

    std::string text(int c) {
        auto p = sqlite3_column_text(s_, c);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
    int integer(int c) { return sqlite3_column_int(s_, c); }
    double real(int c) { return sqlite3_column_double(s_, c); }
};

// BEGIN ... COMMIT, with automatic ROLLBACK if commit() isn't reached.
class Transaction {
    Database& d_;
    bool done_ = false;

public:
    explicit Transaction(Database& d) : d_(d) { d_.exec("BEGIN"); }
    void commit() { d_.exec("COMMIT"); done_ = true; }
    ~Transaction() {
        if (!done_) { try { d_.exec("ROLLBACK"); } catch (...) {} }
    }
};
