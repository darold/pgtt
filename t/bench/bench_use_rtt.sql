\set aid random(1, 100000 * :scale)
\set bid random(1, 1 * :scale)
\set tid random(1, 10 * :scale)
\set delta random(-5000, 5000)

SET client_min_messages TO error;

CREATE TEMPORARY TABLE IF NOT EXISTS test_tt (id integer, lbl varchar) ON COMMIT DELETE ROWS;

BEGIN;
INSERT INTO test_tt (id, lbl) SELECT i, md5(i::text) FROM generate_series(1, 1000) i;
UPDATE pgbench_accounts SET abalance = abalance + :delta WHERE aid = :aid;
SELECT abalance FROM pgbench_accounts WHERE aid = :aid;
UPDATE pgbench_tellers SET tbalance = tbalance + :delta WHERE tid = :tid;
UPDATE pgbench_branches SET bbalance = bbalance + :delta WHERE bid = :bid;
UPDATE test_tt SET id = id+100 WHERE id > 500;
INSERT INTO pgbench_history (tid, bid, aid, delta, mtime) VALUES (:tid, :bid, :aid, :delta, CURRENT_TIMESTAMP);
SELECT * FROM test_tt ORDER BY id DESC LIMIT 10;
END;

