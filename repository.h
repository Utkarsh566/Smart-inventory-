#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "db.h"
#include "models.h"

// All SQL lives here. Every public method takes the mutex, so the single
// SQLite connection is safe to use from Crow's worker threads.
class Repository {
public:
    explicit Repository(const std::string& dbPath) : db_(dbPath) {}

    void createSchema();
    bool productsEmpty();
    void importCsv(const std::string& dataDir);  // suppliers, products, sales, transactions

    std::vector<Supplier> allSuppliers();
    std::vector<Product> allProducts();
    std::vector<Sale> allSales();
    int maxIdNumber(const std::string& table, const std::string& column);

    void insertSupplier(const Supplier& s);
    void insertProduct(const Product& p);
    void updateProduct(const Product& p);  // never touches stock
    bool deleteProduct(const std::string& id);

    // Atomic: sale row + OUT transaction + stock decrement.
    void recordSale(const Sale& s, const std::string& txnId);
    // Atomic: IN transaction + stock increment.
    void recordRestock(const std::string& productId, int qty, const std::string& date,
                       const std::string& txnId);

    std::vector<Sale> listSales(const std::string& from, const std::string& to,
                                const std::string& productId, int limit);
    std::vector<Txn> listTransactions(const std::string& productId, int limit);

    std::optional<User> findUser(const std::string& username);
    void insertUser(const User& u);
    int userCount();

private:
    Database db_;
    std::mutex m_;
};
