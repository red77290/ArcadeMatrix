# ArcadeMatrix Unified OTA Stats & Releases Proxy Worker

Turnkey **Cloudflare Worker + D1 SQLite** microservice serving both **ArcadeMatrix (ESP32)** (`p=esp32`) and **ArcadeMatrix_RPI (Raspberry Pi)** (`p=rpi`).

## Architecture & Privacy-by-Design

- **100% Client-Side WebUI Check-Ins**: Update checks and anonymous check-ins are performed exclusively by the browser WebClient (`data/index.html` / `api/www/js/app.js`) when a user opens the WebUI—never by background tasks on the ESP32 microcontroller.
- **Zero IP / Zero PII**: Client IP headers (`CF-Connecting-IP`, `X-Forwarded-For`, etc.) are never read or stored.
- **Strict RFC 4122 UUID v4 Validation**: Only non-zero, non-default 36-character UUID v4 identifiers (`/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i`) are accepted; nil/fallback UUIDs are strictly rejected.
- **Idempotent Version Migration (`UPSERT`)**: The `devices` table uses `id TEXT PRIMARY KEY`. When a matrix upgrades (e.g. `v4.2.0` → `v4.2.1`) and its WebUI performs an OTA check, `ON CONFLICT(id) DO UPDATE` updates its `version` and `last_seen` in place without ever duplicating the device count.
- **GitHub Releases Edge Cache**: Proxies `red77290/ArcadeMatrix` and `red77290/ArcadeMatrix_RPI` releases with a 10-minute Cloudflare Edge cache (`Cache-Control: public, max-age=600`), preventing GitHub API rate-limiting (`60 req/hr/IP`).

---

## Automated 1-Click Deployment (GitHub Actions or CLI)

You do **not** need to manually edit `database_id` in `wrangler.toml`: [`deploy_worker.mjs`](deploy_worker.mjs) uses the Cloudflare REST API v4 directly (zero npm install required) to provision storage (`arcadematrix-ota-db` D1 when `D1: Edit` is granted, or the embedded `TelemetryStore` SQLite Durable Object with standard `Workers Scripts: Edit` permission), register the `workers.dev` route, configure the nightly cron, and deploy the Worker.

### Option A — GitHub Actions (Recommended)
1. In `red77290/ArcadeMatrix`, go to **Settings → Secrets and variables → Actions**.
2. Add `CLOUDFLARE_API_TOKEN` (Cloudflare **"Edit Cloudflare Workers"** API Token with **Account → Workers Scripts → Edit**, plus optional **Account → D1 → Edit**).
3. Trigger **Actions → Deploy OTA Stats Cloudflare Worker → Run workflow** (also runs automatically on pushes to `main` touching `tools/ota-stats-worker/`).

### Option B — Single Local Command
```bash
CLOUDFLARE_API_TOKEN="<your-token>" node tools/ota-stats-worker/deploy_worker.mjs
```

### Canonical Worker URL Constant
The canonical endpoint pre-configured across `ArcadeMatrix` (ESP32 WebClient & Portal) and `ArcadeMatrix_RPI` (RPi WebClient, OTA API & Portal) is:
```text
https://arcadematrix-ota-stats.red77290.workers.dev
```
If your Cloudflare Workers `*.workers.dev` subdomain differs, update `WORKER_URL` in `src/index.js` and the matching `OTA_STATS_WORKER_URL` / `OTA_WORKER_URL` constants in the portals/WebUIs.

---

## Endpoints

### 1. `GET /v1/ota/releases`
Used by ESP32 and Raspberry Pi matrices when checking for OTA updates:
```text
GET /v1/ota/releases?p=esp32|rpi&id=<uuid-v4>&v=<version>&hw=<hw>&os=<os>&res=<WxH>
```
- Asynchronously records/updates the anonymous device check-in in D1 (`ctx.waitUntil`) and refreshes today's `daily_history` snapshot.
- Returns the upstream GitHub Releases JSON array with `Access-Control-Allow-Origin: *`.

### 2. `GET /v1/stats`
Used by the ESP32 and Raspberry Pi GitHub Pages Portals (`#stats` tab):
```text
GET /v1/stats?p=esp32|rpi
```
Returns aggregated JSON (`kpis`, `versions`, `hardware`, `os_arch`, `resolutions`, `hw_by_res`, `daily_history`, `github_releases`) cached for 15 minutes (`Cache-Control: public, max-age=900`).

---

## Running Unit Tests Locally

Run the self-contained Node.js test suite (validating UUID v4 checks, `v3.0.0` -> `v3.0.1` D1 `UPSERT` migration without duplication, and `/v1/stats` aggregation):
```bash
node tools/ota-stats-worker/test_worker.mjs
```
