#!/usr/bin/env node
/**
 * Unit test suite for ArcadeMatrix Unified OTA Stats & Releases Proxy Worker.
 * Runs in standard Node.js without external npm dependencies using an in-memory
 * Cloudflare D1 SQLite emulator that executes the exact SQL queries from src/index.js.
 */

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import worker, {
  WORKER_URL,
  isValidUuidV4,
  sanitizeCheckinParams,
  compareSemverDesc,
  createSqlStorageAdapter,
  TelemetryStore,
} from "./src/index.js";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

/**
 * In-memory Cloudflare D1 SQLite mock implementing the D1 prepared statement API
 * for `devices` and `daily_history` tables as defined in `schema.sql`.
 */
class MockD1Database {
  constructor() {
    /** @type {Map<string, {id:string, project:string, version:string, hw:string, os:string, res:string, first_seen:number, last_seen:number}>} */
    this.devices = new Map();
    /** @type {Map<string, {day:string, project:string, version:string, count:number}>} */
    this.dailyHistory = new Map();
    this.nowUnix = Math.floor(Date.now() / 1000);
    this.todayStr = new Date(this.nowUnix * 1000).toISOString().slice(0, 10);
  }

  prepare(sql) {
    const db = this;
    const normalizedSql = sql.replace(/\s+/g, " ").trim();
    return {
      boundArgs: [],
      bind(...args) {
        this.boundArgs = args;
        return this;
      },
      async run() {
        const args = this.boundArgs;

        // 1. UPSERT into devices
        if (normalizedSql.startsWith("INSERT INTO devices")) {
          const [id, project, version, hw, os, res] = args;
          if (typeof id !== "string" || id.length !== 36) {
            throw new Error("CHECK constraint failed: length(id) = 36");
          }
          if (project !== "esp32" && project !== "rpi") {
            throw new Error("CHECK constraint failed: project IN ('esp32', 'rpi')");
          }
          const existing = db.devices.get(id);
          if (existing) {
            existing.project = project;
            existing.version = version;
            existing.hw = hw;
            existing.os = os;
            existing.res = res;
            existing.last_seen = db.nowUnix;
          } else {
            db.devices.set(id, {
              id,
              project,
              version,
              hw,
              os,
              res,
              first_seen: db.nowUnix,
              last_seen: db.nowUnix,
            });
          }
          return { success: true };
        }

        // 2. DELETE FROM daily_history WHERE day = date('now') AND project = ?1
        if (normalizedSql.startsWith("DELETE FROM daily_history")) {
          const [project] = args;
          for (const [key, row] of db.dailyHistory.entries()) {
            if (row.day === db.todayStr && row.project === project) {
              db.dailyHistory.delete(key);
            }
          }
          return { success: true };
        }

        // 3. INSERT INTO daily_history ... SELECT date('now'), project, version, COUNT(*) FROM devices ...
        if (normalizedSql.startsWith("INSERT INTO daily_history")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const countsByVer = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              countsByVer.set(dev.version, (countsByVer.get(dev.version) || 0) + 1);
            }
          }
          for (const [version, count] of countsByVer.entries()) {
            const pk = `${db.todayStr}|${project}|${version}`;
            db.dailyHistory.set(pk, {
              day: db.todayStr,
              project,
              version,
              count,
            });
          }
          return { success: true };
        }

        throw new Error(`Unhandled run() SQL in MockD1Database: ${normalizedSql}`);
      },

      async all() {
        const args = this.boundArgs;

        // 1. Version aggregation
        if (normalizedSql.includes("FROM devices") && normalizedSql.includes("GROUP BY version") && !normalizedSql.includes("GROUP BY hw")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const byVer = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project !== project) continue;
            if (!byVer.has(dev.version)) {
              byVer.set(dev.version, { version: dev.version, active_30d: 0, total: 0 });
            }
            const entry = byVer.get(dev.version);
            entry.total += 1;
            if (dev.last_seen >= cutoff) {
              entry.active_30d += 1;
            }
          }
          return { results: Array.from(byVer.values()) };
        }

        // 2. Hardware breakdown
        if (normalizedSql.includes("SELECT hw, COUNT(*) AS count") && !normalizedSql.includes("res") && !normalizedSql.includes("version")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const byHw = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              byHw.set(dev.hw, (byHw.get(dev.hw) || 0) + 1);
            }
          }
          const results = Array.from(byHw.entries())
            .map(([hw, count]) => ({ hw, count }))
            .sort((a, b) => b.count - a.count);
          return { results };
        }

        // 3. OS breakdown
        if (normalizedSql.includes("ELSE os END AS os")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const byOs = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              const key = dev.os === "" ? "unknown" : dev.os;
              byOs.set(key, (byOs.get(key) || 0) + 1);
            }
          }
          const results = Array.from(byOs.entries())
            .map(([os, count]) => ({ os, count }))
            .sort((a, b) => b.count - a.count);
          return { results };
        }

        // 4. Resolution breakdown
        if (normalizedSql.includes("ELSE res END AS res") && !normalizedSql.includes("SELECT hw,")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const byRes = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              const key = dev.res === "" ? "unknown" : dev.res;
              byRes.set(key, (byRes.get(key) || 0) + 1);
            }
          }
          const results = Array.from(byRes.entries())
            .map(([res, count]) => ({ res, count }))
            .sort((a, b) => b.count - a.count);
          return { results };
        }

        // 5. Cross-breakdown hw_by_res
        if (normalizedSql.includes("SELECT hw, CASE WHEN res = '' THEN 'unknown' ELSE res END AS res")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const cross = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              const resKey = dev.res === "" ? "unknown" : dev.res;
              const k = `${dev.hw}|${resKey}`;
              cross.set(k, {
                hw: dev.hw,
                res: resKey,
                count: (cross.get(k)?.count || 0) + 1,
              });
            }
          }
          return { results: Array.from(cross.values()) };
        }

        // 5b. Cross-breakdown hw_by_version
        if (normalizedSql.includes("SELECT hw, version, COUNT(*) AS count")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const cross = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.last_seen >= cutoff) {
              const k = `${dev.hw}|${dev.version}`;
              cross.set(k, {
                hw: dev.hw,
                version: dev.version,
                count: (cross.get(k)?.count || 0) + 1,
              });
            }
          }
          return { results: Array.from(cross.values()) };
        }

        // 5c. Fleet activity recency windows
        if (normalizedSql.includes("AS active_24h")) {
          const [project, daySec, weekSec, monthSec] = args;
          const c24h = db.nowUnix - Number(daySec);
          const c7d = db.nowUnix - Number(weekSec);
          const c30d = db.nowUnix - Number(monthSec);
          const row = {
            active_24h: 0,
            active_1d_7d: 0,
            active_7d_30d: 0,
            inactive_30d: 0,
            new_7d: 0,
            new_30d: 0,
          };
          for (const dev of db.devices.values()) {
            if (dev.project !== project) continue;
            if (dev.last_seen >= c24h) row.active_24h += 1;
            else if (dev.last_seen >= c7d) row.active_1d_7d += 1;
            else if (dev.last_seen >= c30d) row.active_7d_30d += 1;
            else row.inactive_30d += 1;
            if (dev.first_seen >= c7d) row.new_7d += 1;
            if (dev.first_seen >= c30d) row.new_30d += 1;
          }
          return { results: [row] };
        }

        // 5d. Daily new device registrations
        if (normalizedSql.includes("date(first_seen, 'unixepoch') AS day")) {
          const [project, windowSec] = args;
          const cutoff = db.nowUnix - Number(windowSec);
          const byDay = new Map();
          for (const dev of db.devices.values()) {
            if (dev.project === project && dev.first_seen >= cutoff) {
              const day = new Date(dev.first_seen * 1000).toISOString().slice(0, 10);
              byDay.set(day, (byDay.get(day) || 0) + 1);
            }
          }
          const results = Array.from(byDay.entries())
            .map(([day, count]) => ({ day, count }))
            .sort((a, b) => a.day.localeCompare(b.day));
          return { results };
        }

        // 6. Daily history
        if (normalizedSql.includes("FROM daily_history")) {
          const [project] = args;
          const results = Array.from(db.dailyHistory.values())
            .filter((r) => r.project === project)
            .sort((a, b) => a.day.localeCompare(b.day));
          return { results };
        }

        throw new Error(`Unhandled all() SQL in MockD1Database: ${normalizedSql}`);
      },
    };
  }
}

async function runTests() {
  console.log("🔍 Running ArcadeMatrix OTA Stats Worker unit tests...");

  // 1. Verify schema.sql & wrangler.toml structure
  const schemaSql = fs.readFileSync(path.join(__dirname, "schema.sql"), "utf8");
  assert.ok(schemaSql.includes("CREATE TABLE IF NOT EXISTS devices"), "schema.sql must define devices table");
  assert.ok(schemaSql.includes("id TEXT PRIMARY KEY"), "devices.id must be TEXT PRIMARY KEY");
  assert.ok(schemaSql.includes("CREATE TABLE IF NOT EXISTS daily_history"), "schema.sql must define daily_history table");
  assert.ok(schemaSql.includes("PRIMARY KEY (day, project, version)"), "daily_history must have composite PRIMARY KEY");
  assert.ok(!schemaSql.toLowerCase().includes("ip_address"), "schema.sql must never store IP addresses");

  const wranglerToml = fs.readFileSync(path.join(__dirname, "wrangler.toml"), "utf8");
  assert.ok(wranglerToml.includes('name = "arcadematrix-ota-stats"'), "wrangler.toml worker name check");
  assert.ok(wranglerToml.includes('database_name = "arcadematrix-ota-db"'), "wrangler.toml D1 database_name check");
  assert.ok(wranglerToml.includes('crons = ["5 0 * * *"]'), "wrangler.toml cron trigger check");
  assert.equal(WORKER_URL, "https://arcadematrix-ota-stats.red77290.workers.dev");
  console.log("  ✅ schema.sql & wrangler.toml validated.");

  // 2. Test RFC 4122 UUID v4 validation
  assert.equal(isValidUuidV4("550e8400-e29b-41d4-a716-446655440000"), true, "Valid UUID v4 should pass");
  assert.equal(isValidUuidV4("f47ac10b-58cc-4372-a567-0e02b2c3d479"), true, "Valid UUID v4 should pass");
  assert.equal(isValidUuidV4("550e8400-e29b-11d4-a716-446655440000"), false, "UUID v1 must be rejected");
  assert.equal(isValidUuidV4("550e8400-e29b-51d4-a716-446655440000"), false, "UUID v5 must be rejected");
  assert.equal(isValidUuidV4("550e8400-e29b-41d4-0716-446655440000"), false, "Invalid RFC 4122 variant must be rejected");
  assert.equal(isValidUuidV4("00000000-0000-4000-8000-000000000000"), false, "Zero-filled v4 fallback UUID must be rejected");
  assert.equal(isValidUuidV4("00000000-0000-0000-0000-000000000000"), false, "Nil UUID must be rejected");
  assert.equal(isValidUuidV4("a4074101-8139-44a4-920e-0ecddf62503c"), false, "Leaked default config.json UUID must be rejected");
  assert.equal(isValidUuidV4("11111111-2222-4333-8444-555555555555"), false, "Synthetic smoke-test UUID must be rejected");
  assert.equal(isValidUuidV4("550e8400-e29b-41d4-a716-44665544000"), false, "35-char string must be rejected");
  assert.equal(isValidUuidV4("550e8400-e29b-41d4-a716-4466554400000"), false, "37-char string must be rejected");
  assert.equal(isValidUuidV4(""), false, "Empty string must be rejected");
  assert.equal(isValidUuidV4("'; DROP TABLE devices; --"), false, "SQL injection string must be rejected");
  console.log("  ✅ RFC 4122 UUID v4 strict validation passed.");

  // 3. Test parameter sanitization (including 'rpi3b+' plus sign handling)
  const sp = new URLSearchParams("p=rpi&id=550e8400-e29b-41d4-a716-446655440000&v=3.0.1&hw=rpi3b+&os=64bit-bookworm&res=128x64");
  const clean = sanitizeCheckinParams(sp);
  assert.equal(clean.project, "rpi");
  assert.equal(clean.version, "v3.0.1");
  assert.equal(clean.hw, "rpi3b+");
  assert.equal(clean.os, "64bit-bookworm");
  assert.equal(clean.res, "128x64");
  assert.equal(clean.canRecordTelemetry, true);
  assert.ok(compareSemverDesc("v3.0.1", "v3.0.0") < 0, "v3.0.1 should sort before v3.0.0");
  console.log("  ✅ Parameter sanitization & semver sorting passed.");

  // 4. Test D1 UPSERT version migration (v3.0.0 -> v3.0.1) without duplicating devices
  const db = new MockD1Database();
  const mockReleases = [
    {
      tag_name: "v3.0.1",
      published_at: "2026-11-15T12:00:00Z",
      assets: [
        { name: "firmware-esp32s3_waveshare.bin", download_count: 140 },
        { name: "firmware-esp32s3_n16r8.bin", download_count: 85 },
        { name: "ArcadeMatrix-sdcard.zip", download_count: 999 },
      ],
    },
    {
      tag_name: "v3.0.0",
      published_at: "2026-10-20T12:00:00Z",
      assets: [{ name: "firmware-esp32s3_waveshare.bin", download_count: 310 }],
    },
  ];

  const env = {
    DB: db,
    __mockFetch: async () =>
      new Response(JSON.stringify(mockReleases), {
        status: 200,
        headers: { "Content-Type": "application/json" },
      }),
  };

  const pendingPromises = [];
  const ctx = {
    waitUntil(p) {
      pendingPromises.push(p);
    },
  };

  // Step 4.0: Empty D1 database must return 0 devices cleanly (never synthetic numbers)
  const emptyStatsRes = await worker.fetch(
    new Request(`${WORKER_URL}/v1/stats?p=esp32`),
    env,
    ctx
  );
  const emptyStats = await emptyStatsRes.json();
  assert.equal(emptyStats.kpis.active_30d, 0);
  assert.equal(emptyStats.kpis.total_known, 0);
  assert.equal(emptyStats.kpis.latest_version, "v3.0.1");
  assert.equal(emptyStats.kpis.latest_adoption_pct, 0);
  assert.equal(emptyStats.kpis.github_downloads_total, 535, "ArcadeMatrix-sdcard.zip must be excluded");
  assert.equal(emptyStats.versions.length, 0);
  assert.equal(emptyStats.resolutions.length, 0);
  assert.equal(emptyStats.github_hw_downloads[0].hw, "esp32s3_waveshare");
  assert.equal(emptyStats.github_hw_downloads[0].downloads, 450);

  const uuid1 = "550e8400-e29b-41d4-a716-446655440001";
  const uuid2 = "550e8400-e29b-41d4-a716-446655440002";

  // Step 4a: Both devices check in on v3.0.0
  const req1 = new Request(
    `${WORKER_URL}/v1/ota/releases?p=esp32&id=${uuid1}&v=v3.0.0&hw=esp32s3_waveshare&os=freertos&res=128x64`,
    { headers: { "CF-Connecting-IP": "203.0.113.42" } }
  );
  const res1 = await worker.fetch(req1, env, ctx);
  assert.equal(res1.status, 200);
  assert.equal(res1.headers.get("Access-Control-Allow-Origin"), "*");

  const req2 = new Request(
    `${WORKER_URL}/v1/ota/releases?p=esp32&id=${uuid2}&v=v3.0.0&hw=esp32s3_n16r8&os=freertos&res=64x32`
  );
  await worker.fetch(req2, env, ctx);
  await Promise.all(pendingPromises.splice(0));

  // Verify initial state: 2 devices on v3.0.0 (while latest GitHub release is v3.0.1 -> 0% adoption!)
  const statsResBefore = await worker.fetch(
    new Request(`${WORKER_URL}/v1/stats?p=esp32`),
    env,
    ctx
  );
  const statsBefore = await statsResBefore.json();
  assert.equal(statsBefore.kpis.active_30d, 2);
  assert.equal(statsBefore.kpis.total_known, 2);
  assert.equal(statsBefore.kpis.latest_version, "v3.0.1");
  assert.equal(statsBefore.kpis.latest_adoption_pct, 0, "0 devices on v3.0.1 must report 0% adoption");
  assert.equal(statsBefore.versions.length, 1);
  assert.equal(statsBefore.versions[0].version, "v3.0.0");
  assert.equal(statsBefore.versions[0].active_30d, 2);

  // Step 4b: Device 1 upgrades to v3.0.1 and performs an OTA check with the SAME UUID
  const reqUpgrade = new Request(
    `${WORKER_URL}/v1/ota/releases?p=esp32&id=${uuid1}&v=v3.0.1&hw=esp32s3_waveshare&os=freertos&res=128x64`
  );
  await worker.fetch(reqUpgrade, env, ctx);
  await Promise.all(pendingPromises.splice(0));

  // Step 4c: Invalid / poisoned UUID check-ins must still receive GitHub Releases (200 OK) but NOT pollute D1
  for (const badId of [
    "not-a-valid-uuid",
    "00000000-0000-4000-8000-000000000000",
    "a4074101-8139-44a4-920e-0ecddf62503c",
  ]) {
    const reqInvalidUuid = new Request(
      `${WORKER_URL}/v1/ota/releases?p=esp32&id=${badId}&v=v3.0.1&hw=esp32dev&os=freertos&res=64x32`
    );
    const resInvalid = await worker.fetch(reqInvalidUuid, env, ctx);
    assert.equal(resInvalid.status, 200, "OTA proxy must still return releases even if UUID is invalid");
  }
  await Promise.all(pendingPromises.splice(0));

  // Verify post-migration state:
  // - Total devices is STILL 2 (not 3!)
  // - v3.0.1 has 1 device (50%)
  // - v3.0.0 decreased from 2 to 1 device (50%)
  const statsResAfter = await worker.fetch(
    new Request(`${WORKER_URL}/v1/stats?p=esp32`),
    env,
    ctx
  );
  const statsAfter = await statsResAfter.json();

  assert.equal(statsAfter.kpis.active_30d, 2, "Total active devices must remain 2 after upgrade");
  assert.equal(statsAfter.kpis.total_known, 2, "Total known devices must remain 2 (no duplicate row)");
  assert.equal(statsAfter.kpis.latest_version, "v3.0.1");
  assert.equal(statsAfter.kpis.latest_adoption_pct, 50);
  assert.equal(statsAfter.kpis.github_downloads_total, 535);

  assert.equal(statsAfter.versions.length, 2);
  assert.deepEqual(statsAfter.versions[0], {
    version: "v3.0.1",
    active_30d: 1,
    total: 1,
    pct: 50,
  });
  assert.deepEqual(statsAfter.versions[1], {
    version: "v3.0.0",
    active_30d: 1,
    total: 1,
    pct: 50,
  });

  // Verify daily_history also immediately reflects 1 on v3.0.1 and 1 on v3.0.0
  assert.equal(statsAfter.daily_history.length, 1);
  assert.deepEqual(statsAfter.daily_history[0].versions, {
    "v3.0.1": 1,
    "v3.0.0": 1,
  });

  // Verify hw_by_res & hw_by_version cross-matrices + activity recency + daily new devices
  assert.equal(statsAfter.hw_by_res.esp32s3_waveshare["128x64"], 1);
  assert.equal(statsAfter.hw_by_res.esp32s3_n16r8["64x32"], 1);
  assert.equal(statsAfter.hw_by_version.esp32s3_waveshare["v3.0.1"], 1);
  assert.equal(statsAfter.hw_by_version.esp32s3_n16r8["v3.0.0"], 1);
  assert.equal(statsAfter.kpis.active_24h, 2);
  assert.equal(statsAfter.kpis.active_7d, 2);
  assert.equal(statsAfter.kpis.inactive_30d, 0);
  assert.equal(statsAfter.kpis.new_30d, 2);
  assert.equal(statsAfter.kpis.github_latest_downloads, 225);
  assert.equal(statsAfter.activity.length, 4);
  assert.equal(statsAfter.activity[0].bucket, "24h");
  assert.equal(statsAfter.activity[0].count, 2);
  assert.equal(statsAfter.new_devices_daily.length, 1);
  assert.equal(statsAfter.new_devices_daily[0].count, 2);

  // Verify zero IP leakage in DB or response
  const serializedStats = JSON.stringify(statsAfter);
  assert.ok(!serializedStats.includes("203.0.113.42"), "Client IP must never appear in stats");

  // Verify createSqlStorageAdapter numbered parameter expansion (?2 appearing before ?1)
  const capturedCalls = [];
  const adapter = createSqlStorageAdapter({
    exec(sql, ...args) {
      capturedCalls.push({ sql, args });
      return { toArray: () => [{ version: "v4.2.1", active_30d: 1, total: 1 }] };
    },
  });
  const adapterRes = await adapter
    .prepare("SELECT SUM(CASE WHEN last_seen >= unixepoch() - ?2 THEN 1 ELSE 0 END) FROM devices WHERE project = ?1")
    .bind("esp32", 2592000)
    .all();
  assert.equal(capturedCalls.length, 1);
  assert.equal(
    capturedCalls[0].sql,
    "SELECT SUM(CASE WHEN last_seen >= unixepoch() - ? THEN 1 ELSE 0 END) FROM devices WHERE project = ?"
  );
  assert.deepEqual(capturedCalls[0].args, [2592000, "esp32"]);
  assert.equal(adapterRes.results[0].version, "v4.2.1");

  // Verify TELEMETRY_DO fallback routing when env.DB is absent
  const mockD1ForDo = new MockD1Database();
  const doInstance = Object.create(TelemetryStore.prototype);
  doInstance.db = mockD1ForDo;
  const doEnv = {
    TELEMETRY_DO: {
      idFromName: () => "global-id",
      get: () => ({
        fetch: (url, init) => doInstance.fetch(new Request(url, init)),
      }),
    },
    __mockFetch: env.__mockFetch,
  };
  await worker.fetch(
    new Request(`${WORKER_URL}/v1/ota/releases?p=esp32&id=550e8400-e29b-41d4-a716-446655440001&v=v4.2.1&hw=esp32dev&os=freertos&res=64x32`),
    doEnv,
    null
  );
  const doStatsRes = await worker.fetch(new Request(`${WORKER_URL}/v1/stats?p=esp32`), doEnv, null);
  assert.equal(doStatsRes.status, 200);
  const doStats = await doStatsRes.json();
  assert.equal(doStats.kpis.total_known, 1);
  assert.equal(doStats.kpis.active_30d, 1);

  console.log("  ✅ D1 + SQLite Durable Object UPSERT version migration & /v1/stats aggregation passed.");
  console.log("🎉 All OTA Stats Worker unit tests PASSED!");
}

runTests().catch((err) => {
  console.error("❌ Worker test failed:", err);
  process.exit(1);
});
