#pragma once
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <unordered_map>

#include "inventory_service.h"  // ApiError
#include "repository.h"
#include "sha256.h"

namespace auth {

inline std::string randomHex(size_t bytes) {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < bytes; ++i) {
        unsigned v = rd() & 0xFF;
        s += hex[v >> 4]; s += hex[v & 15];
    }
    return s;
}

constexpr int kIterations = 50000;

// "salt$hash": iterated, salted SHA-256. (For production prefer Argon2/bcrypt.)
inline std::string digest(const std::string& salt, const std::string& password) {
    std::string base = salt + password;
    auto d = sha::sha256(std::vector<uint8_t>(base.begin(), base.end()));
    for (int i = 0; i < kIterations; ++i) {
        std::vector<uint8_t> buf(d.begin(), d.end());
        buf.insert(buf.end(), base.begin(), base.end());
        d = sha::sha256(buf);
    }
    return sha::toHex(d.data(), d.size());
}

inline std::string hashPassword(const std::string& password) {
    std::string salt = randomHex(16);
    return salt + "$" + digest(salt, password);
}

inline bool verifyPassword(const std::string& password, const std::string& stored) {
    auto pos = stored.find('$');
    if (pos == std::string::npos) return false;
    std::string expected = stored.substr(pos + 1);
    std::string actual = digest(stored.substr(0, pos), password);
    if (expected.size() != actual.size()) return false;
    unsigned diff = 0;  // constant-time compare
    for (size_t i = 0; i < actual.size(); ++i) diff |= (expected[i] ^ actual[i]);
    return diff == 0;
}

struct Session {
    std::string username, role;
    std::chrono::steady_clock::time_point expires;
};

class AuthService {
public:
    explicit AuthService(Repository& repo) : repo_(repo) {}

    void seedAdminIfEmpty() {
        if (repo_.userCount() > 0) return;
        const char* env = std::getenv("INVENTORY_ADMIN_PASSWORD");
        std::string pw = env && *env ? env : "admin123";
        repo_.insertUser({"admin", hashPassword(pw), "admin"});
        std::cout << "[auth] Created default user 'admin'"
                  << (env && *env ? "" : " with password 'admin123' - CHANGE IT") << "\n";
    }

    void createUser(const std::string& username, const std::string& password, const std::string& role) {
        if (username.size() < 3 || username.size() > 32) throw ApiError(400, "username must be 3-32 characters");
        for (char c : username)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '-')
                throw ApiError(400, "username may only contain letters, digits, _ . -");
        if (password.size() < 8) throw ApiError(400, "password must be at least 8 characters");
        if (role != "admin" && role != "staff") throw ApiError(400, "role must be 'admin' or 'staff'");
        if (repo_.findUser(username)) throw ApiError(409, "username already exists");
        repo_.insertUser({username, hashPassword(password), role});
    }

    // Returns token, or nullopt on bad credentials.
    std::optional<std::pair<std::string, std::string>> login(const std::string& username,
                                                             const std::string& password) {
        auto u = repo_.findUser(username);
        // Hash even for unknown users so response time doesn't reveal which usernames exist.
        std::string stored = u ? u->passwordHash : "00$00";
        bool ok = verifyPassword(password, stored) && u;
        if (!ok) return std::nullopt;
        std::string token = randomHex(32);
        std::lock_guard<std::mutex> lk(m_);
        sessions_[token] = {u->username, u->role, std::chrono::steady_clock::now() + std::chrono::hours(8)};
        return std::make_pair(token, u->role);
    }

    std::optional<Session> validate(const std::string& token) {
        if (token.empty()) return std::nullopt;
        std::lock_guard<std::mutex> lk(m_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return std::nullopt;
        if (it->second.expires < std::chrono::steady_clock::now()) { sessions_.erase(it); return std::nullopt; }
        return it->second;
    }

private:
    Repository& repo_;
    std::mutex m_;
    std::unordered_map<std::string, Session> sessions_;
};

}  // namespace auth
