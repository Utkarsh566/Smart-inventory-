#include "repository.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>

#include "csv.h"

namespace {

int toInt(const std::string& s, const std::string& ctx) {
    try {
        size_t pos = 0;
        int v = std::stoi(s, &pos);
        if (pos != s.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (...) {
        throw std::runtime_error(ctx + ": invalid integer '" + s + "'");
    }
}

double toDouble(const std::string& s, const std::string& ctx) {
    try {
        size_t pos = 0;
        double v = std::stod(s, &pos);
        if (pos != s.size()) throw std::invalid_argument("trailing");
        return v;
    } catch (...) {
        throw std::runtime_error(ctx + ": invalid number '" + s + "'");
    }
}

// Reads a CSV, checks the header, and calls fn for every data row.
void forEachRow(const std::string& dir, const std::string& file,
                const std::vector<std::string>& header,
                const std::function<void(const std::vector<std::string>&, const std::string&)>& fn) {
    const std::string path = (std::filesystem::path(dir) / file).string();
    auto rows = readCsv(path);
    if (rows.empty() || rows[0] != header)
        throw std::runtime_error(file + ": unexpected or missing header row");
    for (size_t i = 1; i < rows.size(); ++i) {
        if (rows[i].size() != header.size())
            throw std::runtime_error(file + " line " + std::to_string(i + 1) + ": expected " +
                                     std::to_string(header.size()) + " columns");
        fn(rows[i], file + " line " + std::to_string(i + 1));
    }
}

}  // namespace

void Repository::createSchema() {
    std::lock_guard<std::mutex> lk(m_);
    db_.exec(R"sql(
CREATE TABLE IF NOT EXISTS suppliers (
    supplier_id TEXT PRIMARY KEY,
    name        TEXT NOT NULL,
    email       TEXT,
    phone       TEXT,
    address     TEXT
);
CREATE TABLE IF NOT EXISTS products (
    product_id    TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    category      TEXT NOT NULL,
    price         REAL NOT NULL CHECK (price >= 0),
    stock         INTEGER NOT NULL CHECK (stock >= 0),
    minimum_stock INTEGER NOT NULL CHECK (minimum_stock >= 0),
    supplier_id   TEXT NOT NULL REFERENCES suppliers(supplier_id)
);
CREATE TABLE IF NOT EXISTS sales (
    sale_id      TEXT PRIMARY KEY,
    product_id   TEXT NOT NULL REFERENCES products(product_id),
    quantity     INTEGER NOT NULL CHECK (quantity > 0),
    total_amount REAL NOT NULL,
    sale_date    TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS inventory_transactions (
    transaction_id TEXT PRIMARY KEY,
    product_id     TEXT NOT NULL REFERENCES products(product_id),
    type           TEXT NOT NULL CHECK (type IN ('IN', 'OUT')),
    quantity       INTEGER NOT NULL CHECK (quantity > 0),
    txn_date       TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS users (
    username      TEXT PRIMARY KEY,
    password_hash TEXT NOT NULL,
    role          TEXT NOT NULL CHECK (role IN ('admin', 'staff'))
);
CREATE INDEX IF NOT EXISTS idx_sales_product_date ON sales(product_id, sale_date);
CREATE INDEX IF NOT EXISTS idx_sales_date         ON sales(sale_date);
CREATE INDEX IF NOT EXISTS idx_txn_product_date   ON inventory_transactions(product_id, txn_date);
CREATE INDEX IF NOT EXISTS idx_products_category  ON products(category);
CREATE INDEX IF NOT EXISTS idx_products_supplier  ON products(supplier_id);
)sql");
}

bool Repository::productsEmpty() {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "SELECT COUNT(*) FROM products");
    st.step();
    return st.integer(0) == 0;
}

void Repository::importCsv(const std::string& dir) {
    std::lock_guard<std::mutex> lk(m_);
    Transaction tx(db_);  // all-or-nothing import

    {
        Statement st(db_, "INSERT INTO suppliers VALUES (?,?,?,?,?)");
        forEachRow(dir, "suppliers.csv", {"supplierId", "name", "email", "phone", "address"},
                   [&](const auto& r, const std::string&) {
                       for (int i = 0; i < 5; ++i) st.bind(i + 1, r[i]);
                       st.step(); st.reset();
                   });
    }
    {
        Statement st(db_, "INSERT INTO products VALUES (?,?,?,?,?,?,?)");
        forEachRow(dir, "products.csv",
                   {"productId", "name", "category", "price", "stock", "minimumStock", "supplierId"},
                   [&](const auto& r, const std::string& ctx) {
                       st.bind(1, r[0]).bind(2, r[1]).bind(3, r[2])
                         .bind(4, toDouble(r[3], ctx)).bind(5, toInt(r[4], ctx))
                         .bind(6, toInt(r[5], ctx)).bind(7, r[6]);
                       st.step(); st.reset();
                   });
    }
    {
        Statement st(db_, "INSERT INTO sales VALUES (?,?,?,?,?)");
        forEachRow(dir, "sales.csv", {"saleId", "productId", "quantity", "totalAmount", "saleDate"},
                   [&](const auto& r, const std::string& ctx) {
                       st.bind(1, r[0]).bind(2, r[1]).bind(3, toInt(r[2], ctx))
                         .bind(4, toDouble(r[3], ctx)).bind(5, r[4]);
                       st.step(); st.reset();
                   });
    }
    {
        Statement st(db_, "INSERT INTO inventory_transactions VALUES (?,?,?,?,?)");
        forEachRow(dir, "inventory_transactions.csv",
                   {"transactionId", "productId", "type", "quantity", "date"},
                   [&](const auto& r, const std::string& ctx) {
                       st.bind(1, r[0]).bind(2, r[1]).bind(3, r[2]).bind(4, toInt(r[3], ctx))
                         .bind(5, r[4]);
                       st.step(); st.reset();
                   });
    }
    tx.commit();
}

std::vector<Supplier> Repository::allSuppliers() {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "SELECT supplier_id,name,email,phone,address FROM suppliers ORDER BY supplier_id");
    std::vector<Supplier> out;
    while (st.step())
        out.push_back({st.text(0), st.text(1), st.text(2), st.text(3), st.text(4)});
    return out;
}

std::vector<Product> Repository::allProducts() {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_,
        "SELECT product_id,name,category,price,stock,minimum_stock,supplier_id "
        "FROM products ORDER BY product_id");
    std::vector<Product> out;
    while (st.step())
        out.push_back({st.text(0), st.text(1), st.text(2), st.real(3), st.integer(4),
                       st.integer(5), st.text(6)});
    return out;
}

std::vector<Sale> Repository::allSales() {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "SELECT sale_id,product_id,quantity,total_amount,sale_date FROM sales");
    std::vector<Sale> out;
    while (st.step())
        out.push_back({st.text(0), st.text(1), st.integer(2), st.real(3), st.text(4)});
    return out;
}

int Repository::maxIdNumber(const std::string& table, const std::string& column) {
    std::lock_guard<std::mutex> lk(m_);
    // Table/column names are internal constants, never user input.
    Statement st(db_, "SELECT " + column + " FROM " + table);
    int best = 0;
    while (st.step()) {
        std::string id = st.text(0);
        size_t i = id.size();
        while (i > 0 && std::isdigit(static_cast<unsigned char>(id[i - 1]))) --i;
        if (i < id.size()) {
            try { best = std::max(best, std::stoi(id.substr(i))); } catch (...) {}
        }
    }
    return best;
}

void Repository::insertSupplier(const Supplier& s) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "INSERT INTO suppliers VALUES (?,?,?,?,?)");
    st.bind(1, s.id).bind(2, s.name).bind(3, s.email).bind(4, s.phone).bind(5, s.address);
    st.step();
}

void Repository::insertProduct(const Product& p) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "INSERT INTO products VALUES (?,?,?,?,?,?,?)");
    st.bind(1, p.id).bind(2, p.name).bind(3, p.category).bind(4, p.price).bind(5, p.stock)
      .bind(6, p.minStock).bind(7, p.supplierId);
    st.step();
}

void Repository::updateProduct(const Product& p) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_,
        "UPDATE products SET name=?, category=?, price=?, minimum_stock=?, supplier_id=? "
        "WHERE product_id=?");
    st.bind(1, p.name).bind(2, p.category).bind(3, p.price).bind(4, p.minStock)
      .bind(5, p.supplierId).bind(6, p.id);
    st.step();
}

bool Repository::deleteProduct(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "DELETE FROM products WHERE product_id=?");
    st.bind(1, id);
    st.step();
    return db_.changes() == 1;
}

void Repository::recordSale(const Sale& s, const std::string& txnId) {
    std::lock_guard<std::mutex> lk(m_);
    Transaction tx(db_);
    {
        Statement st(db_, "INSERT INTO sales VALUES (?,?,?,?,?)");
        st.bind(1, s.id).bind(2, s.productId).bind(3, s.quantity).bind(4, s.total).bind(5, s.date);
        st.step();
    }
    {
        Statement st(db_, "INSERT INTO inventory_transactions VALUES (?,?,?,?,?)");
        st.bind(1, txnId).bind(2, s.productId).bind(3, std::string("OUT")).bind(4, s.quantity)
          .bind(5, s.date);
        st.step();
    }
    {
        // The stock >= ? guard makes overselling impossible even under races.
        Statement st(db_, "UPDATE products SET stock = stock - ? WHERE product_id=? AND stock >= ?");
        st.bind(1, s.quantity).bind(2, s.productId).bind(3, s.quantity);
        st.step();
        if (db_.changes() != 1) throw std::runtime_error("insufficient stock (database guard)");
    }
    tx.commit();
}

void Repository::recordRestock(const std::string& productId, int qty, const std::string& date,
                               const std::string& txnId) {
    std::lock_guard<std::mutex> lk(m_);
    Transaction tx(db_);
    {
        Statement st(db_, "INSERT INTO inventory_transactions VALUES (?,?,?,?,?)");
        st.bind(1, txnId).bind(2, productId).bind(3, std::string("IN")).bind(4, qty).bind(5, date);
        st.step();
    }
    {
        Statement st(db_, "UPDATE products SET stock = stock + ? WHERE product_id=?");
        st.bind(1, qty).bind(2, productId);
        st.step();
        if (db_.changes() != 1) throw std::runtime_error("restock update failed");
    }
    tx.commit();
}

std::vector<Sale> Repository::listSales(const std::string& from, const std::string& to,
                                        const std::string& productId, int limit) {
    std::lock_guard<std::mutex> lk(m_);
    std::string sql = "SELECT sale_id,product_id,quantity,total_amount,sale_date FROM sales WHERE 1=1";
    if (!from.empty()) sql += " AND sale_date >= ?";
    if (!to.empty()) sql += " AND sale_date <= ?";
    if (!productId.empty()) sql += " AND product_id = ?";
    sql += " ORDER BY sale_date DESC, sale_id DESC LIMIT ?";
    Statement st(db_, sql);
    int i = 1;
    if (!from.empty()) st.bind(i++, from);
    if (!to.empty()) st.bind(i++, to);
    if (!productId.empty()) st.bind(i++, productId);
    st.bind(i, limit);
    std::vector<Sale> out;
    while (st.step())
        out.push_back({st.text(0), st.text(1), st.integer(2), st.real(3), st.text(4)});
    return out;
}

std::vector<Txn> Repository::listTransactions(const std::string& productId, int limit) {
    std::lock_guard<std::mutex> lk(m_);
    std::string sql = "SELECT transaction_id,product_id,type,quantity,txn_date FROM inventory_transactions";
    if (!productId.empty()) sql += " WHERE product_id = ?";
    sql += " ORDER BY txn_date DESC, transaction_id DESC LIMIT ?";
    Statement st(db_, sql);
    int i = 1;
    if (!productId.empty()) st.bind(i++, productId);
    st.bind(i, limit);
    std::vector<Txn> out;
    while (st.step())
        out.push_back({st.text(0), st.text(1), st.text(2), st.integer(3), st.text(4)});
    return out;
}

std::optional<User> Repository::findUser(const std::string& username) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "SELECT username,password_hash,role FROM users WHERE username=?");
    st.bind(1, username);
    if (!st.step()) return std::nullopt;
    return User{st.text(0), st.text(1), st.text(2)};
}

void Repository::insertUser(const User& u) {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "INSERT INTO users VALUES (?,?,?)");
    st.bind(1, u.username).bind(2, u.passwordHash).bind(3, u.role);
    st.step();
}

int Repository::userCount() {
    std::lock_guard<std::mutex> lk(m_);
    Statement st(db_, "SELECT COUNT(*) FROM users");
    st.step();
    return st.integer(0);
}
