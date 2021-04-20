* [Description](#description)
* [Installation](#installation)
* [Configuration](#configuration)
* [Use of the extension](#use-of-the-extension)
* [How the extension really works](#how-the-extension-really-works)
* [Performances](performances)
* [Authors](#authors)

## PostgreSQL Global Temporary Tables

### [Description](#description)

pgtt is a PostgreSQL extension to create, manage and use Oracle-style
Global Temporary Tables and the others RDBMS.

The objective of this extension it to propose an extension to provide
the Global Temporary Table feature waiting for an in core
implementation. The main interest of this extension is to mimic the
Oracle behavior with GTT when you can not or don't want to rewrite the
application code when migrating to PostgreSQL. In all other case best
is to rewrite the code to use standard PostgreSQL temporary tables.

This version of the GTT extension use a regular unlogged table as
"template" table and an internal rerouting to a temporary table. See
chapter "How the extension really works?" for more details. A previous
implementation of this extension using Row Security Level is still
available [here](https://github.com/darold/pgtt-rsl).

PostgreSQL native temporary tables are automatically dropped at the
end of a session, or optionally at the end of the current transaction.
Global Temporary Tables (GTT) are permanent, they are created
as regular tables visible to all users but their content is relative
to the current session or transaction. Even if the table is persistent
a session or transaction can not see rows written by an other session.

Usually this is not a problem, you have learn to deal with the
temporary table behavior of PostgreSQL but the problem comes when
you are migrating an Oracle database to PostgreSQL. You have to
rewrite the SQL and PlPgSQL code to follow the application logic and
use PostgreSQL temporary table, that mean recreating the temporary
table everywhere it is used.

The other advantage of this kind of object is when your application
creates and drops a lot of temporary tables, the PostgreSQL catalogs
becomes bloated and the performances start to fall. Usually Global
Temporary Tables prevent catalog bloating, but with this implementation
and even if we have a permanent table, all DML are rerouted to a
regular temporary table created at first access. See below chapter
"How the extension really works?" for more information.

DECLARE TEMPORARY TABLE statement is not supported by PostgreSQL and
by this extension. However this statement defines a temporary table
for the current connection / session, it creates tables that do not
reside in the system catalogs and are not persistent. It cannot be
shared with other sessions. This is the equivalent of PostgreSQL
standard CREATE TEMPORARY TABLE so you might just have to replace the
DECLARE keyword by CREATE.

All Oracle's GTT behavior are respected with the different clauses
minus what is not supported by PostgreSQL:

#### ON COMMIT {DELETE | PRESERVE} ROWS

Specifies the action taken on the global temporary table when a COMMIT
operation is performed.
  - DELETE ROWS: all rows of the table will be deleted if no holdable
  cursor is open on the table.
  - PRESERVE ROWS: the rows of the table will be preserved after the
  COMMIT.

#### LOGGED or NOT LOGGED [ ON ROLLBACK {DELETE | PRESERVE} ROWS ]

Specifies whether operations for the table are logged. The default is
`NOT LOGGED ON ROLLBACK DELETE ROWS`.

* NOT LOGGED: Specifies that insert, update, or delete operations
  against the table are not to be logged, but that the creation or
  dropping of the table is to be logged. During a ROLLBACK or ROLLBACK
  TO SAVEPOINT operation:

  - If the table had been created within a transaction, the table is
  dropped
  - If the table had been dropped within a transaction, the table is
    recreated, but without any data

* ON ROLLBACK: Specifies the action that is to be taken on the not
 logged created temporary table when a ROLLBACK or ROLLBACK TO
 SAVEPOINT operation is performed. The default is DELETE ROWS.

  - DELETE ROWS: if the table data has been changed, all the rows will
  be deleted.
  - PRESERVE ROWS: rows of the table will be preserved.

* LOGGED: specifies that insert, update, or delete operations against
 the table as well as the creation or dropping of the table are to be
 logged.

With PostgreSQL only `NOT LOGGED ON ROLLBACK DELETE ROWS` can be
supported. Creation or dropping of the Global Temporary Table are
logged, see below "How the extension really works?" for the details.


### [Installation](#installation)

To install the pgtt extension you need at least a PostgreSQL version
9.5. Untar the pgtt tarball anywhere you want then you'll need to
compile it with pgxs.  The `pg_config` tool must be in your path.

Depending on your installation, you may need to install some devel
package. Once `pg_config` is in your path, do

	make
	sudo make install

If the extension will be used by a non-superuser role, you need to
add the library to the $libdir/plugins/ directory.

	export libdir=$(pg_config --pkglibdir)
	sudo mkdir $libdir/plugins/
	cd $libdir/plugins/
	sudo ln -s ../pgtt.so

Then it will be possible to use it using `LOAD '$libdir/plugins/pgtt.so';`
To create and manage GTT using a non-superuser role you will have to grant
the CREATE privilege on the `pgtt_schema` schema to the user.
	
To run test execute the following command as superuser:

	make installcheck

An additional standalone test is provided to test the use of the
extension in a dedicated schema "SESSION". The requisite is to
remove these two lines from the pgtt.control file:

	schema = 'pgtt_schema'
	relocatable = false

Then the tests can be executed using:

	mkdir results
	createdb gtt_relocation
	psql -d gtt_relocation -f test/relocation.sql > results/relocation.out 2>&1
	diff results/relocation.out test/expected/relocation.out
	dropdb gtt_relocation


### [Configuration](#configuration)

- *pgtt.enabled*

The extension can be enable / disable using this GUC, default is
enabled. To disable the extension use:

	SET pgtt.enabled TO off;

You can disable or enable the extension at any moment in a session.

### [Use of the extension](#use-of-the-extension)

In all database where you want to use Global Temporary Tables you
will have to create the extension using:

	CREATE EXTENSION pgtt;

As a superuser you can load the extension using:

	LOAD 'pgtt';

non-superuser must load the library using the plugins/ directory
as follow:

	LOAD '$libdir/plugins/pgtt';

Take care to follow installation instruction above to create the
symlink from the plugins/ directory to the extension library file.

The pgtt extension use a dedicated schema to store related objects,
by default: `pgtt_schema`. The extension take care that this schema
is always at end of the `search_path`.

	gtt_testdb=# LOAD '$libdir/plugins/pgtt';
	LOAD
	gtt_testdb=# SHOW search_path;
	    search_path     
	--------------------
	 public,pgtt_schema
	(1 row)

	gtt_testdb=# SET search_path TO appschema,public;
	SET
	gtt_testdb=# SHOW search_path;
		  search_path           
	--------------------------------
	 appschema, public, pgtt_schema
	(1 row)

The pgtt schema is automatically added to the search_path when you
load the extension and if you change the `search_path` later.

#### Create a Global Temporary Table

To create a GTT table named "test_table" use the following statement:

	CREATE GLOBAL TEMPORARY TABLE test_gtt_table (
		id integer,
		lbl text
	) ON COMMIT { PRESERVE | DELETE } ROWS;


The GLOBAL keyword is obsolete but can be used safely, the only thing
is that it will generate a warning:

	WARNING:  GLOBAL is deprecated in temporary table creation

If you don't want to be annoyed by this warning message you can use
it like a comment instead:

	CREATE /*GLOBAL*/ TEMPORARY TABLE test_gtt_table (
		LIKE other_table LIKE
		INCLUDING DEFAULTS
		INCLUDING CONSTRAINTS
		INCLUDING INDEXES
	) ON COMMIT { PRESERVE | DELETE } ROWS;

the extension will detect the GLOBAL keyword.

As you can see in the example above the LIKE clause is supported,
as well as the AS clause WITH DATA or WITH NO DATA (default):

	CREATE /*GLOBAL*/ TEMPORARY TABLE test_gtt_table
	AS SELECT * FROM source_table WITH DATA;

In case of WITH DATA, the extension will fill the GTT with data
returned from the SELECT statement for the current session only.
 
PostgreSQL temporary table clause `ON COMMIT DROP` is not supported by
the extension, GTT are persistent over transactions. If the clause is
used an error will be raised.

Temporary table rows are deleted or preserved at transactions commit
following the clause:

	ON COMMIT { PRESERVE | DELETE } ROWS

#### Drop a Global Temporary Table

To drop a Global Temporary Table you just proceed as for a normal
table:

	DROP TABLE test_gtt_table;

A Global Temporary Table can be dropped even if it is used by other session.

#### Create index on Global Temporary Table

You can create indexes on the global temporary table:

	CREATE INDEX ON test_gtt_table (id);

just like with any other tables.

#### Constraints on Global Temporary Table

You can add any constraint on a Global Temporary Table except FOREIGN KEYS.

	CREATE GLOBAL TEMPORARY TABLE t2 (
		c1 serial PRIMARY KEY,
		c2 VARCHAR (50) UNIQUE NOT NULL,
		c3 boolean DEFAULT false
	)

The use of FOREIGN KEYS in a Global Temporary Table is not allowed.

	CREATE GLOBAL TEMPORARY TABLE t1 (c1 integer, FOREIGN KEY (c1) REFERENCES source (id));
	ERROR:  attempt to create referential integrity constraint on global temporary table

	ALTER TABLE t2 ADD FOREIGN KEY (c1) REFERENCES source (id);
	ERROR:  attempt to create referential integrity constraint on global temporary table

Even if PostgreSQL allow foreign keys on temporary table, the pgtt extension try
to mimic as much as possible the same behavior of Oracle and other RDBMS like DB2,
SQL Server or MySQL.

	ORA-14455: attempt to create referential integrity constraint on temporary table.  

#### Partitioning

Partitioning on Global Temporary Table is not supported, again not because
PostgreSQL do not allow partition on temporary table but because other RDBMS
like Oracle, DB2 and MySQL do not support it. SQL Server supports partition
on global temporary table.


### [How the extension really works](#how-the-extension-really-works)

#### Global Temporary Table use

When `pgtt.enabled` is true (default) and the extension have been
loaded (`LOAD 'pgtt';`) the first access to the table using a SELECT,
UPDATE or DELETE statement will produce the creation of a temporary
table using the definition of the "template" table created during
the call to `CREATE GLOBAL TEMPORARY TABLE` statement.

Once the temporary table is created at the first access, the original
SELECT, UPDATE or DELETE statement is automatically rerouted to the
new regular temporary table. All other access will use the new
temporary table, the `pg_temp*` schema where the table is created is
always looked first in the search path this is why the "template"
table is not concerned by subsequent access.

Creating, renaming and removing a GTT is an administration task it
shall not be done in an application session.

Note that rerouting is active even if you add a namespace qualifier
to the table. For example looking at the internal unlogged template
table:

	bench=# LOAD 'pgtt';
	LOAD
	bench=# CREATE /*GLOBAL*/ TEMPORARY TABLE test_tt (id int, lbl text) ON COMMIT PRESERVE ROWS;
	CREATE TABLE
	bench=# INSERT INTO test_tt VALUES (1, 'one'), (2, 'two'), (3, 'three');
	INSERT 0 3
	bench=# SELECT * FROM pgtt_schema.test_tt;
	 id |  lbl  
	----+-------
	  1 | one
	  2 | two
	  3 | three
	(3 rows)

will actually result in the same as looking at the associated
temporary table like follow:

	bench=# SELECT * FROM test_tt;
	 id |  lbl  
	----+-------
	  1 | one
	  2 | two
	  3 | three
	(3 rows)

or

	bench=# SELECT * FROM pg_temp.test_tt;
	 id |  lbl  
	----+-------
	  1 | one
	  2 | two
	  3 | three
	(3 rows)

If you want to really look at the template table to be sure that
it contains no rows, you must disable the extension rerouting:

	bench=# SET pgtt.enabled TO off;
	SET
	bench=# SELECT * FROM pgtt_schema.test_tt;
	 id | lbl 
	----+-----
	(0 rows)

	bench=# SET pgtt.enabled TO on;
	SET
	bench=# SELECT * FROM pgtt_schema.test_tt;
	 id |  lbl  
	----+-------
	  1 | one
	  2 | two
	  3 | three
	(3 rows)

Look at test file for more examples.

This also mean that you can relocated the extension in a dedicated
namespace. Can be useful if your application's queries use the schema
qualifier with the table name to access to the GTT and you can't change
it. See t/sql/relocation.sql for an example. By default the extension
is not relocatable in an other schema, there is some configuration
change to perform to be able to use this feature.

If you use the CREATE AS form with the WITH DATA clause like in this
example:

	CREATE /*GLOBAL*/ TEMPORARY TABLE test_gtt_table
	AS SELECT * FROM source_table WITH DATA;

the extension will first create the template unlogged table and will
create immediately the associated temporary table filled with all data
returned by the SELECT statement. The first access will not have to
create the table it already exists with data.

#### Table creation

The extension intercept the call to `CREATE TEMPORARY TABLE ...`
statement and look if there is the keyword `GLOBAL` or the comment
`/*GLOBAL*/`. When it is found, instead of creating the temporary
table, it creates a "template" unlogged persistent table following
the temporary table definition. When the template is created it
registers the table into a "catalog" table `pg_global_temp_tables`.

Both objects are created in the extension schema `pgtt_schema`.

When `pgtt.enabled` is false nothing is done.

Here is the description of the catalog table:

```
          Table « pgtt_schema.pg_global_temp_tables »
  Colonne  |  Type   | Collationnement | NULL-able | Par défaut 
-----------+---------+-----------------+-----------+------------
 relid     | integer |                 | not null  | 
 nspname   | name    |                 | not null  | 
 relname   | name    |                 | not null  | 
 preserved | boolean |                 |           | 
 code      | text    |                 |           | 
Index :
    "pg_global_temp_tables_nspname_relname_key" UNIQUE CONSTRAINT, btree (nspname, relname)
```

* `relid`: Oid of the "template" unlogged table.
* `nspname`: namespace of the extension `pgtt_schema` by default.
* `relname`: name of the GTT relation.
* `preserved`: true or false for `ON COMMIT { PRESERVE | DELETE}`.
* `code`: code used at Global Temporary Table creation time.


#### Table removing

The extension intercept the call to `DROP TABLE` and look in the
`pg_global_temp_tables` table to see if it is declared. When it is
found it drops the template unlogged table and the corresponding
entry from the pgtt catalog table `pg_global_temp_tables`.

When `pgtt.enabled` is false nothing is done.

Dropping a GTT that is in use, when the temporary table has already
been created, will raise an error. This is not allowed.


#### Table renaming

The extension intercept the call to `ALTER TABLE ... RENAME` and look
in the `pg_global_temp_tables` table to see if it is declared. When it
is found it renames the "template" table and update the name of the
relation in the `pg_global_temp_tables` table. If the GTT has already
been used in the session the corresponding temporary table exists, in
this case the extension will refuse to rename it. It must be inactive
to be renamed.

When `pgtt.enabled` is false nothing is done.

Renaming a GTT that is in use, when the temporary table has already
been created, will raise an error. This is not allowed.

#### pg_dump / pg_restore

When dumping a database using the pgtt extension, the content of the
"catalog" table `pg_global_temp_tables` will be dumped as well as
all template unlogged tables. Restoring the dump will recreate the
database in the same state.

### [Performances](performances)

Overhead of loading the extension but without using it in a pgbench
tpcb-like scenario.

* Without loading the extension

```
$ pgbench -h localhost bench -c 20 -j 4 -T 60 -f test/bench/bench_noload.sql
starting vacuum...end.
transaction type: test/bench/bench_noload.sql
scaling factor: 1
query mode: simple
number of clients: 20
number of threads: 4
duration: 60 s
number of transactions actually processed: 51741
latency average = 23.201 ms
tps = 862.038042 (including connections establishing)
tps = 862.165341 (excluding connections establishing)
```

* With loading the extension

```
$ pgbench -h localhost bench -c 20 -j 4 -T 60 -f test/bench/bench_load.sql
starting vacuum...end.
transaction type: test/bench/bench_load.sql
scaling factor: 1
query mode: simple
number of clients: 20
number of threads: 4
duration: 60 s
number of transactions actually processed: 51171
latency average = 23.461 ms
tps = 852.495877 (including connections establishing)
tps = 852.599010 (excluding connections establishing)
```

Now a test between using a regular temporary table and a PGTT in the
pgbench tpcb-like scenario.

* Using a regular Temporary Table

```
$ pgbench -h localhost bench -c 20 -j 4 -T 60 -f test/bench/bench_use_rtt.sql 
starting vacuum...end.
transaction type: test/bench/bench_use_rtt.sql
scaling factor: 1
query mode: simple
number of clients: 20
number of threads: 4
duration: 60 s
number of transactions actually processed: 17153
latency average = 70.058 ms
tps = 285.477860 (including connections establishing)
tps = 285.514186 (excluding connections establishing)
```

* Using a Global Temporary Table

Created using:

	CREATE GLOBAL TEMPORARY TABLE test_tt (id int, lbl text)
			ON COMMIT DELETE ROWS;

```
$ pgbench -h localhost bench -c 20 -j 4 -T 60 -f test/bench/bench_use_gtt.sql 
starting vacuum...end.
transaction type: test/bench/bench_use_gtt.sql
scaling factor: 1
query mode: simple
number of clients: 20
number of threads: 4
duration: 60 s
number of transactions actually processed: 17540
latency average = 68.495 ms
tps = 291.993502 (including connections establishing)
tps = 292.028832 (excluding connections establishing)
```

Even if this last test shows a significant performances improvement
comparing to regular temporary tables, most of the time this will
not be the case. 

### [Authors](#authors)

Gilles Darold
gilles@darold.net

### [License](#license)

This extension is free software distributed under the PostgreSQL
Licence.

        Copyright (c) 2018-2020, Gilles Darold

