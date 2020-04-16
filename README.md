PostgreSQL Global Temporary Tables
==================================

pgtt is a PostgreSQL extension to create, manage and use Oracle-style
Global Temporary Tables and probably also BD2-style.

The objective of this extension it to propose an extension to provide
the Global Temporary Table feature waiting for an in core
implementation. The main interest of this extension is to mimic the
Oracle behavior with GTT when you can not or don't want to rewrite the
application code when migrating to PostrgeSQL. In all other case best
is to rewrite the code to use standard PostgreSQL temporary tables.

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
regular temporary table created at first acces. See below chapter
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


Installation
============

To install the pgtt extension you need at least a PostgreSQL version
9.5. Untar the pgtt tarball anywhere you want then you'll need to
compile it with pgxs.  The `pg_config` tool must be in your path.

Depending on your installation, you may need to install some devel
package. Once `pg_config` is in your path, do

	make
	sudo make install

To run test execute the following command:

	prove

An additional standalone test is provided to test the use of the
extension in a dedicated schema "SESSION". The requisite is to
remove these two lines from the pgtt.control file:

	schema = 'pgtt_schema'
	relocatable = false

Then the tests can be executed using:

	dropdb gtt_testdb
	createdb gtt_testdb
	psql gtt_testdb -f t/sql/relocation.sql > t/results/relocation.out 2>&1
	diff t/results/relocation.out t/expected/relocation.out


Configuration
=============

pgtt.enabled
------------

The extension can be enable / disable using this GUC, defaut is
enabled. To disable the extension use:

	SET pgtt.enabled TO false;

You can disable or enable the extension at any moment in a session.

Use of the extension
====================

In all database where you want to use Global Temporary Tables you
will have to create the extension using:

	CREATE EXTENSION pgtt;

The pgtt extension use a dedicated schema to store related objects,
by default: `pgtt_schema`. The extension take care that this schema
is always at end of the `search_path`.

	gtt_testdb=# LOAD 'pgtt';
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
		LIKE other_table
	) ON COMMIT { PRESERVE | DELETE } ROWS;

the extension will detect it.

As you can see in the exemple above the LIKE clause supported as well
as the AS clause WITH DATA or WITH NO DATA (default):

	CREATE /*GLOBAL*/ TEMPORARY TABLE test_gtt_table
	AS SELECT * FROM source_table WITH DATA;

PostgreSQL temporary table clause `ON COMMIT DROP` is not supported by
the extension, GTT are persistent over transactions. If the clause is
used an error will be raised. Only the table rows are deleted or
preserved following the clause:

	ON COMMIT { PRESERVE | DELETE } ROWS

To drop a Global Temporary Table you just proceed as for a normal
table:

	DROP TABLE test_gtt_table;

You can create indexes on the global temporary table:

	CREATE INDEX ON test_gtt_table (id);

just like with any other tables.


How the extension really works?
===============================

Global Temporary Table use
--------------------------

When `pgtt.enabled` is true (default) and the extension have been
loaded (`LOAD 'pgtt';`) the first access to the table using a SELECT,
UPDATE or DELETE statement will raise the creation of a temporary
table using the definition of the "template" table created when
issuing the `CREATE GLOBAL TEMPORARY TABLE` statement. Once it is
done the statement will be rerouted to the newly created temporary
table. All other access will use the new temporary table, the
`pg_temp*` schema where the table is created is always looked first
in the search path.

Creating, renaming and removing a GTT is an administration task it
shall not be done in a application session.

Note that rerouting is active even if you add a namespace qualifier
to the table. For example looking at the internal unlogged template
table:

    SELECT * FROM pgtt_schema.t1;

will actually result in the same as looking at the associated
temporary table like follow:

    SELECT * FROM t1;
    SELECT * FROM pg_temp.t1;

If you want to really look at the template table to be sure that it
contains no rows, you must disable the extension rerouting:

    SET pgtt.enable TO off;
    SELECT * FROM pgtt_schema.t1;
    SET pgtt.enable TO on;

Look at test file for more examples.

This also mean that you can relocated the extension in a dedicated
namespace. Can be useful if your application's queries use the schema
qualifier with the table name to acces to the GTT and you can't change
it. See t/sql/relocation.sql for an example. By default the extension
is not relocatable in an other schema, there is some configuration
change to perform to be able to use this feature.


Table creation
--------------

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


Table removing
--------------

The extension intercept the call to `DROP TABLE` and look in the
`pg_global_temp_tables` table to see if it is declared. When it is
found it drops the template unlogged table and the corresponding
entry from the pgtt catalog table `pg_global_temp_tables`.

When `pgtt.enabled` is false nothing is done.

Dropping a GTT that is in use, when the temporary table has already
been created, will raise an error. This is not allowed.


Table renaming
--------------

The extension intercept the call to `ALTER TABLE ... RENAME` and look
in the `pg_global_temp_tables` table to see if it is declared. When it
is found it renames the "template" table and update the name of the
relation in the `pg_global_temp_tables` table. If the GTT has already
been used in the session the corresponding temporary table exists, in
this case the extension will refuse to rename it. It must be inactive
to be renamed.

When `pgtt.enabled` is false nothing is done.

Remaming a GTT that is in use, when the temporary table has already
been created, will raise an error. This is not allowed.

pg_dump / pg_restore
--------------------

When dumping a database using the pgtt extension, the content of the
"catalog" table `pg_global_temp_tables` will be dumped as well as
all template unlogged tables. Restoring the dump will recreate the
database in the same state.


Authors
=======

Gilles Darold
gilles@darold.net

License
=======

This extension is free software distributed under the PostgreSQL
Licence.

        Copyright (c) 2018-2020, Gilles Darold

