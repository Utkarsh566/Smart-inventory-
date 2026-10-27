#pragma once
#include <nlohmann/json.hpp>

#include "inventory_service.h"
#include "models.h"

using json = nlohmann::json;

inline json toJson(const Product& p) {
    return {{"id", p.id}, {"name", p.name}, {"category", p.category}, {"price", p.price},
            {"stock", p.stock}, {"minimumStock", p.minStock}, {"supplierId", p.supplierId}};
}
inline json toJson(const Supplier& s) {
    return {{"id", s.id}, {"name", s.name}, {"email", s.email}, {"phone", s.phone}, {"address", s.address}};
}
inline json toJson(const Sale& s) {
    return {{"id", s.id}, {"productId", s.productId}, {"quantity", s.quantity},
            {"totalAmount", s.total}, {"saleDate", s.date}};
}
inline json toJson(const Txn& t) {
    return {{"id", t.id}, {"productId", t.productId}, {"type", t.type},
            {"quantity", t.quantity}, {"date", t.date}};
}
template <class T>
json toJsonArray(const std::vector<T>& v) {
    json a = json::array();
    for (const auto& x : v) a.push_back(toJson(x));
    return a;
}
