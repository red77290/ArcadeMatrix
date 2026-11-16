-- ArcadeMatrix Unified OTA Stats & Releases Proxy D1 Schema
-- Privacy-by-design: Zero IP addresses, zero MAC addresses, zero PII.
-- Each matrix is identified solely by a locally generated RFC 4122 UUID v4 (36 chars).

CREATE TABLE IF NOT EXISTS devices (
    id TEXT PRIMARY KEY CHECK (length(id) = 36),
    project TEXT NOT NULL CHECK (project IN ('esp32', 'rpi')),
    version TEXT NOT NULL,
    hw TEXT NOT NULL,
    os TEXT NOT NULL DEFAULT '',
    res TEXT NOT NULL DEFAULT '',
    first_seen INTEGER NOT NULL,
    last_seen INTEGER NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_devices_project_last_seen
    ON devices (project, last_seen);

CREATE INDEX IF NOT EXISTS idx_devices_project_version
    ON devices (project, version);

CREATE TABLE IF NOT EXISTS daily_history (
    day TEXT NOT NULL,
    project TEXT NOT NULL CHECK (project IN ('esp32', 'rpi')),
    version TEXT NOT NULL,
    count INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (day, project, version)
);

CREATE INDEX IF NOT EXISTS idx_daily_history_project_day
    ON daily_history (project, day);
