# Inventory Backend (C++17 · Crow · SQLite)

REST API for products, suppliers, sales and stock movements, with token auth.
On first start it creates `inventory.db` and imports the four CSVs in `data/`.

## Build on Windows

1. Install **Visual Studio 2022** (or Build Tools) with the "Desktop development with C++" workload, and CMake.
2. Install vcpkg once and point `VCPKG_ROOT` at it:
   ```powershell
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat
   setx VCPKG_ROOT C:\vcpkg      # then open a NEW terminal
   ```
3. From this folder (dependencies in `vcpkg.json` install automatically on first configure):
   ```powershell
   cmake --preset default
   cmake --build build --config Release
   ```
4. Run from the **project root** so `data/` is found:
   ```powershell
   .\build\Release\inventory_server.exe            # --port 18080 --db inventory.db --data data
   .\build\Release\core_tests.exe data             # unit/integration tests, no HTTP needed
   ```

## Auth

First run creates user `admin` (password `admin123`, or the `INVENTORY_ADMIN_PASSWORD` env var). Change it.
Send `Authorization: Bearer <token>` on every call except `/api/health` and `/api/auth/login`.
`admin` can create/update/delete products and suppliers and register users; `staff` can read, sell and restock.

```powershell
$r = Invoke-RestMethod -Method Post http://localhost:18080/api/auth/login -ContentType application/json `
     -Body '{"username":"admin","password":"admin123"}'
$h = @{ Authorization = "Bearer $($r.token)" }
Invoke-RestMethod http://localhost:18080/api/products/P001 -Headers $h
Invoke-RestMethod -Method Post http://localhost:18080/api/sales -Headers $h -ContentType application/json `
     -Body '{"productId":"P001","quantity":2}'
```

## Endpoints

| Method | Path | Notes |
|---|---|---|
| POST | `/api/auth/login` | `{username,password}` -> `{token,role}` |
| POST | `/api/auth/register` | admin; `{username,password,role}` |
| GET / POST | `/api/products` | filters: `?category=&supplierId=&q=` (q = name-prefix search) |
| GET / PUT / DELETE | `/api/products/{id}` | PUT ignores `stock` (stock moves only via sales/restock); DELETE -> 409 if history exists |
| GET | `/api/products/low-stock` | most critical first (min-heap) |
| GET | `/api/products/category/{name}` | |
| GET / POST | `/api/suppliers` | |
| GET | `/api/suppliers/{id}`, `/api/suppliers/{id}/products` | |
| GET / POST | `/api/sales` | GET: `?from=&to=&productId=&limit=`; POST: `{productId,quantity,saleDate?}` -> 409 if not enough stock |
| GET | `/api/inventory/transactions` | `?productId=&limit=` |
| POST | `/api/inventory/restock` | `{productId,quantity,date?}` |
| GET | `/api/analytics/summary`, `top-products?k=&by=revenue\|units`, `revenue-by-month`, `revenue-by-category`, `reorder-suggestions` | |

## Design

```
controllers (main.cpp)  ->  InventoryService  ->  Repository  ->  SQLite
                            (rules + indexes)     (all SQL)
```
- **Write-through:** every write commits to SQLite first (sale + OUT txn + stock update in one transaction), then updates memory.
- **Indexes (rebuilt from SQLite at startup):** `unordered_map` by id, by category, by supplier; a trie for name search; per-product sales stats; ordered monthly revenue map; heaps for low-stock and top-K.
- `products.stock` is the source of truth. The CSV transactions have no opening balance, so stock cannot be recomputed from them.
- Reorder suggestion = restock to 2x `minimumStock` (simple heuristic; change in `reorderSuggestions()`).
