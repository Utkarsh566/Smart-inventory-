#pragma once
#include <map>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "models.h"
#include "repository.h"
#include "trie.h"

struct ApiError : std::runtime_error {
    int status;
    ApiError(int s, const std::string& m) : std::runtime_error(m), status(s) {}
};

struct ProductStats {
    long long units = 0;
    double revenue = 0;
};
struct TopProduct {
    Product product;
    long long units;
    double revenue;
};
struct ReorderSuggestion {
    Product product;
    int suggestedQty;
};
struct Summary {
    size_t products = 0, suppliers = 0, lowStock = 0;
    long long unitsSold = 0;
    double revenue = 0, inventoryValue = 0;
};

// Business logic + in-memory indexes. SQLite is the source of truth; every
// write goes to SQLite first (write-through) and only then updates the indexes.
class InventoryService {
public:
    explicit InventoryService(Repository& repo) : repo_(repo) {}
    void load();  // rebuild all indexes from SQLite

    // Products
    Product getProduct(const std::string& id) const;
    std::vector<Product> listProducts(const std::string& category, const std::string& supplierId,
                                      const std::string& query) const;
    Product createProduct(Product p);
    Product updateProduct(const std::string& id, const Product& changes);
    void deleteProduct(const std::string& id);
    std::vector<Product> lowStock() const;

    // Suppliers
    std::vector<Supplier> listSuppliers() const;
    Supplier getSupplier(const std::string& id) const;
    Supplier createSupplier(Supplier s);
    std::vector<Product> productsOfSupplier(const std::string& id) const;

    // Sales / stock movements
    Sale recordSale(const std::string& productId, int qty, std::string date);
    Product restock(const std::string& productId, int qty, std::string date);
    std::vector<Sale> listSales(const std::string& from, const std::string& to,
                                const std::string& productId, int limit) const;
    std::vector<Txn> listTransactions(const std::string& productId, int limit) const;

    // Analytics
    Summary summary() const;
    std::vector<TopProduct> topProducts(int k, bool byRevenue) const;
    std::map<std::string, double> revenueByMonth() const;
    std::map<std::string, double> revenueByCategory() const;
    std::vector<ReorderSuggestion> reorderSuggestions() const;

private:
    void indexProduct(const Product& p);
    void unindexProduct(const Product& p);
    void addStats(const Sale& s);
    std::vector<std::string> searchNames(const std::string& q) const;
    void validateProduct(const Product& p) const;  // caller holds a lock

    Repository& repo_;
    mutable std::shared_mutex mtx_;

    std::unordered_map<std::string, Product> products_;      // id -> product      O(1)
    std::unordered_map<std::string, Supplier> suppliers_;    // id -> supplier     O(1)
    std::unordered_map<std::string, std::vector<std::string>> byCategory_;  // lowercase category -> ids
    std::unordered_map<std::string, std::vector<std::string>> bySupplier_;  // supplier id -> ids
    Trie nameIndex_;                                         // name-word prefixes -> ids
    std::unordered_map<std::string, ProductStats> stats_;    // per-product units/revenue
    std::map<std::string, double> revenueByMonth_;           // "YYYY-MM" -> revenue (ordered)

    int nextProduct_ = 0, nextSupplier_ = 0, nextSale_ = 0, nextTxn_ = 0;
};
