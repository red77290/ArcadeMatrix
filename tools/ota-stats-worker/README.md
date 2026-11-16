# ArcadeMatrix Unified OTA Stats & Releases Proxy Worker

Turnkey **Cloudflare Worker + D1 SQLite** microservice serving both **ArcadeMatrix (ESP32)** (`p=esp32`) and **ArcadeMatrix_RPI (Raspberry Pi)** (`p=rpi`).

## Architecture & Privacy-by-Design

- **Zero IP / Zero PII**: Client IP headers (`CF-Connecting-IP`, `X-Forwarded-For`, etc.) are never read or stored.
- **Strict RFC 4122 UUID v4 Validation**: Only 36-character UUID v4 identifiers (`/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i`) generated locally on the matrix are accepted.
- **Idempotent Version Migration (`UPSERT`)**: The `devices` table uses `id TEXT PRIMARY KEY`. When a matrix upgrades from `v3.0.0` to `v3.0.1` and performs an OTA check, `ON CONFLICT(id) DO UPDATE` updates its `version` and `last_seen` in place—migrating the device from `v3.0.0` to `v3.0.1` without ever duplicating the device count.
- **GitHub Releases Edge Cache**: Proxies `red77290/ArcadeMatrix` and `red77290/ArcadeMatrix_RPI` releases with a 10-minute Cloudflare Edge cache (`Cache-Control: public, max-age=600`), preventing GitHub API rate-limiting (`60 req/hr/IP`) across the fleet.

---

## Quick Deployment Guide (4 Commands)

Run the following commands from `tools/ota-stats-worker/`:

### 1. Create the D1 SQLite Database
```bash
npx wrangler d1 create arcadematrix-ota-db
```
Copy the `database_id` printed in the terminal output and paste it into `wrangler.toml` under `[[d1_databases]]`.

### 2. Initialize the Remote D1 Schema
```bash
npx wrangler d1 execute arcadematrix-ota-db --remote --file=schema.sql
```

### 3. Deploy the Cloudflare Worker
```bash
npx wrangler deploy
```

### 4. Verify the Canonical Worker URL Constant
The canonical endpoint pre-configured across `ArcadeMatrix` (ESP32 WebUI & Portal) and `ArcadeMatrix_RPI` (Rust OTA service, WebUI & Portal) is:
```text
https://arcadematrix-ota-stats.red77290.workers.dev
```
If your Cloudflare Workers subdomain differs, update `WORKER_URL` in `src/index.js` and the matching `OTA_STATS_WORKER_URL` constant in the portals/WebUIs.

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
