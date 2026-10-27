#include "inventory_service.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <functional>
#include <queue>
#include <unordered_set>

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char c : s) {
        if (std::isalnum(c)) cur += static_cast<char>(std::tolower(c));
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string fmtId(const char* prefix, int width, int n) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%0*d", prefix, width, n);
    return buf;
}

bool validDate(const std::string& d) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (size_t i = 0; i < d.size(); ++i)
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(d[i]))) return false;
    int m = std::stoi(d.substr(5, 2)), day = std::stoi(d.substr(8, 2));
    return m >= 1 && m <= 12 && day >= 1 && day <= 31;
}

std::string today() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

std::string resolveDate(const std::string& d, const char* field) {
    if (d.empty()) return today();
    if (!validDate(d)) throw ApiError(400, std::string(field) + " must be YYYY-MM-DD");
    return d;
}

void removeId(std::vector<std::string>& v, const std::string& id) {
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

}  // namespace

// ---------- index maintenance ----------

void InventoryService::indexProduct(const Product& p) {
    byCategory_[lower(p.category)].push_back(p.id);
    bySupplier_[p.supplierId].push_back(p.id);
    for (const auto& t : tokens(p.name)) nameIndex_.insert(t, p.id);
}

void InventoryService::unindexProduct(const Product& p) {
    removeId(byCategory_[lower(p.category)], p.id);
    removeId(bySupplier_[p.supplierId], p.id);
    for (const auto& t : tokens(p.name)) nameIndex_.erase(t, p.id);
}

void InventoryService::addStats(const Sale& s) {
    auto& st = stats_[s.productId];
    st.units += s.quantity;
    st.revenue += s.total;
    if (s.date.size() >= 7) revenueByMonth_[s.date.substr(0, 7)] += s.total;
}

void InventoryService::load() {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    products_.clear(); suppliers_.clear(); byCategory_.clear(); bySupplier_.clear();
    nameIndex_ = Trie(); stats_.clear(); revenueByMonth_.clear();

    for (auto& s : repo_.allSuppliers()) suppliers_[s.id] = s;
    for (auto& p : repo_.allProducts()) { products_[p.id] = p; indexProduct(p); }
    for (auto& s : repo_.allSales()) addStats(s);

    nextProduct_ = repo_.maxIdNumber("products", "product_id");
    nextSupplier_ = repo_.maxIdNumber("suppliers", "supplier_id");
    nextSale_ = repo_.maxIdNumber("sales", "sale_id");
    nextTxn_ = repo_.maxIdNumber("inventory_transactions", "transaction_id");
}

// ---------- products ----------

void InventoryService::validateProduct(const Product& p) const {
    if (p.name.empty()) throw ApiError(400, "name is required");
    if (p.category.empty()) throw ApiError(400, "category is required");
    if (p.price < 0) throw ApiError(400, "price must be >= 0");
    if (p.stock < 0) throw ApiError(400, "stock must be >= 0");
    if (p.minStock < 0) throw ApiError(400, "minimumStock must be >= 0");
    if (!suppliers_.count(p.supplierId)) throw ApiError(400, "Unknown supplierId: " + p.supplierId);
}

Product InventoryService::getProduct(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = products_.find(id);
    if (it == products_.end()) throw ApiError(404, "Product not found: " + id);
    return it->second;
}

std::vector<std::string> InventoryService::searchNames(const std::string& q) const {
    std::unordered_set<std::string> result;
    bool first = true;
    for (const auto& t : tokens(q)) {  // every word must match some name word by prefix
        auto hits = nameIndex_.withPrefix(t);
        if (first) { result = std::move(hits); first = false; }
        else {
            for (auto it = result.begin(); it != result.end();)
                it = hits.count(*it) ? std::next(it) : result.erase(it);
        }
    }
    return {result.begin(), result.end()};
}

std::vector<Product> InventoryService::listProducts(const std::string& category,
                                                    const std::string& supplierId,
                                                    const std::string& query) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    std::vector<std::string> ids;
    // Seed the candidate list from the most selective index available.
    if (!query.empty()) ids = searchNames(query);
    else if (!category.empty()) {
        auto it = byCategory_.find(lower(category));
        if (it != byCategory_.end()) ids = it->second;
    } else if (!supplierId.empty()) {
        auto it = bySupplier_.find(supplierId);
        if (it != bySupplier_.end()) ids = it->second;
    } else {
        for (const auto& kv : products_) ids.push_back(kv.first);
    }
    std::vector<Product> out;
    for (const auto& id : ids) {
        auto it = products_.find(id);
        if (it == products_.end()) continue;
        const Product& p = it->second;
        if (!category.empty() && lower(p.category) != lower(category)) continue;
        if (!supplierId.empty() && p.supplierId != supplierId) continue;
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const Product& a, const Product& b) { return a.id < b.id; });
    return out;
}

Product InventoryService::createProduct(Product p) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    validateProduct(p);
    p.id = fmtId("P", 3, nextProduct_ + 1);
    repo_.insertProduct(p);
    ++nextProduct_;
    products_[p.id] = p;
    indexProduct(p);
    return p;
}

Product InventoryService::updateProduct(const std::string& id, const Product& c) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto it = products_.find(id);
    if (it == products_.end()) throw ApiError(404, "Product not found: " + id);
    Product updated = c;
    updated.id = id;
    updated.stock = it->second.stock;  // stock only changes through sales/restock (keeps the ledger honest)
    validateProduct(updated);
    repo_.updateProduct(updated);
    unindexProduct(it->second);
    it->second = updated;
    indexProduct(updated);
    return updated;
}

void InventoryService::deleteProduct(const std::string& id) {
    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto it = products_.find(id);
    if (it == products_.end()) throw ApiError(404, "Product not found: " + id);
    try {
        repo_.deleteProduct(id);
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()).find("FOREIGN KEY") != std::string::npos)
            throw ApiError(409, "Cannot delete: product has sales or stock-movement history");
        throw;
    }
    unindexProduct(it->second);
    stats_.erase(id);
    products_.erase(it);
}

// Min-heap keyed by stock/minimumStock: most critical product comes out first.
std::vector<Product> InventoryService::lowStock() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    using Item = std::pair<double, std::string>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (const auto& kv : products_) {
        const Product& p = kv.second;
        if (p.stock < p.minStock)
            heap.push({p.minStock > 0 ? static_cast<double>(p.stock) / p.minStock : 0.0, p.id});
    }
    std::vector<Product> out;
    while (!heap.empty()) { out.push_back(products_.at(heap.top().second)); heap.pop(); }
    return out;
}

// ---------- suppliers ----------

std::vector<Supplier> InventoryService::listSuppliers() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    std::vector<Supplier> out;
    for (const auto& kv : suppliers_) out.push_back(kv.second);
    std::sort(out.begin(), out.end(), [](const Supplier& a, const Supplier& b) { return a.id < b.id; });
    return out;
}

Supplier InventoryService::getSupplier(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    auto it = suppliers_.find(id);
    if (it == suppliers_.end()) throw ApiError(404, "Supplier not found: " + id);
    return it->second;
}

Supplier InventoryService::createSupplier(Supplier s) {
    if (s.name.empty()) throw ApiError(400, "name is required");
    if (s.email.empty() || s.email.find('@') == std::string::npos)
        throw ApiError(400, "a valid email is required");
    std::unique_lock<std::shared_mutex> lk(mtx_);
    s.id = fmtId("S", 3, nextSupplier_ + 1);
    repo_.insertSupplier(s);
    ++nextSupplier_;
    suppliers_[s.id] = s;
    return s;
}

std::vector<Product> InventoryService::productsOfSupplier(const std::string& id) const {
    getSupplier(id);  // 404 if missing
    return listProducts("", id, "");
}

// ---------- sales and stock movements ----------

Sale InventoryService::recordSale(const std::string& productId, int qty, std::string date) {
    if (qty <= 0) throw ApiError(400, "quantity must be a positive integer");
    date = resolveDate(date, "saleDate");

    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto it = products_.find(productId);
    if (it == products_.end()) throw ApiError(404, "Product not found: " + productId);
    Product& p = it->second;
    if (qty > p.stock)
        throw ApiError(409, "Insufficient stock: requested " + std::to_string(qty) +
                                ", available " + std::to_string(p.stock));

    Sale s{fmtId("SALE", 5, nextSale_ + 1), productId, qty, p.price * qty, date};
    repo_.recordSale(s, fmtId("T", 5, nextTxn_ + 1));  // throws -> memory untouched
    ++nextSale_; ++nextTxn_;
    p.stock -= qty;
    addStats(s);
    return s;
}

Product InventoryService::restock(const std::string& productId, int qty, std::string date) {
    if (qty <= 0) throw ApiError(400, "quantity must be a positive integer");
    date = resolveDate(date, "date");

    std::unique_lock<std::shared_mutex> lk(mtx_);
    auto it = products_.find(productId);
    if (it == products_.end()) throw ApiError(404, "Product not found: " + productId);
    repo_.recordRestock(productId, qty, date, fmtId("T", 5, nextTxn_ + 1));
    ++nextTxn_;
    it->second.stock += qty;
    return it->second;
}

std::vector<Sale> InventoryService::listSales(const std::string& from, const std::string& to,
                                              const std::string& productId, int limit) const {
    if (!from.empty() && !validDate(from)) throw ApiError(400, "from must be YYYY-MM-DD");
    if (!to.empty() && !validDate(to)) throw ApiError(400, "to must be YYYY-MM-DD");
    return repo_.listSales(from, to, productId, limit);
}

std::vector<Txn> InventoryService::listTransactions(const std::string& productId, int limit) const {
    return repo_.listTransactions(productId, limit);
}

// ---------- analytics ----------

Summary InventoryService::summary() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    Summary s;
    s.products = products_.size();
    s.suppliers = suppliers_.size();
    for (const auto& kv : products_) {
        const Product& p = kv.second;
        if (p.stock < p.minStock) ++s.lowStock;
        s.inventoryValue += p.price * p.stock;
    }
    for (const auto& kv : stats_) { s.unitsSold += kv.second.units; s.revenue += kv.second.revenue; }
    return s;
}

// Top-K with a size-K min-heap: O(n log k).
std::vector<TopProduct> InventoryService::topProducts(int k, bool byRevenue) const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    using Item = std::pair<double, std::string>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> heap;
    for (const auto& kv : stats_) {
        double metric = byRevenue ? kv.second.revenue : static_cast<double>(kv.second.units);
        heap.push({metric, kv.first});
        if (static_cast<int>(heap.size()) > k) heap.pop();
    }
    std::vector<TopProduct> out;
    while (!heap.empty()) {
        const std::string id = heap.top().second;
        heap.pop();
        auto pit = products_.find(id);
        if (pit == products_.end()) continue;
        out.push_back({pit->second, stats_.at(id).units, stats_.at(id).revenue});
    }
    std::reverse(out.begin(), out.end());  // best first
    return out;
}

std::map<std::string, double> InventoryService::revenueByMonth() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    return revenueByMonth_;
}

std::map<std::string, double> InventoryService::revenueByCategory() const {
    std::shared_lock<std::shared_mutex> lk(mtx_);
    std::map<std::string, double> out;
    for (const auto& kv : stats_) {
        auto pit = products_.find(kv.first);
        if (pit != products_.end()) out[pit->second.category] += kv.second.revenue;
    }
    return out;
}

// Heuristic: restock back up to 2x the minimum level.
std::vector<ReorderSuggestion> InventoryService::reorderSuggestions() const {
    std::vector<ReorderSuggestion> out;
    for (const auto& p : lowStock()) out.push_back({p, std::max(1, p.minStock * 2 - p.stock)});
    return out;
}
