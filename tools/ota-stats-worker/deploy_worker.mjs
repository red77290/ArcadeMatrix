#!/usr/bin/env node
/**
 * Zero-dependency, idempotent Cloudflare Worker deployment script for `arcadematrix-ota-stats`.
 *
 * Uses Cloudflare REST API v4 directly via Node 20+/22+ built-in `fetch` & `FormData`:
 *  1. Auto-discovers `CLOUDFLARE_ACCOUNT_ID` via `/accounts` when not explicitly provided.
 *  2. Ensures a `*.workers.dev` subdomain exists (creates `red77290` if none exists yet).
 *  3. Auto-provisions storage:
 *     - Uses Cloudflare D1 (`arcadematrix-ota-db` + `schema.sql`) when token has `D1: Edit`.
 *     - Automatically falls back to SQLite-backed Durable Object (`TelemetryStore`) when using
 *       Cloudflare's standard "Edit Cloudflare Workers" token (`Workers Scripts: Edit`).
 *  4. Uploads ES module `src/index.js`, enables `workers.dev` route, sets nightly cron `5 0 * * *`,
 *     and verifies `/health`.
 */

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const CF_API = "https://api.cloudflare.com/client/v4";
const SCRIPT_NAME = "arcadematrix-ota-stats";
const DB_NAME = "arcadematrix-ota-db";
const DEFAULT_SUBDOMAIN = "red77290";

async function cfRequest(token, endpoint, init = {}) {
  const headers = new Headers(init.headers || {});
  headers.set("Authorization", `Bearer ${token}`);
  if (!(init.body instanceof FormData) && !headers.has("Content-Type")) {
    headers.set("Content-Type", "application/json");
  }
  const resp = await fetch(`${CF_API}${endpoint}`, { ...init, headers });
  const text = await resp.text();
  let json = null;
  try {
    json = JSON.parse(text);
  } catch (_) {
    json = { success: false, raw: text };
  }
  return { status: resp.status, ok: resp.ok && Boolean(json?.success), json };
}

async function resolveAccountId(token) {
  if (process.env.CLOUDFLARE_ACCOUNT_ID) {
    return process.env.CLOUDFLARE_ACCOUNT_ID.trim();
  }
  const { ok, status, json } = await cfRequest(token, "/accounts");
  if (!ok || !Array.isArray(json?.result) || json.result.length === 0) {
    throw new Error(
      `Unable to resolve Cloudflare Account ID (HTTP ${status}). Set CLOUDFLARE_ACCOUNT_ID explicitly.`
    );
  }
  return json.result[0].id;
}

async function ensureWorkersSubdomain(token, accountId) {
  const cur = await cfRequest(token, `/accounts/${accountId}/workers/subdomain`);
  if (cur.ok && cur.json?.result?.subdomain) {
    return cur.json.result.subdomain;
  }
  console.log(`🌐 Registering workers.dev subdomain '${DEFAULT_SUBDOMAIN}'...`);
  const created = await cfRequest(token, `/accounts/${accountId}/workers/subdomain`, {
    method: "PUT",
    body: JSON.stringify({ subdomain: DEFAULT_SUBDOMAIN }),
  });
  if (!created.ok || !created.json?.result?.subdomain) {
    throw new Error(
      `Failed to register workers.dev subdomain (HTTP ${created.status}): ${JSON.stringify(created.json?.errors || created.json)}`
    );
  }
  return created.json.result.subdomain;
}

async function tryProvisionD1(token, accountId) {
  const list = await cfRequest(token, `/accounts/${accountId}/d1/database`);
  if (!list.ok) {
    return null;
  }
  let db = (list.json.result || []).find((d) => d && d.name === DB_NAME && d.uuid);
  if (!db) {
    console.log(`➕ Creating Cloudflare D1 database '${DB_NAME}'...`);
    const created = await cfRequest(token, `/accounts/${accountId}/d1/database`, {
      method: "POST",
      body: JSON.stringify({ name: DB_NAME }),
    });
    if (!created.ok || !created.json?.result?.uuid) {
      throw new Error(
        `Failed to create D1 database '${DB_NAME}' (HTTP ${created.status}): ${JSON.stringify(created.json?.errors)}`
      );
    }
    db = created.json.result;
  }
  const schemaSql = fs.readFileSync(path.join(__dirname, "schema.sql"), "utf8");
  console.log(`🗄️  Applying idempotent D1 schema to '${DB_NAME}' (${db.uuid})...`);
  const applied = await cfRequest(
    token,
    `/accounts/${accountId}/d1/database/${db.uuid}/query`,
    {
      method: "POST",
      body: JSON.stringify({ sql: schemaSql }),
    }
  );
  if (!applied.ok) {
    throw new Error(
      `Failed to apply D1 schema (HTTP ${applied.status}): ${JSON.stringify(applied.json?.errors)}`
    );
  }
  return db.uuid;
}

async function hasExistingTelemetryDo(token, accountId) {
  const list = await cfRequest(
    token,
    `/accounts/${accountId}/workers/durable_objects/namespaces`
  );
  if (!list.ok || !Array.isArray(list.json?.result)) {
    return false;
  }
  return list.json.result.some(
    (ns) => ns && ns.script === SCRIPT_NAME && ns.class === "TelemetryStore"
  );
}

async function main() {
  const token = (process.env.CLOUDFLARE_API_TOKEN || "").trim();
  if (!token) {
    console.error("❌ CLOUDFLARE_API_TOKEN is not set.");
    process.exit(1);
  }

  const accountId = await resolveAccountId(token);
  const subdomain = await ensureWorkersSubdomain(token, accountId);

  const d1Uuid = await tryProvisionD1(token, accountId);
  const bindings = [];
  const metadata = {
    main_module: "index.js",
    compatibility_date: "2024-11-01",
    bindings,
  };

  if (d1Uuid) {
    console.log(`  ✓ Using Cloudflare D1 binding 'DB' (${d1Uuid}).`);
    bindings.push({ type: "d1", name: "DB", id: d1Uuid });
  } else {
    const exists = await hasExistingTelemetryDo(token, accountId);
    console.log(
      `  ✓ D1 scope not present on token — using SQLite Durable Object 'TELEMETRY_DO' (existing=${exists}).`
    );
    bindings.push({
      type: "durable_object_namespace",
      name: "TELEMETRY_DO",
      class_name: "TelemetryStore",
    });
    if (exists) {
      metadata.migration_tag = "v1";
    } else {
      metadata.migrations = {
        new_tag: "v1",
        new_sqlite_classes: ["TelemetryStore"],
      };
    }
  }

  const scriptSource = fs.readFileSync(
    path.join(__dirname, "src", "index.js"),
    "utf8"
  );
  const form = new FormData();
  form.append(
    "metadata",
    new Blob([JSON.stringify(metadata)], { type: "application/json" }),
    "metadata.json"
  );
  form.append(
    "index.js",
    new Blob([scriptSource], { type: "application/javascript+module" }),
    "index.js"
  );

  console.log(`🚀 Uploading Worker '${SCRIPT_NAME}'...`);
  const upload = await cfRequest(
    token,
    `/accounts/${accountId}/workers/scripts/${SCRIPT_NAME}`,
    {
      method: "PUT",
      body: form,
    }
  );
  if (!upload.ok) {
    throw new Error(
      `Worker upload failed (HTTP ${upload.status}): ${JSON.stringify(upload.json?.errors || upload.json)}`
    );
  }

  console.log(`🔗 Enabling https://${SCRIPT_NAME}.${subdomain}.workers.dev ...`);
  const subEnable = await cfRequest(
    token,
    `/accounts/${accountId}/workers/scripts/${SCRIPT_NAME}/subdomain`,
    {
      method: "POST",
      body: JSON.stringify({ enabled: true }),
    }
  );
  if (!subEnable.ok) {
    throw new Error(
      `Enabling workers.dev route failed (HTTP ${subEnable.status}): ${JSON.stringify(subEnable.json?.errors)}`
    );
  }

  console.log("⏰ Configuring nightly cron trigger '5 0 * * *'...");
  const cronRes = await cfRequest(
    token,
    `/accounts/${accountId}/workers/scripts/${SCRIPT_NAME}/schedules`,
    {
      method: "PUT",
      body: JSON.stringify([{ cron: "5 0 * * *" }]),
    }
  );
  if (!cronRes.ok) {
    throw new Error(
      `Cron trigger setup failed (HTTP ${cronRes.status}): ${JSON.stringify(cronRes.json?.errors)}`
    );
  }

  const liveUrl = `https://${SCRIPT_NAME}.${subdomain}.workers.dev`;
  console.log(`✅ Deployed successfully: ${liveUrl}`);

  try {
    const health = await fetch(`${liveUrl}/health`);
    console.log(`🩺 Health check (${health.status}):`, await health.text());
  } catch (err) {
    console.warn("ℹ️ Worker deployed (DNS propagation may take a few seconds):", err.message);
  }
}

main().catch((err) => {
  console.error("❌ Cloudflare Worker deployment failed:", err.message || err);
  process.exit(1);
});
