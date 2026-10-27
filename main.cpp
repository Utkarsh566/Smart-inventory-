#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <string>

#include "auth.h"
#include "crow.h"
#include "crow/middlewares/cors.h"
#include "inventory_service.h"
#include "json_util.h"
#include "repository.h"

namespace {

crow::response jsonResponse(int status, const json& body) {
    crow::response r(status, body.dump());
    r.set_header("Content-Type", "application/json");
    return r;
}
crow::response errorResponse(int status, const std::string& msg) {
    return jsonResponse(status, {{"error", msg}});
}

std::string bearerToken(const crow::request& req) {
    std::string h = req.get_header_value("Authorization");
    const std::string prefix = "Bearer ";
    return h.compare(0, prefix.size(), prefix) == 0 ? h.substr(prefix.size()) : "";
}

std::string param(const crow::request& req, const char* name) {
    const char* v = req.url_params.get(name);
    return v ? v : "";
}

int intParam(const crow::request& req, const char* name, int def, int lo, int hi) {
    std::string v = param(req, name);
    if (v.empty()) return def;
    try {
        size_t pos = 0;
        int n = std::stoi(v, &pos);
        if (pos != v.size()) throw std::invalid_argument("x");
        return std::max(lo, std::min(hi, n));
    } catch (...) {
        throw ApiError(400, std::string(name) + " must be an integer");
    }
}

std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
            std::isxdigit((unsigned char)s[i + 2])) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

json parseBody(const crow::request& req) {
    json j = json::parse(req.body, nullptr, /*allow_exceptions=*/true);
    if (!j.is_object()) throw ApiError(400, "JSON body must be an object");
    return j;
}

Product productFromJson(const json& j, const Product& base) {
    Product p = base;
    p.name = j.value("name", base.name);
    p.category = j.value("category", base.category);
    p.price = j.value("price", base.price);
    p.stock = j.value("stock", base.stock);
    p.minStock = j.value("minimumStock", base.minStock);
    p.supplierId = j.value("supplierId", base.supplierId);
    return p;
}

std::string argOr(int argc, char** argv, const std::string& flag, const std::string& def) {
    for (int i = 1; i + 1 < argc; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return def;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dataDir = argOr(argc, argv, "--data", "data");
    const std::string dbPath = argOr(argc, argv, "--db", "inventory.db");
    const int port = std::stoi(argOr(argc, argv, "--port", "18080"));

    try {
        Repository repo(dbPath);
        repo.createSchema();
        if (repo.productsEmpty()) {
            std::cout << "[init] Empty database - importing CSVs from '" << dataDir << "'\n";
            repo.importCsv(dataDir);
        }
        InventoryService inventory(repo);
        inventory.load();
        auth::AuthService authSvc(repo);
        authSvc.seedAdminIfEmpty();

        crow::App<crow::CORSHandler> app;
        app.get_middleware<crow::CORSHandler>()
            .global()
            .origin("*")
            .headers("Content-Type", "Authorization")
            .methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST, crow::HTTPMethod::PUT,
                     crow::HTTPMethod::DELETE);

        // Runs auth + error mapping around every handler. role: "" = public, "user" = any
        // logged-in account, "admin" = admin only.
        auto handle = [&](const crow::request& req, const std::string& role, auto fn) -> crow::response {
            try {
                if (!role.empty()) {
                    auto session = authSvc.validate(bearerToken(req));
                    if (!session) return errorResponse(401, "Missing or invalid token");
                    if (role == "admin" && session->role != "admin")
                        return errorResponse(403, "Admin role required");
                }
                return fn();
            } catch (const ApiError& e) {
                return errorResponse(e.status, e.what());
            } catch (const json::exception& e) {
                return errorResponse(400, std::string("Invalid JSON: ") + e.what());
            } catch (const std::exception& e) {
                std::cerr << "[error] " << e.what() << "\n";
                return errorResponse(500, "Internal server error");
            }
        };

        // ---------- public ----------
        CROW_ROUTE(app, "/api/health")([&](const crow::request& req) {
            return handle(req, "", [&] { return jsonResponse(200, {{"status", "ok"}}); });
        });

        CROW_ROUTE(app, "/api/auth/login").methods(crow::HTTPMethod::POST)([&](const crow::request& req) {
            return handle(req, "", [&] {
                json j = parseBody(req);
                auto res = authSvc.login(j.value("username", ""), j.value("password", ""));
                if (!res) return errorResponse(401, "Invalid username or password");
                return jsonResponse(200, {{"token", res->first}, {"role", res->second}, {"expiresInHours", 8}});
            });
        });

        // ---------- auth ----------
        CROW_ROUTE(app, "/api/auth/register").methods(crow::HTTPMethod::POST)([&](const crow::request& req) {
            return handle(req, "admin", [&] {
                json j = parseBody(req);
                authSvc.createUser(j.value("username", ""), j.value("password", ""), j.value("role", "staff"));
                return jsonResponse(201, {{"username", j.value("username", "")}, {"role", j.value("role", "staff")}});
            });
        });

        // ---------- products ----------
        CROW_ROUTE(app, "/api/products").methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)(
            [&](const crow::request& req) {
                const bool isPost = req.method == crow::HTTPMethod::POST;
                return handle(req, isPost ? "admin" : "user", [&] {
                    if (isPost) {
                        Product p = productFromJson(parseBody(req), Product{});
                        return jsonResponse(201, toJson(inventory.createProduct(p)));
                    }
                    return jsonResponse(200, toJsonArray(inventory.listProducts(
                                                 param(req, "category"), param(req, "supplierId"),
                                                 param(req, "q"))));
                });
            });

        CROW_ROUTE(app, "/api/products/low-stock")([&](const crow::request& req) {
            return handle(req, "user", [&] { return jsonResponse(200, toJsonArray(inventory.lowStock())); });
        });

        CROW_ROUTE(app, "/api/products/category/<string>")([&](const crow::request& req, const std::string& cat) {
            return handle(req, "user", [&] {
                return jsonResponse(200, toJsonArray(inventory.listProducts(urlDecode(cat), "", "")));
            });
        });

        CROW_ROUTE(app, "/api/products/<string>")
            .methods(crow::HTTPMethod::GET, crow::HTTPMethod::PUT, crow::HTTPMethod::DELETE)(
                [&](const crow::request& req, const std::string& id) {
                    const bool write = req.method != crow::HTTPMethod::GET;
                    return handle(req, write ? "admin" : "user", [&] {
                        if (req.method == crow::HTTPMethod::DELETE) {
                            inventory.deleteProduct(id);
                            return crow::response(204);
                        }
                        if (req.method == crow::HTTPMethod::PUT) {
                            Product merged = productFromJson(parseBody(req), inventory.getProduct(id));
                            return jsonResponse(200, toJson(inventory.updateProduct(id, merged)));
                        }
                        return jsonResponse(200, toJson(inventory.getProduct(id)));
                    });
                });

        // ---------- suppliers ----------
        CROW_ROUTE(app, "/api/suppliers").methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)(
            [&](const crow::request& req) {
                const bool isPost = req.method == crow::HTTPMethod::POST;
                return handle(req, isPost ? "admin" : "user", [&] {
                    if (isPost) {
                        json j = parseBody(req);
                        Supplier s{"", j.value("name", ""), j.value("email", ""), j.value("phone", ""),
                                   j.value("address", "")};
                        return jsonResponse(201, toJson(inventory.createSupplier(s)));
                    }
                    return jsonResponse(200, toJsonArray(inventory.listSuppliers()));
                });
            });

        CROW_ROUTE(app, "/api/suppliers/<string>")([&](const crow::request& req, const std::string& id) {
            return handle(req, "user", [&] { return jsonResponse(200, toJson(inventory.getSupplier(id))); });
        });

        CROW_ROUTE(app, "/api/suppliers/<string>/products")([&](const crow::request& req, const std::string& id) {
            return handle(req, "user", [&] { return jsonResponse(200, toJsonArray(inventory.productsOfSupplier(id))); });
        });

        // ---------- sales ----------
        CROW_ROUTE(app, "/api/sales").methods(crow::HTTPMethod::GET, crow::HTTPMethod::POST)(
            [&](const crow::request& req) {
                return handle(req, "user", [&] {
                    if (req.method == crow::HTTPMethod::POST) {
                        json j = parseBody(req);
                        Sale s = inventory.recordSale(j.value("productId", ""), j.value("quantity", 0),
                                                      j.value("saleDate", ""));
                        return jsonResponse(201, toJson(s));
                    }
                    int limit = intParam(req, "limit", 100, 1, 1000);
                    return jsonResponse(200, toJsonArray(inventory.listSales(param(req, "from"), param(req, "to"),
                                                                             param(req, "productId"), limit)));
                });
            });

        // ---------- inventory movements ----------
        CROW_ROUTE(app, "/api/inventory/transactions")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                int limit = intParam(req, "limit", 100, 1, 1000);
                return jsonResponse(200, toJsonArray(inventory.listTransactions(param(req, "productId"), limit)));
            });
        });

        CROW_ROUTE(app, "/api/inventory/restock").methods(crow::HTTPMethod::POST)([&](const crow::request& req) {
            return handle(req, "user", [&] {
                json j = parseBody(req);
                Product p = inventory.restock(j.value("productId", ""), j.value("quantity", 0), j.value("date", ""));
                return jsonResponse(200, toJson(p));
            });
        });

        // ---------- analytics ----------
        CROW_ROUTE(app, "/api/analytics/summary")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                Summary s = inventory.summary();
                return jsonResponse(200, {{"products", s.products}, {"suppliers", s.suppliers},
                                          {"lowStockProducts", s.lowStock}, {"unitsSold", s.unitsSold},
                                          {"totalRevenue", s.revenue}, {"inventoryValue", s.inventoryValue}});
            });
        });

        CROW_ROUTE(app, "/api/analytics/top-products")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                int k = intParam(req, "k", 5, 1, 100);
                bool byRevenue = param(req, "by") != "units";
                json arr = json::array();
                for (const auto& t : inventory.topProducts(k, byRevenue)) {
                    json o = toJson(t.product);
                    o["unitsSold"] = t.units;
                    o["revenue"] = t.revenue;
                    arr.push_back(o);
                }
                return jsonResponse(200, arr);
            });
        });

        CROW_ROUTE(app, "/api/analytics/revenue-by-month")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                json arr = json::array();
                for (const auto& kv : inventory.revenueByMonth()) arr.push_back({{"month", kv.first}, {"revenue", kv.second}});
                return jsonResponse(200, arr);
            });
        });

        CROW_ROUTE(app, "/api/analytics/revenue-by-category")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                json arr = json::array();
                for (const auto& kv : inventory.revenueByCategory()) arr.push_back({{"category", kv.first}, {"revenue", kv.second}});
                return jsonResponse(200, arr);
            });
        });

        CROW_ROUTE(app, "/api/analytics/reorder-suggestions")([&](const crow::request& req) {
            return handle(req, "user", [&] {
                json arr = json::array();
                for (const auto& r : inventory.reorderSuggestions()) {
                    json o = toJson(r.product);
                    o["suggestedQuantity"] = r.suggestedQty;
                    arr.push_back(o);
                }
                return jsonResponse(200, arr);
            });
        });

        std::cout << "[server] listening on http://localhost:" << port << "\n";
        app.port(static_cast<uint16_t>(port)).multithreaded().run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
