----
-- Regression test to Global Temporary Table implementation
--
-- Test for unsupported partitioning on GTT.
--
----
-- Import the library
LOAD 'pgtt';
-- Must throw ERROR:  Global Temporary Table do not support partitioning.
-- Note that this test always fail on PostgreSQL version lower that 10.
CREATE /*GLOBAL*/ TEMPORARY TABLE measurement (
    logdate         date not null,
    peaktemp        int,
    unitsales       int
) PARTITION BY RANGE (logdate);
