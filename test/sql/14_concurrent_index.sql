--
-- Test for CREATE INDEX CONCURRENTLY on pgtt
--

-- Start with a clean slate to ensure consistent output
SET client_min_messages TO WARNING;
DROP EXTENSION IF EXISTS pgtt CASCADE;
CREATE EXTENSION pgtt;
RESET client_min_messages;

-- 1. Create the Global Temporary Table
CREATE GLOBAL TEMPORARY TABLE my_gtt_concurrent (
	id int,
	lbl text
) ON COMMIT PRESERVE ROWS;

-- 2. Activate the GTT
-- We insert a row so that the local temporary table is physically created.
-- This forces the namespace check in pgtt.c to see a "pg_temp" namespace.
INSERT INTO my_gtt_concurrent VALUES (1, 'initial_data');

-- 3. Negative Test: Standard CREATE INDEX
-- This should still FAIL because the GTT is active and we are not using CONCURRENTLY.
-- The extension preserves the error here: if (!stmt->concurrent) ereport(ERROR...
\set ON_ERROR_STOP 0
CREATE INDEX idx_gtt_fail ON my_gtt_concurrent (id);
\set ON_ERROR_STOP 1

-- 4. Positive Test: CREATE INDEX CONCURRENTLY
-- This should SUCCEED. The extension bypasses the error when stmt->concurrent is true
-- and uses ShareUpdateExclusiveLock.
CREATE INDEX CONCURRENTLY idx_gtt_success ON my_gtt_concurrent (id);

-- Verify the index is actually there and valid
SELECT
    CASE
        WHEN schemaname LIKE 'pg_temp_%' THEN 'pg_temp'
        ELSE schemaname
    END AS schemaname,
    tablename,
    indexname,
    regexp_replace(
        replace(indexdef, 'public.', 'pg_temp.'),
        'pg_temp_\d+',
        'pg_temp',
        'g'
    ) as indexdef
FROM pg_indexes
WHERE tablename = 'my_gtt_concurrent'
ORDER BY indexname;

-- 5. Test REINDEX CONCURRENTLY
-- Assuming the implementation handles ReindexStmt similarly to IndexStmt,
-- or it relies on standard PG behavior for the reindex once the index exists.
REINDEX INDEX CONCURRENTLY idx_gtt_success;

-- No cleanup needed: pg_regress will destroy the environment.
