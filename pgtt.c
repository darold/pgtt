/*-------------------------------------------------------------------------
 *
 * pgtt.c
 *	Add support to Oracle-style Global Temporary Table in PostgreSQL.
 *
 * Author: Gilles Darold <gilles@darold.net>
 * Licence: PostgreSQL
 * Copyright (c) 2018-2023, Gilles Darold,
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include <unistd.h>
#include "funcapi.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"

#include "access/htup_details.h"
#include "access/parallel.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_database.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_type.h"
#include "catalog/toasting.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/extension.h"
#include "commands/tablecmds.h"
#include "commands/comment.h"
#include "executor/spi.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "nodes/print.h"
#include "nodes/value.h"
#include "optimizer/paths.h"
#include "optimizer/plancat.h"
#include "parser/analyze.h"
#include "parser/parse_utilcmd.h"
#include "storage/ipc.h"
#include "storage/lmgr.h"
#include "storage/proc.h"
#include "tcop/utility.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/formatting.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"

#if PG_VERSION_NUM < 110000
#include "utils/memutils.h"
#endif

/* for regexp search */
#include "regex/regexport.h"

#if (PG_VERSION_NUM >= 120000)
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/pg_class.h"
#endif

#if PG_VERSION_NUM < 120000
#error Minimum version of PostgreSQL required is 12
#endif

#define CATALOG_GLOBAL_TEMP_REL	"pg_global_temp_tables"
#define Anum_pgtt_relid   1
#define Anum_pgtt_relname 3

PG_MODULE_MAGIC;

#define NOT_IN_PARALLEL_WORKER (ParallelWorkerNumber < 0)

#if PG_VERSION_NUM >= 140000
#define STMT_OBJTYPE(stmt) stmt->objtype
#else
#define STMT_OBJTYPE(stmt) stmt->relkind
#endif

/* Define ProcessUtility hook proto/parameters following the PostgreSQL version */
#if PG_VERSION_NUM >= 140000
#define GTT_PROCESSUTILITY_PROTO PlannedStmt *pstmt, const char *queryString, \
				       bool readOnlyTree, \
					ProcessUtilityContext context, ParamListInfo params, \
					QueryEnvironment *queryEnv, DestReceiver *dest, \
					QueryCompletion *qc
#define GTT_PROCESSUTILITY_ARGS pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc
#else
#if PG_VERSION_NUM >= 130000
#define GTT_PROCESSUTILITY_PROTO PlannedStmt *pstmt, const char *queryString, \
					ProcessUtilityContext context, ParamListInfo params, \
					QueryEnvironment *queryEnv, DestReceiver *dest, \
					QueryCompletion *qc
#define GTT_PROCESSUTILITY_ARGS pstmt, queryString, context, params, queryEnv, dest, qc
#else
#if PG_VERSION_NUM >= 100000
#define GTT_PROCESSUTILITY_PROTO PlannedStmt *pstmt, const char *queryString, \
					ProcessUtilityContext context, ParamListInfo params, \
					QueryEnvironment *queryEnv, DestReceiver *dest, \
					char *completionTag
#define GTT_PROCESSUTILITY_ARGS pstmt, queryString, context, params, queryEnv, dest, completionTag
#elif PG_VERSION_NUM >= 90300
#define GTT_PROCESSUTILITY_PROTO Node *parsetree, const char *queryString, \
                                        ProcessUtilityContext context, ParamListInfo params, \
					DestReceiver *dest, char *completionTag
#define GTT_PROCESSUTILITY_ARGS parsetree, queryString, context, params, dest, completionTag
#else
#define GTT_PROCESSUTILITY_PROTO Node *parsetree, const char *queryString, \
                                        ParamListInfo params, bool isTopLevel, \
					DestReceiver *dest, char *completionTag
#define GTT_PROCESSUTILITY_ARGS parsetree, queryString, params, isTopLevel, dest, completionTag
#endif
#endif
#endif

/* Saved hook values in case of unload */
static ProcessUtility_hook_type prev_ProcessUtility = NULL;
static ExecutorStart_hook_type prev_ExecutorStart = NULL;
static post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;
/* Hook to intercept CREATE GLOBAL TEMPORARY TABLE query */
static void gtt_ProcessUtility(GTT_PROCESSUTILITY_PROTO);
static void gtt_ExecutorStart(QueryDesc *queryDesc, int eflags);
#if PG_VERSION_NUM >= 140000
static void gtt_post_parse_analyze(ParseState *pstate, Query *query, struct JumbleState * jstate);
#else
static void gtt_post_parse_analyze(ParseState *pstate, Query *query);
#endif
#if PG_VERSION_NUM < 160000
Oid get_extension_schema(Oid ext_oid);
#endif

/* Enable use of Global Temporary Table at session level */
bool pgtt_is_enabled = true;

/* Regular expression search */
#define CREATE_GLOBAL_REGEXP "^\\s*CREATE\\s+(?:\\/\\*\\s*)?GLOBAL(?:\\s*\\*\\/)?"
#define CREATE_WITH_FK_REGEXP "\\s*FOREIGN\\s+KEY"

/* Oid and name of pgtt extrension schema in the database */
Oid pgtt_namespace_oid = InvalidOid;
char pgtt_namespace_name[NAMEDATALEN];

/* In memory storage of GTT and state */
typedef struct Gtt
{
        Oid           relid;
        Oid           temp_relid;
        char          relname[NAMEDATALEN];
	bool          preserved;
	bool          created;
        char          *code;
} Gtt;

typedef struct relhashent
{
        char          name[NAMEDATALEN];
	Gtt           gtt;
} GttHashEnt;

static HTAB *GttHashTable = NULL;

/* Default size of the storage area for GTT but will be dynamically extended */
#define GTT_PER_DATABASE	16

#define GttHashTableDelete(NAME) \
do { \
        GttHashEnt *hentry; \
        \
        hentry = (GttHashEnt *) hash_search(GttHashTable, NAME, HASH_REMOVE, NULL); \
        if (hentry == NULL) \
                elog(DEBUG1, "trying to delete GTT entry in HTAB that does not exist"); \
} while(0)

#define GttHashTableLookup(NAME, GTT) \
do { \
	GttHashEnt *hentry; \
	\
	hentry = (GttHashEnt *) hash_search(GttHashTable, \
							   (NAME), HASH_FIND, NULL); \
	if (hentry) \
		GTT = hentry->gtt; \
} while(0)

#define GttHashTableInsert(GTT, NAME) \
do { \
        GttHashEnt *hentry; bool found; \
        \
        hentry = (GttHashEnt *) hash_search(GttHashTable, \
								   (NAME), HASH_ENTER, &found); \
        if (found) \
                elog(ERROR, "duplicate GTT name"); \
        hentry->gtt = GTT; \
        strcpy(hentry->name, NAME); \
	elog(DEBUG1, "Insert GTT entry in HTAB, key: %s, relid: %d, temp_relid: %d, created: %d", hentry->gtt.relname, hentry->gtt.relid, hentry->gtt.temp_relid, hentry->gtt.created); \
} while(0)

/* Function declarations */

void	_PG_init(void);
void	_PG_fini(void);

int strpos(char *hay, char *needle, int offset);
static Oid gtt_create_table_statement(Gtt gtt);
static void gtt_create_table_as(Gtt gtt, bool skipdata);
static void gtt_unregister_global_temporary_table(Oid relid, const char *relname);
void GttHashTableDeleteAll(void);
void EnableGttManager(void);
Gtt GetGttByName(const char *name);
static void gtt_load_global_temporary_tables(void);
static Oid create_temporary_table_internal(Oid parent_relid, bool preserved);
static bool gtt_check_command(GTT_PROCESSUTILITY_PROTO);
static bool gtt_table_exists(QueryDesc *queryDesc);
void exitHook(int code, Datum arg);
static bool is_catalog_relid(Oid relid);
static void force_pgtt_namespace (void);
static void gtt_update_registered_table(Gtt gtt);
int strremovestr(char *src, char *toremove);
static void gtt_unregister_gtt_not_cached(const char *relname);

/*
 * Module load callback
 */
void
_PG_init(void)
{
	static bool inited = false;

	elog(DEBUG1, "_PG_init() : %d", inited);

	/*
	 * Be sure we do initialization only once.
	 *
	 * If initialization fails due to, e.g., ERROR:  extension "pgtt" does
	 * not exist, then we'll return here on the next usage.
	 */
	if (inited)
		return;

	if (ParallelWorkerNumber >= 0)
		return;
	/*
	 * If we are loaded via shared_preload_libraries exit.
	 */
	if (process_shared_preload_libraries_in_progress)
	{
		ereport(FATAL,
				(errmsg("The pgtt extension can not be loaded using shared_preload_libraries."),
				 errhint("Use \"LOAD 'pgtt';\" in the running session instead.")));
	}

	if (!IsTransactionState())
	{
		ereport(FATAL,
				(errmsg("The pgtt extension can not be loaded using session_preload_libraries."),
				 errhint("Use \"LOAD 'pgtt';\" in the running session instead.")));
	}

	if (GttHashTable == NULL)
	{
		/* Initialize list of Global Temporary Table */
		EnableGttManager();

		/*
		 * Load temporary table definition from pg_global_temp_tables table
		 * into our Hash table and pre-create the temporary tables.
		 */
		gtt_load_global_temporary_tables();
	}

	/*
	 * Be sure that extension schema is at end of the search path so that
	 * "template" tables will be found.
	 */
	force_pgtt_namespace();

	/*
 	 * Define (or redefine) custom GUC variables.
	 * No custom GUC variable at this time
	 */
	DefineCustomBoolVariable("pgtt.enabled",
							"Enable use of Global Temporary Table",
							"By default the extension is automatically enabled after load, "
							"it can be temporary disable by setting the GUC value to false "
							"then enable again later wnen necessary.",
							&pgtt_is_enabled,
							true,
							PGC_USERSET,
							0,
							NULL,
							NULL,
							NULL);

	inited = true;

	/*
	 * Install hooks.
	 */
	prev_ExecutorStart = ExecutorStart_hook;
	ExecutorStart_hook = gtt_ExecutorStart;
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = gtt_post_parse_analyze;

	prev_ProcessUtility = ProcessUtility_hook;
	ProcessUtility_hook = gtt_ProcessUtility;

	/* set the exit hook */
	on_proc_exit(&exitHook, PointerGetDatum(NULL));
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	elog(DEBUG1, "_PG_fini()");

	/* Uninstall hooks. */
	ExecutorStart_hook = prev_ExecutorStart;
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
	ProcessUtility_hook = prev_ProcessUtility;
}

/*
 * Exit hook.
 */
void
exitHook(int code, Datum arg)
{
        elog(DEBUG1, "exiting with %d", code);
}

static void
gtt_ProcessUtility(GTT_PROCESSUTILITY_PROTO)
{
	elog(DEBUG1, "gtt_ProcessUtility()");

	/* Do not waste time here if the feature is not enabled for this session */
	if (pgtt_is_enabled && NOT_IN_PARALLEL_WORKER)
	{
		/*
		 * Be sure that extension schema is at end of the search path so that
		 * "template" tables will be find.
		 */
		if (IsTransactionState())
			force_pgtt_namespace();

		/*
		 * Check if we have a CREATE GLOBAL TEMPORARY TABLE
		 * in this case do more work than the simple table
		 * creation see SQL file in sql/ subdirectory.
		 *
		 * If the current query use a GTT that is not already
		 * created create it.
		 */
		if (gtt_check_command(GTT_PROCESSUTILITY_ARGS))
		{
			elog(DEBUG1, "Work on GTT from Utility Hook done, get out of UtilityHook immediately.");
			return;
		}
	}

	elog(DEBUG1, "restore ProcessUtility");

	/* Excecute the utility command, we are not concerned */
	PG_TRY();
	{
		if (prev_ProcessUtility)
			prev_ProcessUtility(GTT_PROCESSUTILITY_ARGS);
		else
			standard_ProcessUtility(GTT_PROCESSUTILITY_ARGS);
	}
	PG_CATCH();
	{
		PG_RE_THROW();
	}
	PG_END_TRY();

	elog(DEBUG1, "End of gtt_ProcessUtility()");
}

/*
 * Look at utility command to search CREATE TABLE / DROP TABLE
 * and INSERT INTO statements to see if a Global Temporary Table
 * is concerned.
 * Return true if all work is done and the origin statement must
 * be forgotten. False mean that the statement must be processed
 * normally.
 */
static bool
gtt_check_command(GTT_PROCESSUTILITY_PROTO)
{
	bool	preserved = true;
	bool    work_completed = false;
	char	*name = NULL;
#if PG_VERSION_NUM >= 100000
	Node    *parsetree = pstmt->utilityStmt;
#endif

	Assert(parsetree != NULL);
	Assert(queryString != NULL);

	elog(DEBUG1, "gtt_check_command() on query: \"%s\"", queryString);

	/* Intercept CREATE / DROP TABLE statements */
	switch (nodeTag(parsetree))
	{
		case T_VariableSetStmt:
		{
			VariableSetStmt *stmt = (VariableSetStmt *) parsetree;

			/*
			 * Forcing search_path is not enough because it does not
			 * handle SET search_path TO ... statement. This code also
			 * add the PGTT schema if not present in the path
			 */
			if (strcmp(stmt->name, "search_path") == 0)
			{
				if (stmt->kind == VAR_SET_VALUE)
				{
					ListCell *l;
					bool     found = false;

					if (stmt->args == NIL)
						break;

					foreach(l, stmt->args)
					{
						Node    *arg = (Node *) lfirst(l);
						A_Const *con = (A_Const *) arg;
						char    *val;

						val = strVal(&con->val);
						if (strcmp(val,
							get_namespace_name(pgtt_namespace_oid)) == 0)
							found = true;
					}
					/* append the extension schema to the arg list. */
					if (!found)
					{
						A_Const *newcon = makeNode(A_Const);
						char *str = (char *) get_namespace_name(pgtt_namespace_oid);

#if PG_VERSION_NUM < 150000
						newcon->val.type = T_String;
						newcon->val.val.str = pstrdup(str);
#else
						newcon->val.node.type = T_String;
						newcon->val.sval.sval = pstrdup(str);
#endif
						//newcon->type = T_A_Const;
						newcon->location = strlen(queryString);
						stmt->args = lappend(stmt->args, newcon);
					}
				}
			}
		}
		break;

		case T_CreateTableAsStmt:
		{
			Gtt gtt;
			int i;
			CreateTableAsStmt *stmt = (CreateTableAsStmt *)parsetree;
			bool skipdata = stmt->into->skipData;
			bool regexec_result;

			/* Get the name of the relation */
			name = stmt->into->rel->relname;

			/*
			 * CREATE TABLE AS is similar as SELECT INTO,
			 * so avoid going further in this last case.
			 */
			if (stmt->is_select_into)
				break;

			/* do not proceed OBJECT_MATVIEW */
			if (STMT_OBJTYPE(stmt) != OBJECT_TABLE)
				break;

			/*
			 * Be sure to have CREATE TEMPORARY TABLE definition
			 */
			if (stmt->into->rel->relpersistence != RELPERSISTENCE_TEMP)
				break;

			/* 
			 * We only take care here of statements with the GLOBAL keyword
			 * even if it is deprecated and generate a warning.
			 */
			regexec_result = RE_compile_and_execute(
					cstring_to_text(CREATE_GLOBAL_REGEXP),
					VARDATA_ANY(cstring_to_text((char *) queryString)),
					VARSIZE_ANY_EXHDR(cstring_to_text((char *) queryString)),
					REG_ADVANCED | REG_ICASE | REG_NEWLINE,
					DEFAULT_COLLATION_OID,
					0, NULL);

			
			if (!regexec_result)
				break;

			/*
			 * What to do at commit time for global temporary relations
			 * default is ON COMMIT PRESERVE ROWS (do nothing)
			 */
			if (stmt->into->onCommit == ONCOMMIT_DELETE_ROWS)
				preserved = false;

			/* 
			 * Case of ON COMMIT DROP and GLOBAL TEMPORARY might not be
			 * allowed, this is the same as using a normal temporary table
			 * inside a transaction. Here the table should be dropped after
			 * commit so it will not survive a transaction.
			 * Throw an error to prevent the use of this clause.
			 */
			if (stmt->into->onCommit == ONCOMMIT_DROP)
				ereport(ERROR,
						(errmsg("use of ON COMMIT DROP with GLOBAL TEMPORARY is not allowed"),
						 errhint("Create a local temporary table inside a transaction instead, this is the default behavior.")));

			elog(DEBUG1, "Create table %s, rows persistance: %d, GLOBAL at position: %d",
						name, preserved,
						strpos(asc_toupper(queryString, strlen(queryString)), "GLOBAL", 0));

			/* Force creation of the temporary table in our pgtt schema */
			stmt->into->rel->schemaname = pstrdup(pgtt_namespace_name);
			/* replace temporary state from the table to unlogged table */
			stmt->into->rel->relpersistence = RELPERSISTENCE_UNLOGGED;
			/* Do not copy data in the unlogged table */
			stmt->into->skipData = true;

			/*
			 * At this stage the unlogged table will be created with normal
			 * utility hook. What we need now is to register the table in
			 * the pgtt catalog table and create a normal temporary table
			 * using the original statement without the GLOBAL keyword
			 */
			gtt.relid = 0;
			gtt.temp_relid = 0;
			strcpy(gtt.relname, name);
			gtt.relname[strlen(name)] = 0;
			gtt.preserved = preserved;
			gtt.created = false;
			/* Extract the AS ... code part from the query */
			gtt.code = pstrdup(queryString);
			for (i = 30; i < strlen(queryString) - 1; i++)
			{
				if (    isspace(queryString[i])
					&& (queryString[i+1] == 'A' || queryString[i+1] == 'a')
					&& (queryString[i+2] == 'S' || queryString[i+2] == 's')
					&& (isspace(queryString[i+3]) || queryString[i+3] == '(') )
					break;
			} 
			if (i == strlen(queryString) - 1)
				elog(ERROR, "can not find AS keyword in this CREATE TABLE AS statement.");

			gtt.code += i;

			if (gtt.code[strlen(gtt.code) - 1] == ';')
				gtt.code[strlen(gtt.code) - 1] = 0;

			/* remove WITH DATA from the code */
			strremovestr(gtt.code, "WITH DATA");

			/* Create the necessary object to emulate the GTT */
			gtt_create_table_as(gtt, skipdata);

			work_completed = true;

			break;
		}

		case T_CreateStmt:
		{
			/* CREATE TABLE statement */
			CreateStmt *stmt = (CreateStmt *)parsetree;
			Gtt        gtt;
			int        len, i, start = 0, end = 0;
			bool regexec_result;

			/* Get the name of the relation */
			name = stmt->relation->relname;

			/*
			 * Be sure to have CREATE TEMPORARY TABLE definition
			 */
			if (stmt->relation->relpersistence != RELPERSISTENCE_TEMP)
				break;

			/* 
			 * We only take care here of statements with the GLOBAL keyword
			 * even if it is deprecated and generate a warning.
			 */
			regexec_result = RE_compile_and_execute(
					cstring_to_text(CREATE_GLOBAL_REGEXP),
					VARDATA_ANY(cstring_to_text((char *) queryString)),
					VARSIZE_ANY_EXHDR(cstring_to_text((char *) queryString)),
					REG_ADVANCED | REG_ICASE | REG_NEWLINE,
					DEFAULT_COLLATION_OID,
					0, NULL);

                        if (!regexec_result)
                                break;

			/* Check if there is foreign key defined in the statement */
			regexec_result = RE_compile_and_execute(
					cstring_to_text(CREATE_WITH_FK_REGEXP),
					VARDATA_ANY(cstring_to_text((char *) queryString)),
					VARSIZE_ANY_EXHDR(cstring_to_text((char *) queryString)),
					REG_ADVANCED | REG_ICASE | REG_NEWLINE,
					DEFAULT_COLLATION_OID,
					0, NULL);
                        if (regexec_result)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("attempt to create referential integrity constraint on global temporary table")));


#if (PG_VERSION_NUM >= 100000)
			/*
			 * We do not allow partitioning on GTT, not that PostgreSQL can
			 * not do it but because we want to mimic the Oracle or other
			 * RDBMS behavior.
			 */
			if (stmt->partspec != NULL)
				elog(ERROR, "Global Temporary Table do not support partitioning.");
#endif

			/*
			 * What to do at commit time for global temporary relations
			 * default is ON COMMIT PRESERVE ROWS (do nothing)
			 */
			if (stmt->oncommit == ONCOMMIT_DELETE_ROWS)
				preserved = false;

			/* 
			 * Case of ON COMMIT DROP and GLOBAL TEMPORARY might not be
			 * allowed, this is the same as using a normal temporary table
			 * inside a transaction. Here the table should be dropped after
			 * commit so it will not survive a transaction.
			 * Throw an error to prevent the use of this clause.
			 */
			if (stmt->oncommit == ONCOMMIT_DROP)
				ereport(ERROR,
						(errmsg("use of ON COMMIT DROP with GLOBAL TEMPORARY is not allowed"),
						 errhint("Create a local temporary table inside a transaction instead, this is the default behavior.")));

			elog(DEBUG1, "Create table %s, rows persistance: %d, GLOBAL at position: %d",
						name, preserved,
						strpos(asc_toupper(queryString, strlen(queryString)), "GLOBAL", 0));

			/* Create the Global Temporary Table template and register the table */
			gtt.relid = 0;
			gtt.temp_relid = 0;
			strcpy(gtt.relname, name);
			gtt.relname[strlen(name)] = 0;
			gtt.preserved = preserved;
			gtt.created = false;
			gtt.code = NULL;

			/* Extract the definition of the table */
			for (i = 0; i < strlen(queryString); i++)
			{
				if (queryString[i] == '(')
				{
					start = i;
					break;
				}
			}
			start++;
			for (i = start; i < strlen(queryString); i++)
			{
				if (queryString[i] == ')')
				{
					end = i;
				}
			}

			len = end - start;
			if (end > 0 && start > 0)
			{
				gtt.code = palloc0(sizeof(char *) * (len + 1));
				strncpy(gtt.code, queryString+start, len);
				gtt.code[len] = '\0'; 
			}

			elog(DEBUG1, "code for Global Temporary Table \"%s\" creation is \"%s\"", gtt.relname, gtt.code);

			/* Create the necessary object to emulate the GTT */
			gtt.relid = gtt_create_table_statement(gtt);

			/*
			 * In case of problem during GTT creation previous function
			 * call throw an error so the code that's follow is safe.
			 * Update GTT cache with table flagged as created
			 */
			gtt.created = false;
			GttHashTableDelete(gtt.relname);
			GttHashTableInsert(gtt, gtt.relname);
			work_completed = true;

			elog(DEBUG1, "Global Temporary Table \"%s\" created", gtt.relname);
			break;
		}

		case T_DropStmt:
		{
			DropStmt *drop = (DropStmt *) parsetree;
			if (drop->removeType == OBJECT_TABLE)
			{
				List *relationNameList = NULL;
				int relationNameListLength = 0;
#if PG_VERSION_NUM < 150000
				Value *relationSchemaNameValue = NULL;
				Value *relationNameValue = NULL;
#else
				String *relationSchemaNameValue = NULL;
				String *relationNameValue = NULL;
#endif
				Gtt gtt;

				relationNameList = list_copy((List *) linitial(drop->objects));
				relationNameListLength = list_length(relationNameList);

				switch (relationNameListLength)
				{
					case 1:
					{
						relationNameValue = linitial(relationNameList);
						break;
					}
					case 2:
					{
						relationSchemaNameValue = linitial(relationNameList);
						relationNameValue = lsecond(relationNameList);
						break;
					}
					case 3:
					{
						relationSchemaNameValue = lsecond(relationNameList);
						relationNameValue = lthird(relationNameList);
						break;
					}
					default:
					{
						ereport(ERROR, (errcode(ERRCODE_SYNTAX_ERROR),
										errmsg("improper relation name: \"%s\"",
											   NameListToString(relationNameList))));
						break;
					}
				}

				/* prefix with schema name if it is not added already */
				if (relationSchemaNameValue == NULL)
				{
#if PG_VERSION_NUM < 150000
					Value *schemaNameValue = makeString(pgtt_namespace_name);
#else
					String *schemaNameValue = makeString(pgtt_namespace_name);
#endif
					relationNameList = lcons(schemaNameValue, relationNameList);
				}

				/*
				 * Check if the table is in the hash list, drop
				 * it if it has already been be created and remove
				 * the cache entry.
				 */
#if PG_VERSION_NUM < 150000
				if (PointerIsValid(relationNameValue->val.str))
#else
				if (PointerIsValid(relationNameValue->sval))
#endif
				{
#if PG_VERSION_NUM < 150000
					elog(DEBUG1, "looking for dropping table: %s", relationNameValue->val.str);
#else
					elog(DEBUG1, "looking for dropping table: %s", relationNameValue->sval);
#endif
					/* Initialize Gtt object */
					gtt.relid = 0;
					gtt.temp_relid = 0;
					gtt.relname[0] = '\0';
					gtt.preserved = false;
					gtt.code = NULL;
					gtt.created = false;

#if PG_VERSION_NUM < 150000
					elog(DEBUG1, "looking if table %s is a cached GTT", relationNameValue->val.str);
					GttHashTableLookup(relationNameValue->val.str, gtt);
#else
					elog(DEBUG1, "looking if table %s is a cached GTT", relationNameValue->sval);
					GttHashTableLookup(relationNameValue->sval, gtt);
#endif
					if (gtt.relname[0] != '\0')
					{
						/*
						 * When the temporary table have been created
						 * we can not remove the GTT in the same session.
						 * Creating and dropping GTT can only be performed
						 * by a superuser in a "maintenance" session.
						 */
						if (gtt.created)
							elog(ERROR, "can not drop a GTT that is in use.");
						/*
						 * Unregister the Global Temporary Table and its link to the
						 * view stored in pg_global_temp_tables table
						 */
						gtt_unregister_global_temporary_table(gtt.relid, gtt.relname);

						/* Remove the table from the hash table */
						GttHashTableDelete(gtt.relname);
					}
					else
					{
						/*
						 * Table is not on current session cache but remove
						 * it from PGTT list if it exists.
						 */
#if PG_VERSION_NUM < 150000
						elog(DEBUG1, "looking if table %s is registered as GTT", relationNameValue->val.str);
						gtt_unregister_gtt_not_cached(relationNameValue->val.str);
#else
						elog(DEBUG1, "looking if table %s is registered as GTT", relationNameValue->sval);
						gtt_unregister_gtt_not_cached(relationNameValue->sval);
#endif
					}
				}
			}
                        break;
                }

		case T_RenameStmt:
		{
			/* CREATE TABLE statement */
			RenameStmt *stmt = (RenameStmt *)parsetree;
			Gtt        gtt;

			/* We only take care of tabe renaming to update our internal storage */
			if (stmt->renameType != OBJECT_TABLE || stmt->newname == NULL)
				break;

			gtt.relid = 0;
			/* Look if the table is declared as GTT */
			GttHashTableLookup(stmt->relation->relname, gtt);

			/* Not registered as a GTT, nothing to do here */
			if (gtt.relid == 0)
				break;

			/* If a temporary table have already created do not allow changing name */
			if (gtt.created)
				elog(ERROR, "a temporary table has been created and is active, can not rename the GTT table in this session.");

			/* Rename the table and get the resulting new Oid */
			RenameRelation(stmt);

			elog(DEBUG1, "updating registered table in %s.pg_global_temp_tables.", pgtt_namespace_name);
			strcpy(gtt.relname, stmt->newname);
			gtt_update_registered_table(gtt);

			/* Delete and recreate the table in cache */
			GttHashTableDelete(stmt->relation->relname);
			GttHashTableInsert(gtt, stmt->newname);
			work_completed = true;

			break;
		}

		case T_CommentStmt:
		{
			/* COMMENT ON TABLE/COLUMN statement */
			CommentStmt   *stmt = (CommentStmt *)parsetree;
			Relation      relation;
			char          *nspname;

			/* We only take care of comment on table or column to update our internal storage */
			if (stmt->objtype != OBJECT_TABLE && stmt->objtype != OBJECT_COLUMN)
				break;

			/*
			 * Get the relation object by calling get_object_address().
			 * get_object_address() will throw an error if the object
			 * does not exist, and will also acquire a lock on the target
			 * to guard against concurrent DROP operations.
			 */
#if (PG_VERSION_NUM < 100000)
			(void) get_object_address(stmt->objtype, stmt->objname, stmt->objargs,
										 &relation, ShareUpdateExclusiveLock, false);
#else
			(void) get_object_address(stmt->objtype, stmt->object,
										&relation, ShareUpdateExclusiveLock, false);
#endif

			/* Just take care that the GTT is not in use */
			nspname = get_namespace_name(RelationGetNamespace(relation));
			relation_close(relation, NoLock);
			if (strcmp(nspname, pgtt_namespace_name) != 0)
			{
				if (strstr(nspname, "pg_temp") != NULL)
					elog(ERROR, "a temporary table has been created and is active, can not add a comment on the GTT table in this session.");
			}

			break;
		}

		case T_AlterTableStmt:
		{
			/* Look for contrainst statement */
			AlterTableStmt   *stmt = (AlterTableStmt *)parsetree;
			ListCell   *lcmd;
			Gtt        gtt;

			if (STMT_OBJTYPE(stmt) != OBJECT_TABLE)
				break;

			/* Look if the table is declared as GTT */
			gtt.relid = 0;
			GttHashTableLookup(stmt->relation->relname, gtt);

			/* Not registered as a GTT, nothing to do here */
			if (gtt.relid == 0)
				break;

			/* We do not allow foreign keys on global temporary table */
			foreach(lcmd, stmt->cmds)
			{
				AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);

				if (cmd->subtype == AT_AddConstraint
#if (PG_VERSION_NUM < 130000)
						|| cmd->subtype == AT_ProcessedConstraint
#endif
				   )
				{
					Constraint *constr = (Constraint *) cmd->def;
					if (constr->contype == CONSTR_FOREIGN)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
								 errmsg("attempt to create referential integrity constraint on global temporary table")));
				}
			}

			break;
		}

		case T_IndexStmt:
		{
			/* CREATE INDEX statement */
			IndexStmt  *stmt = (IndexStmt *) parsetree;
			Oid        relid;
			char       *nspname;

			relid = RangeVarGetRelidExtended(stmt->relation, ShareLock,
#if (PG_VERSION_NUM >= 110000)
				       					0,
#else
									false, false,
#endif
									RangeVarCallbackOwnsRelation,
									NULL);

			/* Just take care that the GTT is not in use */
			nspname = get_namespace_name(get_rel_namespace(relid));
			if (strcmp(nspname, pgtt_namespace_name) != 0)
			{
				if (strstr(nspname, "pg_temp") != NULL)
					elog(ERROR, "a temporary table has been created and is active, can not add an index on the GTT table in this session.");
			}

			break;
		}


		default:
			break;
	}
	return work_completed;
}

static void
gtt_ExecutorStart(QueryDesc *queryDesc, int eflags)
{
	elog(DEBUG1, "gtt_ExecutorStart()");

	/* Do not waste time here if the feature is not enabled for this session */
	if (pgtt_is_enabled && NOT_IN_PARALLEL_WORKER)
	{

		/* check if we are working on a GTT and create it if it doesn't exist */
		if (queryDesc->operation == CMD_INSERT
				|| queryDesc->operation == CMD_DELETE
				|| queryDesc->operation == CMD_UPDATE
				|| queryDesc->operation == CMD_SELECT)
		{
			/* Verify if a GTT table is defined, create it if this is not already the case */
			if (gtt_table_exists(queryDesc))
				elog(DEBUG1, "ExecutorStart() statement use a Global Temporary Table");
		}
	}

	elog(DEBUG1, "restore ExecutorStart()");

	/* Continue the normal behavior */
	if (prev_ExecutorStart)
		prev_ExecutorStart(queryDesc, eflags);
	else
		standard_ExecutorStart(queryDesc, eflags);

	elog(DEBUG1, "End of gtt_ExecutorStart()");
}

static bool
gtt_table_exists(QueryDesc *queryDesc)
{
	bool    is_gtt = false;
	char    *name = NULL;
	char    relpersistence;
	RangeTblEntry *rte;
	Relation      rel;
	Gtt           gtt;
	PlannedStmt *pstmt = (PlannedStmt *) queryDesc->plannedstmt;

	if (!pstmt)
		return false;

	/* no relation in rtable probably a function call */
	if (list_length(pstmt->rtable) == 0)
		return false;

	/* This must be a valid relation and not a temporary table */
	rte = (RangeTblEntry *) linitial(pstmt->rtable);
	if (rte->relid != InvalidOid && rte->relkind == RELKIND_RELATION
				&& !is_catalog_relid(rte->relid))
	{
#if (PG_VERSION_NUM >= 120000)
		rel = table_open(rte->relid, NoLock);
#else
		rel = heap_open(rte->relid, NoLock);
#endif
		name = RelationGetRelationName(rel);
		relpersistence = rel->rd_rel->relpersistence;
#if (PG_VERSION_NUM >= 120000)
		table_close(rel, NoLock);
#else
		heap_close(rel, NoLock);
#endif

		/* Do not go further with temporary tables, catalog or toast table */
		if (relpersistence != RELPERSISTENCE_TEMP)
			return false;

		gtt.relid = 0;
		gtt.temp_relid = 0;
		gtt.relname[0] = '\0';
		gtt.preserved = false;
		gtt.code = NULL;
		gtt.created = false;

		/* Check if the table is in the hash list and it has not already be created */
		if (PointerIsValid(name))
			GttHashTableLookup(name, gtt);

		elog(DEBUG1, "gtt_table_exists() looking for table \"%s\" with relid %d into cache.", name, rte->relid);
		if (gtt.relname[0] != '\0')
		{
			elog(DEBUG1, "GTT found in cache with name: %s, relid: %d, temp_relid %d", gtt.relname, gtt.relid, gtt.temp_relid);
			/* Create the temporary table if it does not exists */
			if (!gtt.created)
			{
				elog(DEBUG1, "global temporary table does not exists create it: %s", gtt.relname);
				/* Call create temporary table */
				if ((gtt.temp_relid = create_temporary_table_internal(gtt.relid, gtt.preserved)) != InvalidOid)
				{
					elog(DEBUG1, "global temporary table %s (oid: %d) created", gtt.relname, gtt.temp_relid);
					/* Update hash list with table flagged as created */
					gtt.created = true;
					GttHashTableDelete(gtt.relname);
					GttHashTableInsert(gtt, gtt.relname);
				}
				else
					elog(ERROR, "can not create global temporary table %s", gtt.relname);
			}
			is_gtt = true;
		}
		else
			/* the table is not a global temporary table do nothing*/
			elog(DEBUG1, "table \"%s\" not registered as GTT", name);
	}

	return is_gtt;
}

int
strpos(char *hay, char *needle, int offset)
{
	char *haystack;
	char *p;

	haystack = (char *) malloc(strlen(hay));
	if (haystack == NULL)
	{
		fprintf(stderr, _("out of memory\n"));
		exit(EXIT_FAILURE);
		return -1;
	}
	memset(haystack, 0, strlen(hay));

	strncpy(haystack, hay+offset, strlen(hay)-offset);
	p = strstr(haystack, needle);
	if (p)
		return p - haystack+offset;

	return -1;
}

/*
 * Create the Global Temporary Table with all associated objects
 * by creating the template table and register the GTT in the
 * pg_global_temp_tables table.
 *
 */
static Oid
gtt_create_table_statement(Gtt gtt)
{
	char    *newQueryString = NULL;
	int     connected = 0;
	int     finished = 0;
	int     result = 0;
	Oid     gttOid = InvalidOid;
	Datum   oidDatum;
	bool    isnull;

	elog(DEBUG1, "proceeding to Global Temporary Table creation.");

	connected = SPI_connect();
	if (connected != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("could not connect to SPI manager")));

	/* Create the "template" table */
	newQueryString = psprintf("CREATE UNLOGGED TABLE %s.%s (%s)",
			quote_identifier(pgtt_namespace_name),
			quote_identifier(gtt.relname),
			gtt.code);
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

	/* Get Oid of the newly created table */
	newQueryString = psprintf("SELECT c.relfilenode FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE c.relname='%s' AND n.nspname = '%s'",
			gtt.relname,
			pgtt_namespace_name);

        result = SPI_exec(newQueryString, 0);
        if (result != SPI_OK_SELECT && SPI_processed != 1)
                ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

	oidDatum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &isnull);

	if (!isnull)
		gttOid = DatumGetInt32(oidDatum);
        
	if (isnull || !OidIsValid(gttOid))
                ereport(ERROR,
				(errmsg("can not get OID of newly created GTT template table %s",
							quote_identifier(gtt.relname))));

	/* Now register the GTT table */
	newQueryString = psprintf("INSERT INTO %s.pg_global_temp_tables VALUES (%d, '%s', '%s', '%c', %s)",
			quote_identifier(pgtt_namespace_name),
			gttOid,
			pgtt_namespace_name,
			gtt.relname,
			(gtt.preserved) ? 't' : 'f',
			quote_literal_cstr(gtt.code)
		);
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("can not registrer new global temporary table")));

	/* Set privilege on the unlogged table */
	newQueryString = psprintf("GRANT ALL ON TABLE %s.%s TO public",
			quote_identifier(pgtt_namespace_name),
			quote_identifier(gtt.relname));
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));


	/* Mark the GTT as been created before register the table in the cache */
	gtt.created = true;

	finished = SPI_finish();
	if (finished != SPI_OK_FINISH)
		ereport(ERROR, (errmsg("could not disconnect from SPI manager")));

	return gttOid;
}

/*
 * Unregister a Global Temporary Table in pg_global_temp_tables table
 * using his relid.
 */
static void
gtt_unregister_global_temporary_table(Oid relid, const char *relname)
{
	RangeVar     *rv;
	Relation      rel;
	ScanKeyData   key[1];
	SysScanDesc   scan;
	HeapTuple     tuple;

	elog(DEBUG1, "Looking for registered GTT relid = %d, relname = %s", relid, relname);

	/* Set and open the GTT relation */
	rv = makeRangeVar(pgtt_namespace_name, CATALOG_GLOBAL_TEMP_REL, -1);
#if (PG_VERSION_NUM >= 120000)
	rel = table_openrv(rv, RowExclusiveLock);
#else
	rel = heap_openrv(rv, RowExclusiveLock);
#endif
	/* Define scanning */
	ScanKeyInit(&key[0], Anum_pgtt_relid, BTEqualStrategyNumber, F_OIDEQ, ObjectIdGetDatum(relid));

	/* Start search of relation */
	scan = systable_beginscan(rel, 0, true, NULL, 1, key);
	/* Remove the tuples. */
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		elog(DEBUG1, "removing tuple with relid = %d and relname = %s", relid, relname);
		simple_heap_delete(rel, &tuple->t_self);
	}
	/* Cleanup. */
	systable_endscan(scan);
#if (PG_VERSION_NUM >= 120000)
	table_close(rel, RowExclusiveLock);
#else
	heap_close(rel, RowExclusiveLock);
#endif
}

/*
 * Unregister a Global Temporary Table in pg_global_temp_tables table
 * that is not cached and using his name only.
 */
static void
gtt_unregister_gtt_not_cached(const char *relname)
{
	RangeVar     *rv;
	Relation      rel;
	ScanKeyData   key[1];
	SysScanDesc   scan;
	HeapTuple     tuple;

	elog(DEBUG1, "Looking for registered GTT relname = %s", relname);

	/* Set and open the GTT relation */
	rv = makeRangeVar(pgtt_namespace_name, CATALOG_GLOBAL_TEMP_REL, -1);
#if (PG_VERSION_NUM >= 120000)
	rel = table_openrv(rv, RowExclusiveLock);
#else
	rel = heap_openrv(rv, RowExclusiveLock);
#endif
	/* Define scanning */
	ScanKeyInit(&key[0], Anum_pgtt_relname, BTEqualStrategyNumber, F_NAMEEQ, CStringGetDatum(relname));

	/* Start search of relation */
	scan = systable_beginscan(rel, 0, true, NULL, 1, key);
	/* Remove the tuples. */
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		elog(DEBUG1, "removing tuple with relname = %s", relname);
		simple_heap_delete(rel, &tuple->t_self);
	}

	/* Cleanup. */
	systable_endscan(scan);
#if (PG_VERSION_NUM >= 120000)
	table_close(rel, RowExclusiveLock);
#else
	heap_close(rel, RowExclusiveLock);
#endif
}

#if PG_VERSION_NUM < 160000
/*
 * From src/backend/commands/extension.c
 */
Oid
get_extension_schema(Oid ext_oid)
{
	Oid                     result;
	Relation        rel;
	SysScanDesc scandesc;
	HeapTuple       tuple;
	ScanKeyData entry[1];

#if (PG_VERSION_NUM >= 120000)
	rel = table_open(ExtensionRelationId, AccessShareLock);
#else
	rel = heap_open(ExtensionRelationId, AccessShareLock);
#endif

	ScanKeyInit(&entry[0],
#if (PG_VERSION_NUM >= 120000)
				Anum_pg_extension_oid,
#else
				ObjectIdAttributeNumber,
#endif
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(ext_oid));

	scandesc = systable_beginscan(rel, ExtensionOidIndexId, true,
								  NULL, 1, entry);

	tuple = systable_getnext(scandesc);

	/* We assume that there can be at most one matching tuple */
	if (HeapTupleIsValid(tuple))
		result = ((Form_pg_extension) GETSTRUCT(tuple))->extnamespace;
	else
		result = InvalidOid;

	systable_endscan(scandesc);

#if (PG_VERSION_NUM >= 120000)
	table_close(rel, AccessShareLock);
#else
	heap_close(rel, AccessShareLock);
#endif

	return result;
}
#endif


/*
 * EnableGttManager
 *              Enables the GTT management cache at backend startup.
 */
void
EnableGttManager(void)
{
	Oid extOid = get_extension_oid("pgtt", false);

	if (GttHashTable == NULL)
	{
		HASHCTL         ctl;

		MemSet(&ctl, 0, sizeof(ctl));
		ctl.keysize = NAMEDATALEN;
		ctl.entrysize = sizeof(GttHashEnt);

		/* allocate GTT Cache in the cache context */
		ctl.hcxt = CacheMemoryContext;
		GttHashTable = hash_create("Global Temporary Table hash list",
									GTT_PER_DATABASE,
									&ctl,
#if PG_VERSION_NUM >= 140000
									HASH_STRINGS | HASH_ELEM | HASH_CONTEXT
#else
									HASH_ELEM | HASH_CONTEXT
#endif
						);
		elog(DEBUG1, "GTT cache initialized.");
	}

	/* Set the OID and name of the extension schema, all objects will be created in this schema */
	pgtt_namespace_oid = get_extension_schema(extOid);
	if (!OidIsValid(pgtt_namespace_oid))
		elog(ERROR, "namespace %d can not be found.", pgtt_namespace_oid); 
	strcpy(pgtt_namespace_name, get_namespace_name(pgtt_namespace_oid));
}

/*
 * Delete all declared Global Temporary Table.
 *
 */
void
GttHashTableDeleteAll(void)
{
        HASH_SEQ_STATUS status;
        GttHashEnt *lentry = NULL;

        if (GttHashTable == NULL)
                return;

        hash_seq_init(&status, GttHashTable);
        while ((lentry = (GttHashEnt *) hash_seq_search(&status)) != NULL)
        {
		Gtt          gtt = GetGttByName(lentry->name);

		elog(DEBUG1, "Remove GTT %s from our hash table", gtt.relname);
		GttHashTableDelete(lentry->name);
		/* Restart the iteration in case that led to other drops */
		hash_seq_term(&status);
		hash_seq_init(&status, GttHashTable);
        }
}

/*
 * GetGttByName
 *       Returns a Gtt given a table name, or NULL if name is not found.
 */
Gtt
GetGttByName(const char *name)
{
        Gtt          gtt;

        if (PointerIsValid(name))
                GttHashTableLookup(name, gtt);

        return gtt;
}

/*
 * Load Global Temporary Table in memory from pg_global_temp_tables table.
 */
static void
gtt_load_global_temporary_tables(void)
{
	RangeVar       *rv;
	Relation       rel;
#if (PG_VERSION_NUM >= 120000)
	TableScanDesc  scan;
#else
	HeapScanDesc   scan;
#endif
	HeapTuple     tuple;
	int           numberOfAttributes;
	TupleDesc     tupleDesc;
	Snapshot      snapshot;

	elog(DEBUG1, "gtt_load_global_temporary_tables()");

	elog(DEBUG1, "retrieve GTT list from definition table %s.%s", pgtt_namespace_name, CATALOG_GLOBAL_TEMP_REL);

	/* Set and open the GTT definition storage relation */
	rv = makeRangeVar(pgtt_namespace_name, CATALOG_GLOBAL_TEMP_REL, -1);
	/* Open the CATALOG_GLOBAL_TEMP_REL table. We don't want to allow
	 * writable accesses by other session during import. */
	snapshot = GetActiveSnapshot();
	//snapshot = GetTransactionSnapshot();
#if (PG_VERSION_NUM >= 120000)
	rel = table_openrv(rv, AccessShareLock);
        scan = table_beginscan(rel, snapshot, 0, (ScanKey) NULL);
#else
	rel = heap_openrv(rv, AccessShareLock);
        scan = heap_beginscan(rel, snapshot, 0, (ScanKey) NULL);
#endif
	tupleDesc = RelationGetDescr(rel);
	numberOfAttributes = tupleDesc->natts;
        while (HeapTupleIsValid(tuple = heap_getnext(scan, ForwardScanDirection)))
        {
		Gtt gtt;
		Datum *values = (Datum *) palloc(numberOfAttributes * sizeof(Datum));
		bool *isnull = (bool *) palloc(numberOfAttributes * sizeof(bool));

		/* Extract data */
		heap_deform_tuple(tuple, tupleDesc, values, isnull);
		gtt.relid = DatumGetInt32(values[0]);
		strcpy(gtt.relname, NameStr(*(DatumGetName(values[2]))));
		gtt.preserved = DatumGetBool(values[3]);
		gtt.code = TextDatumGetCString(values[4]);
		gtt.created = false;
		gtt.temp_relid = 0;
		/* Add table to cache */
		GttHashTableInsert(gtt, gtt.relname);
	}

	/* Cleanup. */
#if (PG_VERSION_NUM >= 120000)
	table_endscan(scan);
	table_close(rel, AccessShareLock);
#else
	heap_endscan(scan);
	heap_close(rel, AccessShareLock);
#endif
}

static Oid
create_temporary_table_internal(Oid parent_relid, bool preserved)
{
	/* Value to be returned */
        Oid                         temp_relid = InvalidOid; /* safety */
#if (PG_VERSION_NUM >= 130000)
	ObjectAddress               address;
#endif

        /* Parent's namespace and name */
        Oid                         parent_nsp;
        char                       *parent_name,
                                   *parent_nsp_name;
        char                        parent_persistence;

        /* Elements of the "CREATE TABLE" query tree */
        RangeVar                   *parent_rv;
        RangeVar                   *table_rv;
        TableLikeClause            *like_clause = makeNode(TableLikeClause);
        CreateStmt                 *createStmt = makeNode(CreateStmt);
        List                       *createStmts;
        ListCell                   *lc;

	elog(DEBUG1, "creating a temporary table like table with Oid %d", parent_relid);

        /* Lock parent and check if it exists */
        LockRelationOid(parent_relid, ShareUpdateExclusiveLock);
        if (!SearchSysCacheExists1(RELOID, ObjectIdGetDatum(parent_relid)))
                elog(ERROR, "relation %u does not exist", parent_relid);

        /* Cache parent's namespace and name */
        parent_name = get_rel_name(parent_relid);
        parent_nsp = get_rel_namespace(parent_relid);
        parent_nsp_name = get_namespace_name(parent_nsp);
        parent_persistence = get_rel_persistence(parent_relid);

        /* Make up parent's RangeVar */
        parent_rv = makeRangeVar(parent_nsp_name, parent_name, -1);
        parent_rv->relpersistence = parent_persistence;

	elog(DEBUG1, "Parent namespace: %s, parent relname: %s, parent oid: %d",
									parent_rv->schemaname,
									parent_rv->relname,
									parent_relid);

	/* Set name of temporary table same as parent table */
	table_rv = makeRangeVar("pg_temp", parent_rv->relname, -1);
        Assert(table_rv);

	elog(DEBUG1, "Initialize TableLikeClause structure");
        /* Initialize TableLikeClause structure */
        like_clause->relation            = copyObject(parent_rv);
        like_clause->options             = CREATE_TABLE_LIKE_DEFAULTS
						| CREATE_TABLE_LIKE_INDEXES
						| CREATE_TABLE_LIKE_CONSTRAINTS
#if (PG_VERSION_NUM >= 100000)
						| CREATE_TABLE_LIKE_IDENTITY
#endif
#if (PG_VERSION_NUM >= 120000)
						| CREATE_TABLE_LIKE_GENERATED
#endif
						| CREATE_TABLE_LIKE_COMMENTS;

	elog(DEBUG1, "Initialize CreateStmt structure");
        /* Initialize CreateStmt structure */
        createStmt->relation            = copyObject(table_rv);
	createStmt->relation->schemaname = NULL;
	createStmt->relation->relpersistence = RELPERSISTENCE_TEMP;
        createStmt->tableElts           = list_make1(copyObject(like_clause));
        createStmt->inhRelations        = NIL;
        createStmt->ofTypename          = NULL;
        createStmt->constraints         = NIL;
        createStmt->options             = NIL;
#if (PG_VERSION_NUM >= 120000)
        createStmt->accessMethod        = NULL;
#endif
	if (preserved)
		createStmt->oncommit    = ONCOMMIT_PRESERVE_ROWS;
	else
		createStmt->oncommit    = ONCOMMIT_DELETE_ROWS;
        createStmt->tablespacename      = NULL;
        createStmt->if_not_exists       = false;

	elog(DEBUG1, "Obtain the sequence of Stmts to create temporary table");
        /* Obtain the sequence of Stmts to create temporary table */
        createStmts = transformCreateStmt(createStmt, NULL);

	elog(DEBUG1, "Processing list of statements");
        /* Create the temporary table */
        foreach (lc, createStmts)
        {
                /* Fetch current CreateStmt */
                Node *cur_stmt = (Node *) lfirst(lc);

		elog(DEBUG1, "Processing statement of type %d", nodeTag(cur_stmt));
                if (IsA(cur_stmt, CreateStmt))
                {
			Datum           toast_options;
			static char     *validnsps[] = HEAP_RELOPT_NAMESPACES;
                        Oid             temp_relowner;

                        /* Temporary table owner must be current user */
                        temp_relowner = GetUserId();

			elog(DEBUG1, "Creating a temporary table and get its Oid");
                        /* Create a temporary table and save its Oid */
#if (PG_VERSION_NUM < 100000)
			temp_relid = DefineRelation((CreateStmt *) cur_stmt, RELKIND_RELATION, temp_relowner, NULL).objectId;
#elif (PG_VERSION_NUM < 130000)
			temp_relid = DefineRelation((CreateStmt *) cur_stmt, RELKIND_RELATION, temp_relowner, NULL, NULL).objectId;
#else
			address = DefineRelation((CreateStmt *) cur_stmt, RELKIND_RELATION, temp_relowner, NULL, NULL);
			temp_relid = address.objectId;
#endif
			/* Update config one more time */
			CommandCounterIncrement();

			/*
			 * parse and validate reloptions for the toast
			 * table
			 */
			toast_options = transformRelOptions((Datum) 0,
									((CreateStmt *) cur_stmt)->options,
									"toast",
									validnsps,
									true,
									false);

			(void) heap_reloptions(RELKIND_TOASTVALUE, toast_options, true);

			NewRelationCreateToastTable(temp_relid, toast_options);
                }
		else if (IsA(cur_stmt, IndexStmt))
		{
			Oid                     relid;
			elog(DEBUG1, "execution statement CREATE INDEX, relation has an index.");

			relid =
				RangeVarGetRelidExtended(((IndexStmt *) cur_stmt)->relation, ShareLock,
#if (PG_VERSION_NUM >= 110000)
										 0,
#else
										 false, false,
#endif
										 RangeVarCallbackOwnsRelation,
										 NULL);

			DefineIndex(relid,      /* OID of heap relation */
						(IndexStmt *) cur_stmt,
						InvalidOid, /* no predefined OID */
#if (PG_VERSION_NUM >= 110000)
						InvalidOid, /* no parent index */
						InvalidOid, /* no parent constraint */
#endif
#if (PG_VERSION_NUM >= 160000)
						-1,/* total parts */
#endif
						false,  /* is_alter_table */
						true,   /* check_rights */
#if (PG_VERSION_NUM > 100000)
						true,   /* check_not_in_use */
#endif
						false,  /* skip_build */
						false); /* quiet */
		}
		else if (IsA(cur_stmt, CommentStmt))
		{
			CommentObject((CommentStmt *) cur_stmt);
		}
#if (PG_VERSION_NUM >= 90600)
		else if (IsA(cur_stmt, TableLikeClause))
		{
			TableLikeClause *like = (TableLikeClause *) cur_stmt;
			RangeVar   *rv = createStmt->relation;
			List       *morestmts;

			morestmts = expandTableLikeClause(rv, like);
			createStmts = list_concat(createStmts, morestmts);
			/* don't need a CCI now */
			continue;
		}
#endif
		else
		{
			/*
			 * Recurse for anything else.
			 */
#if PG_VERSION_NUM >= 100000
                        PlannedStmt *stmt = makeNode(PlannedStmt);
                        stmt->commandType       = CMD_UTILITY;
                        stmt->canSetTag         = true;
                        stmt->utilityStmt       = cur_stmt;
                        stmt->stmt_location     = -1;
                        stmt->stmt_len          = 0;
			ProcessUtility(stmt,
								 "PGTT provide a query string",
#if PG_VERSION_NUM >= 140000
								 false,
#endif
								 PROCESS_UTILITY_SUBCOMMAND,
								 NULL, NULL,
								 None_Receiver,
								 NULL);
#else
			ProcessUtility(cur_stmt,
								 "PGTT provide a query string",
#if PG_VERSION_NUM >= 140000
								 false,
#endif
								 PROCESS_UTILITY_SUBCOMMAND,
								 NULL,
								 None_Receiver,
								 NULL);
#endif
		}

		/* Need CCI between commands */
#if (PG_VERSION_NUM < 130000)
		if (lnext(lc) != NULL)
#else
		if (lnext(createStmts, lc) != NULL)
#endif
			CommandCounterIncrement();
        }

	/* release lock on "template" relation */
        UnlockRelationOid(parent_relid, ShareUpdateExclusiveLock);

	elog(DEBUG1, "Create a temporary table done with Oid: %d", temp_relid);
        return temp_relid;
}

/*
 * Post-parse-analysis hook: mark query with a queryId
 */
static void
#if PG_VERSION_NUM >= 140000
gtt_post_parse_analyze(ParseState *pstate, Query *query, struct JumbleState * jstate)
#else
gtt_post_parse_analyze(ParseState *pstate, Query *query)
#endif
{
	if (NOT_IN_PARALLEL_WORKER && pgtt_is_enabled && query->rtable != NIL)
	{
		/* replace the Oid of the template table by our new table in the rtable */
		RangeTblEntry *rte = (RangeTblEntry *) linitial(query->rtable);
		Relation      rel;
		Gtt           gtt;
		char          *name = NULL;

		/* This must be a valid relation not from pg_catalog*/
		if (rte->relid != InvalidOid && rte->relkind == RELKIND_RELATION
				&& !is_catalog_relid(rte->relid))
		{
#if (PG_VERSION_NUM >= 120000)
			rel = table_open(rte->relid, NoLock);
#else
			rel = heap_open(rte->relid, NoLock);
#endif
			name = RelationGetRelationName(rel);
#if (PG_VERSION_NUM >= 120000)
			table_close(rel, NoLock);
#else
			heap_close(rel, NoLock);
#endif

			gtt.relid = 0;
			gtt.temp_relid = 0;
			gtt.relname[0] = '\0';
			gtt.preserved = false;
			gtt.code = NULL;
			gtt.created = false;

			/* Check if the table is in the hash list and it has not already be created */
			if (PointerIsValid(name))
			{
				elog(DEBUG1, "gtt_post_parse_analyze() looking for table \"%s\" with relid %d into cache.", name, rte->relid);
				GttHashTableLookup(name, gtt);
			}
			else
				elog(ERROR, "gtt_post_parse_analyze() table to search in cache is not valide pointer, relid: %d.", rte->relid);

			if (gtt.relname[0] != '\0')
			{
				/* After an error and rollback the table is still registered in cache but must be initialized */
				if (gtt.created && OidIsValid(gtt.temp_relid)
						&& !SearchSysCacheExists1(RELOID, ObjectIdGetDatum(gtt.temp_relid))
						)
				{
					elog(DEBUG1, "invalid temporary table with relid %d (%s), reseting.", gtt.temp_relid, gtt.relname);
					gtt.created = false;
					gtt.temp_relid = 0;
				}
				/* Create the temporary table if it does not exists */
				if (!gtt.created)
				{
					elog(DEBUG1, "global temporary table from relid %d does not exists create it: %s", rte->relid, gtt.relname);
					/* Call create temporary table */
					if ((gtt.temp_relid = create_temporary_table_internal(gtt.relid, gtt.preserved)) != InvalidOid)
					{
						elog(DEBUG1, "global temporary table %s (oid: %d) created", gtt.relname, gtt.temp_relid);
						/* Update hash list with table flagged as created*/
						gtt.created = true;
						GttHashTableDelete(gtt.relname);
						GttHashTableInsert(gtt, gtt.relname);
					}
					else
						elog(ERROR, "can not create global temporary table %s", gtt.relname);
				}

				elog(DEBUG1, "temporary table exists with oid %d", gtt.temp_relid);

				if (rte->relid != gtt.temp_relid)
				{
#if PG_VERSION_NUM >= 160000
					RTEPermissionInfo *rteperm = list_nth(query->rteperminfos,
											rte->perminfoindex - 1);
					rteperm->relid = gtt.temp_relid;
#endif
					if (rte->rellockmode != AccessShareLock)
					{
						LockRelationOid(gtt.temp_relid, rte->rellockmode);
						UnlockRelationOid(rte->relid, rte->rellockmode);
					}
					rte->relid = gtt.temp_relid;
					elog(DEBUG1, "rerouting relid %d access to %d for GTT table \"%s\"", rte->relid, gtt.temp_relid, name);
				}
			}
			else
				/* the table is not a global temporary table do nothing*/
				elog(DEBUG1, "table \"%s\" not registered as GTT", name);
		}
	}

	/* restore hook */
        if (prev_post_parse_analyze_hook) {
#if PG_VERSION_NUM >= 140000
		prev_post_parse_analyze_hook(pstate, query, jstate);
#else
		prev_post_parse_analyze_hook(pstate, query);
#endif
	}
}

static bool
is_catalog_relid(Oid relid)
{
	HeapTuple       reltup;
	Form_pg_class   relform;
	Oid             relnamespace;

	reltup = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(reltup))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	relform = (Form_pg_class) GETSTRUCT(reltup);
        relnamespace = relform->relnamespace;
	ReleaseSysCache(reltup);
        if (relnamespace == PG_CATALOG_NAMESPACE || relnamespace == PG_TOAST_NAMESPACE)
	{
		elog(DEBUG1, "relation %d is in pg_catalog or pg_toast schema, nothing to do.", relid);
		return true;
	}

	return false;
}

/*
 * Be sure that extension schema is at end of the search path so that
 * "template" tables will be find.
 */
static void
force_pgtt_namespace (void)
{
#if PG_VERSION_NUM >= 170000
	SearchPathMatcher  *overridePath = GetSearchPathMatcher(CurrentMemoryContext);
#else
	OverrideSearchPath *overridePath = GetOverrideSearchPath(CurrentMemoryContext);
#endif
	ListCell           *lc;
	Oid                schemaId = InvalidOid;
	StringInfoData     search_path;
	bool               found = false;
	bool               first = true;

	initStringInfo(&search_path);
	/* verify that extension schema is in the path */
	foreach(lc, overridePath->schemas)
	{
		schemaId = lfirst_oid(lc);
		if (schemaId == InvalidOid)
			continue;
		if (schemaId == pgtt_namespace_oid)
			found = true;
		if (!first)
			appendStringInfoChar(&search_path, ',');
		appendStringInfo(&search_path, "%s", quote_identifier(get_namespace_name(schemaId)));
		first = false;
	}

	if (!found)
	{
		if (!first)
			appendStringInfoChar(&search_path, ',');
		appendStringInfo(&search_path, "%s", quote_identifier(pgtt_namespace_name));
		/*
		 * Override the search_path by adding our pgtt schema
		 */
		(void) set_config_option("search_path",
                                                                         search_path.data,
                                                                         (superuser() ? PGC_SUSET : PGC_USERSET),
                                                                         PGC_S_SESSION,
                                                                         GUC_ACTION_SET, true, 0,
									 false
									 );
	}
	elog(DEBUG1, "search_path forced to %s.", search_path.data);
}

/*
 * Update a registered Global Temporary Table 
 * in the pg_global_temp_tables table.
 *
 */
static void
gtt_update_registered_table(Gtt gtt)
{
	char    *newQueryString = NULL;
	int     connected = 0;
	int     finished = 0;
	int     result = 0;

	elog(DEBUG1, "proceeding to Global Temporary Table creation.");

	connected = SPI_connect();
	if (connected != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("could not connect to SPI manager")));

	newQueryString = psprintf("UPDATE %s.pg_global_temp_tables SET relname = '%s' WHERE relid = %d",
			quote_identifier(pgtt_namespace_name),
			gtt.relname,
			gtt.relid
		);
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("can not update relid %d into %s.pg_global_temp_tables", gtt.relid, quote_identifier(pgtt_namespace_name))));

	finished = SPI_finish();
	if (finished != SPI_OK_FINISH)
		ereport(ERROR, (errmsg("could not disconnect from SPI manager")));
}

/*
 * Create the temporary table related to a Global Temporary Table
 * and register the GTT in pg_global_temp_tables table.
 *
 */
static void
gtt_create_table_as(Gtt gtt, bool skipdata)
{
	char    *newQueryString = NULL;
	int     connected = 0;
	int     finished = 0;
	int     result = 0;
	Oid     gttOid = InvalidOid;
	Datum   oidDatum;
	bool    isnull;

	elog(DEBUG1, "proceeding to Global Temporary Table creation.");

	connected = SPI_connect();
	if (connected != SPI_OK_CONNECT)
		ereport(ERROR, (errmsg("could not connect to SPI manager")));

	/* Create the "template" table */
	newQueryString = psprintf("CREATE UNLOGGED TABLE %s.%s %s;",
			quote_identifier(pgtt_namespace_name),
			quote_identifier(gtt.relname),
			gtt.code);
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

	/* Get Oid of the newly created table */
	newQueryString = psprintf("SELECT c.relfilenode FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE c.relname='%s' AND n.nspname = '%s'",
			gtt.relname,
			pgtt_namespace_name);

        result = SPI_exec(newQueryString, 0);
        if (result != SPI_OK_SELECT && SPI_processed != 1)
                ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

	oidDatum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &isnull);

	if (!isnull)
		gttOid = DatumGetInt32(oidDatum);

	if (isnull || !OidIsValid(gttOid))
                ereport(ERROR,
				(errmsg("can not get OID of newly created GTT template table %s",
							quote_identifier(gtt.relname))));

	gtt.relid = gttOid;

	/* Create the temporary table only if data from source table must be inserted */
	if (!skipdata)
	{
		char namespaceName[NAMEDATALEN];

		/* Get current temporary namespace name */
		snprintf(namespaceName, sizeof(namespaceName), "pg_temp_%d", MyBackendId);
		
		newQueryString = psprintf("CREATE TEMPORARY TABLE %s %s WITH DATA",
				quote_identifier(gtt.relname),
				gtt.code);
		result = SPI_exec(newQueryString, 0);
		if (result < 0)
			ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

		/* Get Oid of the newly created temporary table */
		newQueryString = psprintf("SELECT c.relfilenode FROM pg_class c JOIN pg_namespace n ON (c.relnamespace=n.oid) WHERE c.relname='%s' AND n.nspname = '%s'",
				gtt.relname,
				namespaceName);

		result = SPI_exec(newQueryString, 0);
		if (result != SPI_OK_SELECT && SPI_processed != 1)
			ereport(ERROR, (errmsg("execution failure on query: \"%s\"", newQueryString)));

		oidDatum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &isnull);

		if (!isnull)
			gtt.temp_relid = DatumGetInt32(oidDatum);
		
		if (isnull || !OidIsValid(gttOid))
			ereport(ERROR,
					(errmsg("can not get OID of newly created temporary table %s",
								quote_identifier(gtt.relname))));
		gtt.created = true;
	}

	/* Now register the GTT table */
	newQueryString = psprintf("INSERT INTO %s.pg_global_temp_tables VALUES (%d, '%s', '%s', '%c', %s)",
			quote_identifier(pgtt_namespace_name),
			gtt.relid,
			pgtt_namespace_name,
			gtt.relname,
			(gtt.preserved) ? 't' : 'f',
			quote_literal_cstr(gtt.code)
		);
        result = SPI_exec(newQueryString, 0);
        if (result < 0)
                ereport(ERROR, (errmsg("can not registrer new global temporary table")));

	finished = SPI_finish();
	if (finished != SPI_OK_FINISH)
		ereport(ERROR, (errmsg("could not disconnect from SPI manager")));

	/* registrer the table in the cache */
	GttHashTableDelete(gtt.relname);
	GttHashTableInsert(gtt, gtt.relname);
}

int
strremovestr(char *src, char *toremove)
{
	while( *src )
	{
		char *k=toremove,*s=src;
		while( *k && *k==*s ) ++k,++s;
		if( !*k )
		{
			while( *s ) *src++=*s++;
			*src=0;
			return 1;
		}
		++src;
	}
	return 0;
}

