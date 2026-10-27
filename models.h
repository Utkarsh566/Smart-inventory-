#pragma once
#include <string>

struct Supplier {
    std::string id, name, email, phone, address;
};

struct Product {
    std::string id, name, category;
    double price = 0;
    int stock = 0;
    int minStock = 0;
    std::string supplierId;
};

struct Sale {
    std::string id, productId;
    int quantity = 0;
    double total = 0;
    std::string date;  // YYYY-MM-DD
};

struct Txn {
    std::string id, productId, type;  // type: "IN" | "OUT"
    int quantity = 0;
    std::string date;
};

struct User {
    std::string username, passwordHash, role;  // role: "admin" | "staff"
};
