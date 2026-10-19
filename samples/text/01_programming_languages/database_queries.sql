-- WinPreview Metrics & Analytics Schema
CREATE TABLE IF NOT EXISTS preview_sessions (
    session_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    file_path VARCHAR(1024) NOT NULL,
    file_extension VARCHAR(32) NOT NULL,
    file_size_bytes BIGINT NOT NULL,
    load_time_ms NUMERIC(8, 2) NOT NULL,
    was_modified BOOLEAN DEFAULT FALSE,
    opened_at TIMESTAMPTZ DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_preview_sessions_ext 
ON preview_sessions (file_extension);

-- Query: Average load time grouped by document extension
SELECT 
    file_extension,
    COUNT(*) AS total_opens,
    ROUND(AVG(load_time_ms), 2) AS avg_load_ms,
    ROUND(MAX(load_time_ms), 2) AS max_load_ms,
    SUM(CASE WHEN was_modified THEN 1 ELSE 0 END) AS edits_count
FROM preview_sessions
WHERE opened_at >= CURRENT_DATE - INTERVAL '30 days'
GROUP BY file_extension
ORDER BY total_opens DESC;
