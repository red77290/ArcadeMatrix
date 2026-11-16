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
 * Validates that a string is a strict 36-character RFC 4122 UUID v4.
 * @param {string} id
 * @returns {boolean}
 */
export function isValidUuidV4(id) {
  return typeof id === "string" && id.length === 36 && UUID_V4_REGEX.test(id);
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

  // 7. GitHub releases download aggregation
  let githubDownloadsTotal = 0;
  const github_releases = (
    Array.isArray(githubReleasesRaw) ? githubReleasesRaw : []
  ).map((rel) => {
    const assets = Array.isArray(rel.assets) ? rel.assets : [];
    const downloads = assets.reduce(
      (sum, asset) => sum + Number(asset.download_count || 0),
      0
    );
    githubDownloadsTotal += downloads;
    return {
      tag: rel.tag_name || rel.name || "unknown",
      published_at: rel.published_at || null,
      downloads,
    };
  });

  const latestVersion =
    github_releases[0]?.tag || versions[0]?.version || "v3.0.1";
  const latestEntry =
    versions.find((v) => v.version === latestVersion) || versions[0] || null;
  const latestAdoptionPct = latestEntry ? latestEntry.pct : 0;

  return {
    project,
    updated_at: new Date().toISOString(),
    kpis: {
      active_30d: active30dTotal,
      total_known: totalKnown,
      latest_version: latestEntry ? latestEntry.version : latestVersion,
      latest_adoption_pct: latestAdoptionPct,
      github_downloads_total: githubDownloadsTotal,
    },
    versions,
    hardware,
    os_arch,
    resolutions,
    hw_by_res,
    daily_history,
    github_releases,
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

      if (env?.DB && params.canRecordTelemetry) {
        const promise = upsertDeviceAndSnapshot(env.DB, params).catch((err) => {
          console.error("D1 telemetry upsert failed:", err);
        });
        if (ctx && typeof ctx.waitUntil === "function") {
          ctx.waitUntil(promise);
        } else {
          await promise;
        }
      }

      try {
        const fetchFn = env?.__mockFetch || fetch;
        const releases = await fetchGitHubReleases(params.project, fetchFn);
        return jsonResponse(releases, 200, 600);
      } catch (err) {
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

      if (!env?.DB) {
        return jsonResponse({ error: "D1 database binding 'DB' unavailable" }, 503);
      }

      let ghReleases = [];
      try {
        const fetchFn = env?.__mockFetch || fetch;
        ghReleases = await fetchGitHubReleases(project, fetchFn);
      } catch (_) {
        // Non-fatal: still serve D1 fleet statistics even if GitHub API is temporarily unreachable
      }

      const stats = await buildProjectStats(env.DB, project, ghReleases);
      return jsonResponse(stats, 200, 900);
    }

    // Health / root info endpoint
    if (pathname === "/" || pathname === "/health") {
      return jsonResponse(
        {
          service: "arcadematrix-ota-stats",
          status: "ok",
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
    if (!env?.DB) return;
    for (const project of VALID_PROJECTS) {
      await refreshDailyHistoryForProject(env.DB, project);
    }
  },
};
