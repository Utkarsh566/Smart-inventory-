// Exercises the core (CSV, trie, hashing, SQLite import, service rules) without HTTP.
// Usage: core_tests <path-to-data-dir>
#include <cmath>
#include <iostream>

#include "auth.h"
#include "csv.h"
#include "inventory_service.h"
#include "repository.h"
#include "sha256.h"
#include "trie.h"

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) { std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n"; ++failures; } \
    } while (0)

template <class F>
static int httpStatusOf(F f) {
    try { f(); return 200; } catch (const ApiError& e) { return e.status; }
}

int main(int argc, char** argv) {
    const std::string dataDir = argc > 1 ? argv[1] : "data";

    // sha256 known-answer tests
    CHECK(sha::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    // CSV: quotes, embedded commas, escaped quotes, CRLF, BOM
    auto rows = parseCsv("\xEF\xBB\xBF" "a,b\r\n\"x,1\",\"he said \"\"hi\"\"\"\r\n\r\n");
    CHECK(rows.size() == 2 && rows[1][0] == "x,1" && rows[1][1] == "he said \"hi\"");

    // Trie
    Trie t;
    t.insert("mouse", "P1"); t.insert("monitor", "P2");
    CHECK(t.withPrefix("mo").size() == 2 && t.withPrefix("mou").count("P1") == 1);
    t.erase("mouse", "P1");
    CHECK(t.withPrefix("mou").empty());

    // Password hashing
    std::string h = auth::hashPassword("correct horse");
    CHECK(auth::verifyPassword("correct horse", h));
    CHECK(!auth::verifyPassword("wrong", h));

    // Full import + service rules on an in-memory database
    Repository repo(":memory:");
    repo.createSchema();
    CHECK(repo.productsEmpty());
    repo.importCsv(dataDir);
    InventoryService svc(repo);
    svc.load();

    Summary s = svc.summary();
    CHECK(s.products == 30 && s.suppliers == 8);
    CHECK(s.lowStock == svc.lowStock().size());
    CHECK(svc.listSales("", "", "", 100000).size() == 4800);
    CHECK(svc.listTransactions("", 100000).size() == 4980);

    // Stock-ledger rules
    Product before = svc.getProduct("P001");
    Sale sale = svc.recordSale("P001", 1, "2026-07-01");
    CHECK(svc.getProduct("P001").stock == before.stock - 1);
    CHECK(std::fabs(sale.total - before.price) < 1e-9);
    CHECK(sale.id == "SALE04801");
    CHECK(httpStatusOf([&] { svc.recordSale("P001", before.stock + 1000, ""); }) == 409);
    CHECK(httpStatusOf([&] { svc.recordSale("NOPE", 1, ""); }) == 404);
    CHECK(httpStatusOf([&] { svc.recordSale("P001", 0, ""); }) == 400);
    CHECK(httpStatusOf([&] { svc.recordSale("P001", 1, "07/01/2026"); }) == 400);
    CHECK(svc.restock("P001", 5, "2026-07-02").stock == before.stock + 4);

    // Persistence: a fresh service over the same DB sees the same stock
    InventoryService svc2(repo);
    svc2.load();
    CHECK(svc2.getProduct("P001").stock == before.stock + 4);

    // CRUD + indexes
    Product np{"", "Ergonomic Keyboard Pro", "Accessories", 2500, 10, 5, "S001"};
    Product created = svc.createProduct(np);
    CHECK(created.id == "P031");
    CHECK(!svc.listProducts("", "", "ergo key").empty());
    CHECK(httpStatusOf([&] { svc.deleteProduct("P001"); }) == 409);  // has history
    svc.deleteProduct("P031");
    CHECK(svc.listProducts("", "", "ergonomic").empty());
    np.supplierId = "S999";
    CHECK(httpStatusOf([&] { svc.createProduct(np); }) == 400);

    // Analytics
    auto top = svc.topProducts(3, true);
    CHECK(top.size() == 3 && top[0].revenue >= top[1].revenue && top[1].revenue >= top[2].revenue);
    CHECK(!svc.revenueByMonth().empty());

    // Auth flow
    auth::AuthService a(repo);
    a.seedAdminIfEmpty();
    auto login = a.login("admin", "admin123");
    CHECK(login && login->second == "admin" && a.validate(login->first));
    CHECK(!a.login("admin", "bad"));
    CHECK(!a.validate("garbage"));

    std::cout << (failures ? "TESTS FAILED\n" : "ALL TESTS PASSED\n");
    return failures ? 1 : 0;
}
