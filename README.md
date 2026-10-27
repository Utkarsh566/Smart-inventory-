Smart Inventory & Demand Forecasting System
It is a complete web application that stores products, suppliers, sales and stock movements in MongoDB, forecasts the demand of every product with a Python / scikit-learn model, and turns the forecast into clear reorder recommendations.

All data shown in the application comes from MongoDB (loaded from the four CSV datasets in data/) and from the Python ML pipeline. Nothing on the dashboard is hard-coded.

1. Features
Area	What it does
Authentication	Register, login, bcrypt password hashing, JWT-protected APIs, logout, admin/staff roles
Products	List, search, filter (category, stock status, supplier), sort, paginate, add, edit, delete, low-stock indicator
Suppliers	CRUD, number of supplied products, view a supplier's products
Sales	Sales history with filters; recording a sale reduces stock and creates an OUT inventory transaction in one MongoDB transaction
Inventory	IN/OUT transaction history, filters (product, type, date range), IN vs OUT chart, movement summary
Forecast	Python ML pipeline: 3 models compared, chronological split, MAE / RMSE / MAPE, 7-day and 30-day forecasts per product
Reorder	Transparent formula using forecast + safety stock − current stock, a written reason for every product, purchase orders
Analytics	Sales trends, product/category performance, inventory movement, stock health, supplier relationships, model performance
AI Copilot	Authenticated chat that explains live stock, sales, forecast, and reorder data using the Gemini API
Dashboard	Live cards and charts (sales, demand history vs forecast, IN vs OUT, categories, low-stock table)
SDG & Sustainability	UN 2030 Agenda integration (SDG 12, 9, 13, 8): live eco-efficiency score, waste diverted metrics, interactive "What-If" simulator, operational compliance checklist, and exportable report
Theme & Interactive UI	Modern creative design system with Light and Dark mode toggle switch, system preference sync, theme-aware Recharts palettes, and glassmorphic micro-interactions
2. Architecture
                 ┌──────────────────────┐
                 │   React Frontend     │   http://localhost:5173
                 │   (Vite, Recharts)   │
                 └──────────┬───────────┘
                            │ REST API (JSON + JWT)
                            ▼
                 ┌──────────────────────┐
                 │  Node.js + Express   │   http://localhost:5000/api
                 │  auth, business      │
                 │  logic, reorder, AI  │
                 └───────┬───────┬──────┘
                         │       │ child_process.spawn()  →  JSON on stdout
                         │       ▼
                         │  ┌──────────────────────┐
                         │  │ Python ML            │  backend/ml/forecast.py
                         │  │ pandas, scikit-learn │  (reads sales from MongoDB)
                         │  └──────────────────────┘
                         ▼
                 ┌──────────────────────┐
                 │       MongoDB        │  users, suppliers, products, sales,
                 │                      │  inventory_transactions, forecasts,
                 │                      │  forecast_runs, purchase_orders, counters
                 └──────────────────────┘
React never talks to MongoDB or Python directly. It only calls the Express API. The optional Inventory AI Copilot calls Google Gemini from the backend; its API key is never sent to the browser.

3. Technologies
Frontend: React 18, React Router 6, Axios, Recharts, plain CSS, Vite
Backend: Node.js (18+), Express 4, Mongoose 8, JSON Web Tokens, bcrypt, Helmet, CORS
Database: MongoDB 7 (single-node replica set so transactions work)
Machine learning: Python 3.10+, pandas, NumPy, scikit-learn, pymongo
4. Project structure
smart-inventory/
├── data/                         the four source CSV datasets
│   ├── suppliers.csv  products.csv  sales.csv  inventory_transactions.csv
├── backend/
│   ├── server.js  app.js         start-up and Express app
│   ├── config/                   env.js (environment variables), db.js (MongoDB connection)
│   ├── models/                   Mongoose schemas (one per collection)
│   ├── routes/  controllers/     REST API
│   ├── middleware/               auth.js (JWT), errorHandler.js
│   ├── services/                 stockService, forecastService, mlRunner, reorderService, analyticsService, ...
│   ├── utils/                    transaction helper, ID generator, validators
│   ├── scripts/                  importData, seedAdmin, verifyDb, runForecast, startLocalMongo
│   ├── ml/                       forecast.py, requirements.txt
│   ├── tests/e2e.test.js         end-to-end API tests
│   └── .env.example
├── frontend/
│   └── src/  pages/ components/ charts/ context/ services/ hooks/ utils/  App.jsx
├── docs/screenshots/             screenshots of every page (for the report)
├── README.md  PROJECT_DOCUMENTATION.md  VIVA_GUIDE.md
5. Prerequisites
Node.js 18 or newer (node -v)
Python 3.10 or newer (python3 --version)
MongoDB — any one of:
nothing installed: use the bundled launcher npm run db:start (downloads MongoDB once, no admin rights needed), or
your own local MongoDB, or
a free MongoDB Atlas cluster
6. Installation
cd smart-inventory

# 1. Backend packages
cd backend
npm install

# 2. Python packages (creates backend/ml/.venv)
npm run ml:setup

# 3. Environment file
cp .env.example .env        # then edit .env (see section 7)

# 4. Frontend packages
cd ../frontend
npm install
On Windows, replace step 2 with:

py -m venv ml\.venv
ml\.venv\Scripts\pip install -r ml\requirements.txt
7. Environment variables (backend/.env)
Variable	Meaning	Example
MONGODB_URI	MongoDB connection string including the database name	mongodb://127.0.0.1:27017/smart_inventory?replicaSet=rs0
JWT_SECRET	Long random string used to sign login tokens	generate with the command below
PORT	Port of the API	5000
CORS_ORIGIN	Allowed frontend origin(s), comma separated	http://localhost:5173
JWT_EXPIRES_IN	Login session length	8h
ADMIN_NAME, ADMIN_EMAIL, ADMIN_PASSWORD	The administrator account created by the seed/import	choose your own password (8+ characters)
PYTHON_PATH	Python executable; leave empty to auto-detect ml/.venv	
ML_TIMEOUT_MS	Maximum time allowed for a forecast run	180000
GEMINI_API_KEY	Optional Google AI Studio key for the Inventory AI Copilot; stored only in backend environment	leave blank to disable AI chat
GEMINI_MODEL	Gemini model used by the Copilot	gemini-3.5-flash-lite
SAFETY_Z, LEAD_TIME_DAYS, SAFETY_WINDOW_DAYS	Reorder settings (see PROJECT_DOCUMENTATION.md §20)	1.65, 7, 30
Generate a secret:

node -e "console.log(require('crypto').randomBytes(48).toString('hex'))"
Secrets live only in backend/.env, which is ignored by git. Nothing secret is sent to React. To enable the Inventory AI Copilot, create a Gemini API key in Google AI Studio and add it as GEMINI_API_KEY in backend/.env or your hosting provider's backend environment variables. The configured model uses Google's free tier, subject to account eligibility and rate limits. Free-tier prompts may be used by Google to improve products. Keep the key private and do not commit .env.

8. MongoDB setup
Option A – bundled launcher (easiest, no installation):

cd backend
npm run db:start      # keep this terminal open
This starts MongoDB 7 on 127.0.0.1:27017 as a single-node replica set called rs0, and stores the data in backend/.mongo-data/. The default MONGODB_URI in .env.example already points to it.

Option B – your own MongoDB or Atlas: put its connection string in MONGODB_URI. Multi-document transactions need a replica set (Atlas always is one). On a plain standalone mongod the application still works: it prints a warning and uses compensating writes (undo steps) instead of transactions.

9. Import the four CSV datasets
cd backend
npm run import          # adds missing records; safe to run many times
npm run verify          # checks counts, relationships, duplicate IDs and indexes
Expected result:

suppliers                read     8 | inserted     8 | already existed     0 | invalid   0 | now in DB 8
products                 read    30 | inserted    30 | already existed     0 | invalid   0 | now in DB 30
sales                    read  4800 | inserted  4800 | already existed     0 | invalid   0 | now in DB 4800
inventory_transactions   read  4980 | inserted  4980 | already existed     0 | invalid   0 | now in DB 4980
Records are matched by their stable CSV IDs, so running the import again inserts nothing and never overwrites stock that the application has changed.
Every row is validated; rows with a missing supplier/product, bad date, bad quantity, etc. are rejected and listed with the reason.
The import also creates the admin user from ADMIN_EMAIL / ADMIN_PASSWORD.
npm run import:reset deletes the application data (not the users) and imports again — useful to return to a clean state before a demo.
node scripts/importData.js --dir /other/folder imports CSVs from another folder.
10. Start the backend
cd backend
npm start               # or: npm run dev   (restarts on file changes)
http://localhost:5000/api/health should answer {"status":"ok","database":"connected",...}.

11. Start the frontend
cd frontend
npm run dev
Open http://localhost:5173 and log in with the admin email and password from backend/.env.

12. Run the ML forecast
From the application: open Forecast and press Run forecast (React → Node → Python → Node → React).

From the command line: cd backend && npm run forecast (same thing, stores the result in MongoDB).

Python only, to study the output:

cd backend
ml/.venv/bin/python ml/forecast.py --csv ../data/sales.csv --horizon 30 > forecast.json
A run takes about 3 seconds on this dataset. It compares Linear Regression, Random Forest and Gradient Boosting, picks the one with the lowest validation MAE, and stores 30 products × 30 days = 900 forecast rows.

13. Tests
cd backend
npm test
Runs 65 end-to-end API tests against a separate database (smart_inventory_test, created and dropped automatically): authentication, import, CRUD, sale → stock → OUT transaction, concurrency, rollback, analytics, the real Python forecast, Python failure cases, reorder calculation and purchase orders.

14. API overview
All routes are under /api. Everything except /health, /auth/login and /auth/register needs the header Authorization: Bearer <token>.

Method & path	Purpose
POST /auth/register, POST /auth/login, GET /auth/me	Authentication
GET /dashboard	All dashboard cards and chart data
GET /products ?q=&category=&supplierId=&stockStatus=low|out|ok&sort=&order=&page=&limit=	Product list
GET /products/categories, GET/PUT/DELETE /products/:id, POST /products	Product CRUD
GET /suppliers, GET/PUT/DELETE /suppliers/:id, POST /suppliers, GET /suppliers/analytics	Supplier CRUD + analytics
GET /sales ?productId=&from=&to=&sort=&order=&page=&limit=, GET /sales/:id	Sales history
POST /sales { productId, quantity, saleDate? }	Record a sale (updates stock, creates OUT transaction)
GET /sales/analytics ?from=&to=&groupBy=day|week|month	Sales aggregations
GET /inventory-transactions ?productId=&type=IN|OUT&from=&to=, GET /inventory-transactions/:id	Transaction history
POST /inventory-transactions { productId, type, quantity, date? }	Manual stock movement (updates stock)
GET /inventory/analytics	Inventory aggregations
GET /assistant/status, POST /assistant/chat	Authenticated AI Copilot status and chat; requires GEMINI_API_KEY
POST /forecasts/run	Run the Python ML pipeline and store the forecasts
GET /forecasts ?productId=&horizon=7|30, GET /forecasts/product/:productId, /forecasts/summary, /forecasts/status, /forecasts/runs	Read forecasts and metrics
GET /reorder ?horizon=7|30	Reorder recommendations
GET/POST /purchase-orders, PATCH /purchase-orders/:id/status	Purchase orders
Status codes used: 200, 201, 400 (invalid input), 401 (not logged in), 403 (not allowed), 404, 409 (duplicate ID, insufficient stock, record still in use), 422 (not enough data to forecast), 500, 502/504 (ML process failed / timed out). Errors always look like { "error": { "message": "..." } }.

15. Troubleshooting
Problem	Fix
Backend prints Could not start: ... ECONNREFUSED	MongoDB is not running. Run npm run db:start in another terminal, or check MONGODB_URI.
Missing required environment variable ...	Create backend/.env from .env.example.
JWT_SECRET must be a long random string	Replace the placeholder with a generated secret (section 7).
Login says "Invalid email or password"	Use ADMIN_EMAIL / ADMIN_PASSWORD from backend/.env. If you changed them after the first import, run npm run seed:admin (it creates the user only if that email does not exist yet).
Forecast button: "Python packages are missing"	Run npm run ml:setup in backend/.
Forecast button: "Python could not be started"	Set PYTHON_PATH in .env to your Python executable.
AI Copilot says it is not configured	Add GEMINI_API_KEY to the backend environment, then restart or redeploy the backend.
Frontend: "Cannot reach the server"	Start the backend; check that it runs on the port in frontend/.env → VITE_API_PROXY (default http://localhost:5000).
Warning: "not a replica set"	The app works, but without real transactions. Use npm run db:start or Atlas.
Port already in use	Change PORT in backend/.env (and VITE_API_PROXY to match).
Want the original data back	npm run import:reset in backend/.
See PROJECT_DOCUMENTATION.md for the full report and VIVA_GUIDE.md for viva preparation.
