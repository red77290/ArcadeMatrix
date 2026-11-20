/**
 * ArcadeMatrix Unified OTA Releases Proxy & Anonymous Fleet Telemetry Worker
 *
 * Serves both:
 *  - p=esp32 -> red77290/ArcadeMatrix
 *  - p=rpi   -> red77290/ArcadeMatrix_RPI
 *
 * Privacy guarantees:
 *  - Zero IP address reading or storage (CF-Connecting-IP / X-Forwarded-For ignored)
 *  - Zero cookies or fingerprinting
 *  - Strictly validates RFC 4122 UUID v4 (36 chars) generated locally on the device
 *  - Uses SQLite D1 UPSERT on PRIMARY KEY (id) so version upgrades (e.g. v3.0.0 -> v3.0.1)
 *    migrate the existing device record in place without ever double-counting.
 */

export const WORKER_URL = "https://arcadematrix-ota-stats.red77290.workers.dev";

export const UUID_V4_REGEX =
  /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i;

const ZERO_RANDOM_UUID_V4_REGEX =
  /^0{8}-0{4}-40{3}-[89ab]0{3}-0{12}$/i;

export const REJECTED_UUIDS = new Set([
  "00000000-0000-0000-0000-000000000000",
  "00000000-0000-4000-8000-000000000000",
  "a4074101-8139-44a4-920e-0ecddf62503c",
  "11111111-2222-4333-8444-555555555555",
]);

export const VERSION_REGEX = /^v?[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z._-]+)?$/;
export const RES_REGEX = /^\d{2,4}x\d{2,4}$/;
export const OS_REGEX = /^[a-zA-Z0-9._+-]{1,32}$/;

export const VALID_PROJECTS = new Set(["esp32", "rpi"]);

export const VALID_HW = new Set([
  "esp32dev",
  "esp32s3_waveshare",
  "esp32s3_n16r8",
  "rpi5",
  "rpi4b",
  "rpi3b+",
  "rpi3b",
  "rpizero2w",
  "rpizero",
  "rpi_other",
  "unknown",
]);

export const GITHUB_REPOS = {
  esp32: "red77290/ArcadeMatrix",
  rpi: "red77290/ArcadeMatrix_RPI",
};

const ACTIVE_WINDOW_SECONDS = 30 * 24 * 60 * 60; // 30 days

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, Accept",
};

/**
 * Validates that a string is a strict, non-zero, non-poisoned 36-character RFC 4122 UUID v4.
 * @param {string} id
 * @returns {boolean}
 */
export function isValidUuidV4(id) {
  if (typeof id !== "string") return false;
  const trimmed = id.trim().toLowerCase();
  if (trimmed.length !== 36 || !UUID_V4_REGEX.test(trimmed)) return false;
  if (ZERO_RANDOM_UUID_V4_REGEX.test(trimmed)) return false;
  if (REJECTED_UUIDS.has(trimmed)) return false;
  return true;
}

/**
 * Sanitizes and validates query parameters for OTA check-in.
 * @param {URLSearchParams} searchParams
 */
export function sanitizeCheckinParams(searchParams) {
  const rawProject = (searchParams.get("p") || "").trim().toLowerCase();
  const project = VALID_PROJECTS.has(rawProject) ? rawProject : null;

  const rawId = (searchParams.get("id") || "").trim().toLowerCase();
  const id = isValidUuidV4(rawId) ? rawId : null;

  let rawVer = (searchParams.get("v") || "").trim();
  let version = null;
  if (rawVer.length > 0 && rawVer.length <= 24 && VERSION_REGEX.test(rawVer)) {
    version = rawVer.startsWith("v") || rawVer.startsWith("V")
      ? "v" + rawVer.slice(1)
      : "v" + rawVer;
  }

  // Handle '+' in 'rpi3b+' potentially decoded as space in query strings before trimming
  const rawHwParam = (searchParams.get("hw") || "").toLowerCase();
  let rawHw = rawHwParam === "rpi3b " ? "rpi3b+" : rawHwParam.trim();
  const hw = VALID_HW.has(rawHw) ? rawHw : null;

  const rawOs = (searchParams.get("os") || "").trim().toLowerCase();
  const os = rawOs && rawOs.length <= 32 && OS_REGEX.test(rawOs) ? rawOs : "";

  const rawRes = (searchParams.get("res") || "").trim().toLowerCase();
  const res = rawRes && RES_REGEX.test(rawRes) ? rawRes : "";

  return {
    project,
    id,
    version,
    hw,
    os,
    res,
    canRecordTelemetry: Boolean(project && id && version && hw),
  };
}

/**
 * Compares two semver strings in descending order (newest first).
 * @param {string} a
 * @param {string} b
 * @returns {number}
 */
export function compareSemverDesc(a, b) {
  const clean = (v) =>
    String(v || "")
      .replace(/^v/i, "")
      .split("-")[0]
      .split(".")
      .map((n) => parseInt(n, 10) || 0);
  const pa = clean(a);
  const pb = clean(b);
  for (let i = 0; i < 3; i++) {
    if ((pb[i] || 0) !== (pa[i] || 0)) {
      return (pb[i] || 0) - (pa[i] || 0);
    }
  }
  return String(b || "").localeCompare(String(a || ""));
}

/**
 * Upserts the device check-in and immediately refreshes today's daily_history
 * for that project so version migrations (e.g. v3.0.0 -> v3.0.1) are reflected
 * in real time without waiting for the midnight cron.
 *
 * @param {D1Database} db
 * @param {{ id: string, project: string, version: string, hw: string, os: string, res: string }} record
 */
export async function upsertDeviceAndSnapshot(db, record) {
  await db
    .prepare(
      `INSERT INTO devices (id, project, version, hw, os, res, first_seen, last_seen)
       VALUES (?1, ?2, ?3, ?4, ?5, ?6, unixepoch(), unixepoch())
       ON CONFLICT(id) DO UPDATE SET
         project = excluded.project,
         version = excluded.version,
         hw = excluded.hw,
         os = excluded.os,
         res = excluded.res,
         last_seen = unixepoch()`
    )
    .bind(
      record.id,
      record.project,
      record.version,
      record.hw,
      record.os,
      record.res
    )
    .run();

  await refreshDailyHistoryForProject(db, record.project);
}

/**
 * Recomputes today's daily_history snapshot for a given project from active devices (last 30d).
 * @param {D1Database} db
 * @param {string} project
 */
export async function refreshDailyHistoryForProject(db, project) {
  await db
    .prepare(
      `DELETE FROM daily_history WHERE day = date('now') AND project = ?1`
    )
    .bind(project)
    .run();

  await db
    .prepare(
      `INSERT INTO daily_history (day, project, version, count)
       SELECT date('now'), project, version, COUNT(*)
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY project, version`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .run();
}

/**
 * Fetches GitHub Releases JSON with Edge caching (10 minutes) to avoid GitHub API rate limits.
 * @param {string} project
 * @param {function} [fetchImpl]
 */
export async function fetchGitHubReleases(project, fetchImpl = fetch) {
  const repo = GITHUB_REPOS[project];
  const ghUrl = `https://api.github.com/repos/${repo}/releases?per_page=20`;

  const response = await fetchImpl(ghUrl, {
    headers: {
      Accept: "application/vnd.github+json",
      "User-Agent": "ArcadeMatrix-OTA-Proxy/1.0 (+https://github.com/red77290)",
    },
    cf: {
      cacheTtl: 600,
      cacheEverything: true,
    },
  });

  if (!response.ok) {
    throw new Error(`GitHub Releases API returned HTTP ${response.status}`);
  }
  return await response.json();
}

/**
 * Aggregates fleet statistics and version migration breakdowns from D1 + GitHub Releases.
 * @param {D1Database} db
 * @param {string} project
 * @param {Array} githubReleasesRaw
 */
export async function buildProjectStats(db, project, githubReleasesRaw = []) {
  // 1. Version breakdown (both active_30d and total_known)
  const versionRowsResult = await db
    .prepare(
      `SELECT
         version,
         SUM(CASE WHEN last_seen >= unixepoch() - ?2 THEN 1 ELSE 0 END) AS active_30d,
         COUNT(*) AS total
       FROM devices
       WHERE project = ?1
       GROUP BY version`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const versionRows = versionRowsResult?.results || [];
  let active30dTotal = 0;
  let totalKnown = 0;
  for (const row of versionRows) {
    active30dTotal += Number(row.active_30d || 0);
    totalKnown += Number(row.total || 0);
  }

  const versions = versionRows
    .map((row) => {
      const active = Number(row.active_30d || 0);
      const total = Number(row.total || 0);
      const pct =
        active30dTotal > 0
          ? Math.round((active / active30dTotal) * 1000) / 10
          : 0;
      return {
        version: row.version,
        active_30d: active,
        total,
        pct,
      };
    })
    .sort((a, b) => compareSemverDesc(a.version, b.version));

  // 2. Active hardware breakdown (last 30 days)
  const hwRowsResult = await db
    .prepare(
      `SELECT hw, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY hw
       ORDER BY count DESC`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const hardware = (hwRowsResult?.results || []).map((row) => {
    const count = Number(row.count || 0);
    return {
      hw: row.hw,
      count,
      pct:
        active30dTotal > 0
          ? Math.round((count / active30dTotal) * 1000) / 10
          : 0,
    };
  });

  // 3. OS / architecture breakdown (last 30 days)
  const osRowsResult = await db
    .prepare(
      `SELECT CASE WHEN os = '' THEN 'unknown' ELSE os END AS os, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY CASE WHEN os = '' THEN 'unknown' ELSE os END
       ORDER BY count DESC`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const os_arch = (osRowsResult?.results || []).map((row) => {
    const count = Number(row.count || 0);
    return {
      os: row.os,
      count,
      pct:
        active30dTotal > 0
          ? Math.round((count / active30dTotal) * 1000) / 10
          : 0,
    };
  });

  // 4. Resolution breakdown (last 30 days)
  const resRowsResult = await db
    .prepare(
      `SELECT CASE WHEN res = '' THEN 'unknown' ELSE res END AS res, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY CASE WHEN res = '' THEN 'unknown' ELSE res END
       ORDER BY count DESC`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const resolutions = (resRowsResult?.results || []).map((row) => {
    const count = Number(row.count || 0);
    return {
      res: row.res,
      count,
      pct:
        active30dTotal > 0
          ? Math.round((count / active30dTotal) * 1000) / 10
          : 0,
    };
  });

  // 5. Cross-breakdown hw_by_res (last 30 days)
  const crossRowsResult = await db
    .prepare(
      `SELECT hw, CASE WHEN res = '' THEN 'unknown' ELSE res END AS res, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY hw, CASE WHEN res = '' THEN 'unknown' ELSE res END`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const hw_by_res = {};
  for (const row of crossRowsResult?.results || []) {
    if (!hw_by_res[row.hw]) {
      hw_by_res[row.hw] = {};
    }
    hw_by_res[row.hw][row.res] = Number(row.count || 0);
  }

  // 5b. Cross-breakdown hw_by_version (active last 30 days)
  const hwVerRowsResult = await db
    .prepare(
      `SELECT hw, version, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND last_seen >= unixepoch() - ?2
       GROUP BY hw, version`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const hw_by_version = {};
  for (const row of hwVerRowsResult?.results || []) {
    if (!hw_by_version[row.hw]) {
      hw_by_version[row.hw] = {};
    }
    hw_by_version[row.hw][row.version] = Number(row.count || 0);
  }

  // 5c. Fleet activity recency windows & new device growth
  const DAY_SECONDS = 86400;
  const WEEK_SECONDS = 7 * DAY_SECONDS;
  const activityRowsResult = await db
    .prepare(
      `SELECT
         SUM(CASE WHEN last_seen >= unixepoch() - ?2 THEN 1 ELSE 0 END) AS active_24h,
         SUM(CASE WHEN last_seen >= unixepoch() - ?3 AND last_seen < unixepoch() - ?2 THEN 1 ELSE 0 END) AS active_1d_7d,
         SUM(CASE WHEN last_seen >= unixepoch() - ?4 AND last_seen < unixepoch() - ?3 THEN 1 ELSE 0 END) AS active_7d_30d,
         SUM(CASE WHEN last_seen < unixepoch() - ?4 THEN 1 ELSE 0 END) AS inactive_30d,
         SUM(CASE WHEN first_seen >= unixepoch() - ?3 THEN 1 ELSE 0 END) AS new_7d,
         SUM(CASE WHEN first_seen >= unixepoch() - ?4 THEN 1 ELSE 0 END) AS new_30d
       FROM devices
       WHERE project = ?1`
    )
    .bind(project, DAY_SECONDS, WEEK_SECONDS, ACTIVE_WINDOW_SECONDS)
    .all();

  const actRow = (activityRowsResult?.results || [])[0] || {};
  const active24h = Number(actRow.active_24h || 0);
  const active1d7d = Number(actRow.active_1d_7d || 0);
  const active7d = active24h + active1d7d;
  const active7d30d = Number(actRow.active_7d_30d || 0);
  const inactive30d = Number(actRow.inactive_30d || 0);
  const new7d = Number(actRow.new_7d || 0);
  const new30d = Number(actRow.new_30d || 0);

  const pctOfKnown = (n) =>
    totalKnown > 0 ? Math.round((n / totalKnown) * 1000) / 10 : 0;

  const activity = [
    { bucket: "24h", count: active24h, pct: pctOfKnown(active24h) },
    { bucket: "7d", count: active1d7d, pct: pctOfKnown(active1d7d) },
    { bucket: "30d", count: active7d30d, pct: pctOfKnown(active7d30d) },
    { bucket: "dormant", count: inactive30d, pct: pctOfKnown(inactive30d) },
  ];

  // 5d. Daily new device registrations (last 30 days)
  const newDailyRowsResult = await db
    .prepare(
      `SELECT date(first_seen, 'unixepoch') AS day, COUNT(*) AS count
       FROM devices
       WHERE project = ?1 AND first_seen >= unixepoch() - ?2
       GROUP BY date(first_seen, 'unixepoch')
       ORDER BY day ASC`
    )
    .bind(project, ACTIVE_WINDOW_SECONDS)
    .all();

  const new_devices_daily = (newDailyRowsResult?.results || []).map((row) => ({
    day: row.day,
    count: Number(row.count || 0),
  }));

  // 6. Daily history (last 30 days)
  const historyRowsResult = await db
    .prepare(
      `SELECT day, version, count
       FROM daily_history
       WHERE project = ?1 AND day >= date('now', '-30 days')
       ORDER BY day ASC`
    )
    .bind(project)
    .all();

  const historyByDay = new Map();
  for (const row of historyRowsResult?.results || []) {
    if (!historyByDay.has(row.day)) {
      historyByDay.set(row.day, {});
    }
    historyByDay.get(row.day)[row.version] = Number(row.count || 0);
  }
  const daily_history = Array.from(historyByDay.entries()).map(
    ([day, vers]) => ({
      day,
      versions: vers,
    })
  );

  // 7. Supplementary GitHub releases raw asset download counters (kept strictly separate from Cloudflare telemetry)
  let githubDownloadsTotal = 0;
  const ghHwCounts = new Map();
  const classifyAssetHw = (assetName) => {
    const lower = String(assetName || "").toLowerCase();
    if (lower.includes("sdcard") || lower.includes("tools")) return null;
    if (project === "esp32") {
      if (lower.includes("waveshare")) return "esp32s3_waveshare";
      if (lower.includes("n16r8")) return "esp32s3_n16r8";
      if (lower.includes("esp32dev")) return "esp32dev";
      return null;
    }
    if (lower.includes("aarch64") || lower.includes("arm64")) return "aarch64";
    if (lower.includes("armhf") || lower.includes("armv7")) return "armhf";
    return null;
  };

  const github_releases = (
    Array.isArray(githubReleasesRaw) ? githubReleasesRaw : []
  ).map((rel) => {
    const assets = Array.isArray(rel.assets) ? rel.assets : [];
    let downloads = 0;
    for (const asset of assets) {
      const nameLower = String(asset.name || "").toLowerCase();
      if (nameLower.includes("sdcard") || nameLower.includes("tools")) continue;
      const dl = Number(asset.download_count || 0);
      downloads += dl;
      const hwKey = classifyAssetHw(nameLower);
      if (hwKey) {
        ghHwCounts.set(hwKey, (ghHwCounts.get(hwKey) || 0) + dl);
      }
    }
    githubDownloadsTotal += downloads;
    return {
      tag: rel.tag_name || rel.name || "unknown",
      published_at: rel.published_at || null,
      downloads,
    };
  });

  const ghHwSum = Array.from(ghHwCounts.values()).reduce((a, b) => a + b, 0);
  const github_hw_downloads = Array.from(ghHwCounts.entries())
    .map(([hw, downloads]) => ({
      hw,
      downloads,
      pct: ghHwSum > 0 ? Math.round((downloads / ghHwSum) * 1000) / 10 : 0,
    }))
    .sort((a, b) => b.downloads - a.downloads);

  const defaultLatest = project === "esp32" ? "v4.2.1" : "v4.2.0";
  const latestVersion =
    github_releases[0]?.tag || versions[0]?.version || defaultLatest;
  const latestEntry =
    versions.find((v) => v.version === latestVersion) || null;
  const latestAdoptionPct = latestEntry ? latestEntry.pct : 0;
  const githubLatestDownloads = github_releases[0]?.downloads || 0;

  return {
    project,
    updated_at: new Date().toISOString(),
    kpis: {
      active_24h: active24h,
      active_7d: active7d,
      active_30d: active30dTotal,
      inactive_30d: inactive30d,
      new_7d: new7d,
      new_30d: new30d,
      total_known: totalKnown,
      latest_version: latestVersion,
      latest_adoption_pct: latestAdoptionPct,
      github_downloads_total: githubDownloadsTotal,
      github_latest_downloads: githubLatestDownloads,
    },
    versions,
    hardware,
    os_arch,
    resolutions,
    hw_by_res,
    hw_by_version,
    activity,
    new_devices_daily,
    daily_history,
    github_releases,
    github_hw_downloads,
  };
}

function jsonResponse(payload, status = 200, maxAgeSeconds = 0) {
  const headers = {
    ...CORS_HEADERS,
    "Content-Type": "application/json; charset=utf-8",
  };
  if (maxAgeSeconds > 0) {
    headers["Cache-Control"] = `public, max-age=${maxAgeSeconds}`;
  } else {
    headers["Cache-Control"] = "no-store";
  }
  return new Response(JSON.stringify(payload), { status, headers });
}

const TELEMETRY_SCHEMA_STATEMENTS = [
  `CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY CHECK (length(id) = 36),
    project TEXT NOT NULL CHECK (project IN ('esp32', 'rpi')),
    version TEXT NOT NULL,
    hw TEXT NOT NULL,
    os TEXT NOT NULL DEFAULT '',
    res TEXT NOT NULL DEFAULT '',
    first_seen INTEGER NOT NULL,
    last_seen INTEGER NOT NULL
  )`,
  `CREATE INDEX IF NOT EXISTS idx_devices_project_last_seen ON devices (project, last_seen)`,
  `CREATE INDEX IF NOT EXISTS idx_devices_project_version ON devices (project, version)`,
  `CREATE TABLE IF NOT EXISTS daily_history (
    day TEXT NOT NULL,
    project TEXT NOT NULL CHECK (project IN ('esp32', 'rpi')),
    version TEXT NOT NULL,
    count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day, project, version)
  )`,
  `CREATE INDEX IF NOT EXISTS idx_daily_history_project_day ON daily_history (project, day)`,
];

/**
 * Wraps Cloudflare Durable Object `state.storage.sql` (`SqlStorage`) in a D1-compatible
 * `.prepare(sql).bind(...args).run()/.all()` interface so the exact same SQL queries work
 * seamlessly on both Cloudflare D1 (`env.DB`) and SQLite Durable Objects (`env.TELEMETRY_DO`).
 *
 * @param {{ exec: (query: string, ...bindings: any[]) => { toArray: () => any[] } }} sqlStorage
 */
export function createSqlStorageAdapter(sqlStorage) {
  return {
    prepare(query) {
      return {
        boundArgs: [],
        bind(...args) {
          this.boundArgs = args;
          return this;
        },
        _exec() {
          const bound = this.boundArgs;
          const positional = [];
          const rewritten = query.replace(/\?(\d+)/g, (_, n) => {
            positional.push(bound[Number(n) - 1]);
            return "?";
          });
          const finalArgs = positional.length > 0 ? positional : bound;
          return sqlStorage.exec(rewritten, ...finalArgs);
        },
        async run() {
          this._exec();
          return { success: true };
        },
        async all() {
          const cursor = this._exec();
          return { results: cursor.toArray() };
        },
      };
    },
  };
}

/**
 * SQLite-backed Durable Object storing anonymous fleet telemetry when `env.DB` (D1) is not bound.
 * Provisioned automatically with standard `Workers Scripts: Edit` permission on Workers Free & Paid plans.
 */
export class TelemetryStore {
  constructor(state, _env) {
    this.state = state;
    this.sql = state.storage.sql;
    this.db = createSqlStorageAdapter(this.sql);
    for (const stmt of TELEMETRY_SCHEMA_STATEMENTS) {
      this.sql.exec(stmt);
    }
    for (const badId of REJECTED_UUIDS) {
      this.sql.exec(`DELETE FROM devices WHERE id = ?`, badId);
    }
    this.sql.exec(`DELETE FROM daily_history WHERE day = date('now')`);
    this.sql.exec(
      `INSERT INTO daily_history (day, project, version, count)
       SELECT date('now'), project, version, COUNT(*)
       FROM devices
       WHERE last_seen >= unixepoch() - ?
       GROUP BY project, version`,
      ACTIVE_WINDOW_SECONDS
    );
  }

  async fetch(request) {
    const url = new URL(request.url);
    if (request.method === "POST" && url.pathname === "/upsert") {
      const record = await request.json();
      await upsertDeviceAndSnapshot(this.db, record);
      return jsonResponse({ ok: true });
    }
    if (request.method === "POST" && url.pathname === "/stats") {
      const { project, ghReleases } = await request.json();
      const stats = await buildProjectStats(this.db, project, ghReleases || []);
      return jsonResponse(stats);
    }
    if (request.method === "POST" && url.pathname === "/cron") {
      for (const project of VALID_PROJECTS) {
        await refreshDailyHistoryForProject(this.db, project);
      }
      return jsonResponse({ ok: true });
    }
    return jsonResponse({ error: "Not found" }, 404);
  }
}

function getTelemetryStub(env) {
  if (!env?.TELEMETRY_DO) return null;
  const id = env.TELEMETRY_DO.idFromName("global");
  return env.TELEMETRY_DO.get(id);
}

export default {
  /**
   * HTTP request entrypoint.
   * Deliberately never inspects client IP headers.
   */
  async fetch(request, env, ctx) {
    if (request.method === "OPTIONS") {
      return new Response(null, { status: 204, headers: CORS_HEADERS });
    }

    if (request.method !== "GET") {
      return jsonResponse({ error: "Method not allowed" }, 405);
    }

    const url = new URL(request.url);
    const pathname = url.pathname.replace(/\/+$/, "") || "/";

    // Route 1: GET /v1/ota/releases
    if (pathname === "/v1/ota/releases") {
      const params = sanitizeCheckinParams(url.searchParams);
      if (!params.project) {
        return jsonResponse(
          { error: "Invalid or missing project parameter 'p' (esp32 | rpi)" },
          400
        );
      }

      let upsertPromise = Promise.resolve();
      if (params.canRecordTelemetry) {
        if (env?.DB) {
          upsertPromise = upsertDeviceAndSnapshot(env.DB, params).catch((err) => {
            console.error("D1 telemetry upsert failed:", err);
          });
        } else {
          const stub = getTelemetryStub(env);
          if (stub) {
            upsertPromise = stub
              .fetch("https://telemetry.internal/upsert", {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(params),
              })
              .then((r) => r.arrayBuffer())
              .catch((err) => {
                console.error("DO SQLite telemetry upsert failed:", err);
              });
          }
        }
      }

      try {
        const fetchFn = env?.__mockFetch || fetch;
        const [, releases] = await Promise.all([
          upsertPromise,
          fetchGitHubReleases(params.project, fetchFn),
        ]);
        return jsonResponse(releases, 200, 600);
      } catch (err) {
        await upsertPromise;
        return jsonResponse(
          { error: "Failed to fetch upstream GitHub releases", detail: String(err.message || err) },
          502
        );
      }
    }

    // Route 2: GET /v1/stats
    if (pathname === "/v1/stats") {
      const project = (url.searchParams.get("p") || "").trim().toLowerCase();
      if (!VALID_PROJECTS.has(project)) {
        return jsonResponse(
          { error: "Invalid or missing project parameter 'p' (esp32 | rpi)" },
          400
        );
      }

      const stub = !env?.DB ? getTelemetryStub(env) : null;
      if (!env?.DB && !stub) {
        return jsonResponse(
          { error: "Telemetry storage binding ('DB' or 'TELEMETRY_DO') unavailable" },
          503
        );
      }

      let ghReleases = [];
      try {
        const fetchFn = env?.__mockFetch || fetch;
        ghReleases = await fetchGitHubReleases(project, fetchFn);
      } catch (_) {
        // Non-fatal: still serve fleet statistics even if GitHub API is temporarily unreachable
      }

      if (env?.DB) {
        const stats = await buildProjectStats(env.DB, project, ghReleases);
        return jsonResponse(stats, 200, 0);
      }

      const doResp = await stub.fetch("https://telemetry.internal/stats", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ project, ghReleases }),
      });
      const stats = await doResp.json();
      return jsonResponse(stats, 200, 0);
    }

    // Health / root info endpoint
    if (pathname === "/" || pathname === "/health") {
      return jsonResponse(
        {
          service: "arcadematrix-ota-stats",
          status: "ok",
          storage: env?.DB ? "d1" : env?.TELEMETRY_DO ? "durable-object-sqlite" : "none",
          endpoints: [
            "/v1/ota/releases?p=esp32|rpi&id=<uuid-v4>&v=<ver>&hw=<hw>&os=<os>&res=<res>",
            "/v1/stats?p=esp32|rpi",
          ],
          privacy: "Zero IP logging. Anonymous RFC 4122 UUID v4 UPSERT only.",
        },
        200,
        60
      );
    }

    return jsonResponse({ error: "Not found" }, 404);
  },

  /**
   * Nightly cron handler ("5 0 * * *") to record daily_history snapshots for both projects.
   */
  async scheduled(_event, env, _ctx) {
    if (env?.DB) {
      for (const project of VALID_PROJECTS) {
        await refreshDailyHistoryForProject(env.DB, project);
      }
      return;
    }
    const stub = getTelemetryStub(env);
    if (stub) {
      await stub.fetch("https://telemetry.internal/cron", { method: "POST" });
    }
  },
};
