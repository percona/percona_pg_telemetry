#include "postgres.h"
#include "fmgr.h"
#include "funcapi.h"
#include "pg_config.h"
#include "utils/guc_tables.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/relscan.h"
#include "access/table.h"
#include "access/tableam.h"
#include "commands/dbcommands.h"
#include "catalog/namespace.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "postmaster/bgworker.h"
#include "storage/fd.h"
#include "storage/latch.h"
#include "storage/proc.h"
#include "utils/builtins.h"
#include "utils/guc.h"
#include "utils/fmgroids.h"
#include "utils/fmgrprotos.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/syscache.h"
#include "utils/snapmgr.h"

#include "percona_telemetry.h"
#include "pt_json.h"

#if PG_VERSION_NUM >= 130000
#include "postmaster/interrupt.h"
#endif
#if PG_VERSION_NUM >= 140000
#include "utils/backend_status.h"
#include "utils/wait_event.h"
#else
#include "pgstat.h"
#endif

#include <sys/stat.h>

PG_MODULE_MAGIC;

/* General defines */
#define PT_BUILD_VERSION    "1.0"
#define PT_FILENAME_BASE    "percona_telemetry"

/* Init and exported functions */
void _PG_init(void);
PGDLLEXPORT void percona_telemetry_main(Datum);
PGDLLEXPORT void percona_telemetry_worker(Datum);

PG_FUNCTION_INFO_V1(percona_telemetry_status);
PG_FUNCTION_INFO_V1(percona_telemetry_version);


/* Internal init, shared memeory and signal functions */
static void init_guc(void);
static void pt_sigterm(SIGNAL_ARGS);
static void pt_shmem_init(void);
static void pt_shmem_request(void);

#if PG_VERSION_NUM >= 150000
static shmem_request_hook_type prev_shmem_request_hook = NULL;
#endif

/* Helper functions */
static BgwHandleStatus setup_background_worker(const char *bgw_function_name, const char *bgw_name, const char *bgw_type, Oid datid, pid_t bgw_notify_pid);
static void start_leader(void);
static long server_uptime(void);
static void cleaup_telemetry_dir(void);
static char *generate_filename(char *filename);
static bool validate_dir(char *folder_path);

/* Database information collection and writing to file */
static void write_pg_settings(void);
static List *get_database_list(void);
static List *get_extensions_list(PTDatabaseInfo *dbinfo, MemoryContext cxt);
static bool write_database_info(PTDatabaseInfo *dbinfo, List *extlist);

/* Shared state stuff */
static PTSharedState *ptss = NULL;

#define PT_SHARED_STATE_HEADER_SIZE     (offsetof(PTSharedState, telemetry_filenames))
#define PT_SHARED_STATE_PREV_FILE_SIZE  (files_to_keep * MAXPGPATH)
#define PT_SHARED_STATE_SIZE            (PT_SHARED_STATE_HEADER_SIZE + PT_SHARED_STATE_PREV_FILE_SIZE)
#define PT_DEFAULT_FOLDER_PATH          "/usr/local/percona/telemetry/pg"

/* variable for signal handlers' variables */
static volatile sig_atomic_t sigterm_recvd = false;

/* GUC variables */
char *t_folder = PT_DEFAULT_FOLDER_PATH;
int scrape_interval = HOURS_PER_DAY * MINS_PER_HOUR * SECS_PER_MINUTE;
bool telemetry_enabled = true;
int files_to_keep = 7;

/* General global variables */
static MemoryContext pt_cxt;

/*
 * Signal handler for SIGTERM. When received, sets the flag and the latch.
 */
static void
pt_sigterm(SIGNAL_ARGS)
{
    sigterm_recvd = true;

    /* Only if MyProc is set... */
    if (MyProc != NULL)
    {
        SetLatch(&MyProc->procLatch);
    }
}

/*
 * Initializing everything here
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	init_guc();

#if PG_VERSION_NUM >= 150000
    prev_shmem_request_hook = shmem_request_hook;
    shmem_request_hook = pt_shmem_request;
#else
    pt_shmem_request();
#endif

	start_leader();
}

/*
 * Start the launcher process
 */
static void
start_leader(void)
{
    setup_background_worker("percona_telemetry_main", "percona_telemetry launcher", "percona_telemetry launcher", InvalidOid, 0);
}

/*
 *
 */
static char *
generate_filename(char *filename)
{
    char f_name[MAXPGPATH];
    uint64 system_id = GetSystemIdentifier();
    time_t currentTime;

    time(&currentTime);
    pg_snprintf(f_name, MAXPGPATH, "%s-%lu-%ld.json", PT_FILENAME_BASE, system_id, currentTime);

    join_path_components(filename, ptss->pg_telemetry_folder, f_name);

    return filename;
}

/*
 *
 */
static char *
telemetry_add_filename(char *filename)
{
    Assert(filename);

    snprintf(ptss->telemetry_filenames[ptss->curr_file_index], MAXPGPATH, "%s", filename);
    return ptss->telemetry_filenames[ptss->curr_file_index];
}

/*
 *
 */
static char *
telemetry_curr_filename(void)
{
    return ptss->telemetry_filenames[ptss->curr_file_index];
}

/*
 *
 */
static bool
telemetry_file_is_valid(void)
{
    return (*ptss->telemetry_filenames[ptss->curr_file_index] != '\0');
}

/*
 *
 */
static char *
telemetry_file_next(char *filename)
{
    char *curr_oldest = telemetry_curr_filename();
    ptss->curr_file_index = (ptss->curr_file_index + 1) % files_to_keep;

    /* Remove the existing file on this location if valid */
    if (telemetry_file_is_valid())
    {
        PathNameDeleteTemporaryFile(ptss->telemetry_filenames[ptss->curr_file_index], false);
    }

    telemetry_add_filename(filename);

    return (*curr_oldest) ? curr_oldest : NULL;
}

/*
 *
 */
static void
cleaup_telemetry_dir(void)
{
    DIR *d;
    struct dirent *de;
    uint64 system_id = GetSystemIdentifier();
    char json_file_id[MAXPGPATH];
    int file_id_len;

    validate_dir(ptss->pg_telemetry_folder);

    d = AllocateDir(ptss->pg_telemetry_folder);

    if (d == NULL)
    {
        ereport(ERROR,
                        (errcode_for_file_access(),
                            errmsg("could not open percona telemetry directory \"%s\": %m",
                                        ptss->pg_telemetry_folder)));
    }

    pg_snprintf(json_file_id, sizeof(json_file_id), "%s-%lu", PT_FILENAME_BASE, system_id);
    file_id_len = strlen(json_file_id);

    while ((de = ReadDir(d, ptss->pg_telemetry_folder)) != NULL)
    {
        if (strncmp(json_file_id, de->d_name, file_id_len) == 0)
        {
            telemetry_file_next(de->d_name);
        }
    }

    FreeDir(d);
}

/*
 * pg_telemetry_folder
 */
bool
validate_dir(char *folder_path)
{
    struct stat st;
    bool is_dir = false;

    /* Let's validate the path. */
    if (stat(folder_path, &st) == 0)
    {
        is_dir = S_ISDIR(st.st_mode);
    }

    if (is_dir == false)
    {
        ereport(LOG,
                (errcode_for_file_access(),
                 errmsg("percont_telemetry.pg_telemetry_folder \"%s\" is not set to a writeable folder or the folder does not exist.", folder_path)));

        PT_WORKER_EXIT(PT_FILE_ERROR);
    }

    return is_dir;
}

/*
 * Select the status of percona_telemetry.
 */
Datum
percona_telemetry_status(PG_FUNCTION_ARGS)
{
#define PT_STATUS_COLUMN_COUNT  2

    TupleDesc tupdesc;
    Datum values[PT_STATUS_COLUMN_COUNT];
    bool nulls[PT_STATUS_COLUMN_COUNT] = {false};
    HeapTuple tup;
    Datum result;
    int col_index = 0;

    /* Initialize shmem */
    pt_shmem_init();

    tupdesc = CreateTemplateTupleDesc(PT_STATUS_COLUMN_COUNT);
    TupleDescInitEntry(tupdesc, (AttrNumber) 1, "latest_output_filename", TEXTOID, -1, 0);
    TupleDescInitEntry(tupdesc, (AttrNumber) 2, "pt_enabled", BOOLOID, -1, 0);
    tupdesc = BlessTupleDesc(tupdesc);

    col_index = 0;
    if (telemetry_curr_filename()[0] != '\0')
        values[col_index] = CStringGetTextDatum(telemetry_curr_filename());
    else
        nulls[col_index] = true;

    col_index++;
    values[col_index] = BoolGetDatum(telemetry_enabled);

    tup = heap_form_tuple(tupdesc, values, nulls);
    result = HeapTupleGetDatum(tup);

	PG_RETURN_DATUM(result);
}

/*
 * Select the version of percona_telemetry.
 */
Datum
percona_telemetry_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(PT_BUILD_VERSION));
}

/*
 * Request additional shared memory required
 */
static void
pt_shmem_request(void)
{
#if PG_VERSION_NUM >= 150000
    if (prev_shmem_request_hook)
        prev_shmem_request_hook();
#endif

    RequestAddinShmemSpace(MAXALIGN(PT_SHARED_STATE_SIZE));
}

/*
 * Initialize the shared memory
 */
static void
pt_shmem_init(void)
{
    bool found;

    LWLockAcquire(AddinShmemInitLock, LW_EXCLUSIVE);

    ptss = (PTSharedState *) ShmemInitStruct("percona_telemetry shared state", sizeof(PTSharedState), &found);
    if (!found)
    {
        uint64 system_id = GetSystemIdentifier();

        /* Set paths */
        strncpy(ptss->pg_telemetry_folder, t_folder, MAXPGPATH);
        pg_snprintf(ptss->dbtemp_filepath, MAXPGPATH, "%s/%s-%lu.temp", ptss->pg_telemetry_folder, PT_FILENAME_BASE, system_id);

        /* Let's be optimistic here. No error code and no file currently being written. */
        ptss->error_code = PT_SUCCESS;
        ptss->write_in_progress = false;
        ptss->json_file_indent = 0;
        ptss->first_db_entry = false;
        ptss->last_db_entry = false;

        ptss->curr_file_index = 0;
        memset(ptss->telemetry_filenames, 0, PT_SHARED_STATE_PREV_FILE_SIZE);
    }

    LWLockRelease(AddinShmemInitLock);
}

/*
 * Intialize the GUCs
 */
static void
init_guc(void)
{
    char       *env;

    /* is the extension enabled? */
    DefineCustomBoolVariable("percona_telemetry.enabled",
                             "Enable or disable the percona_telemetry extension",
                             NULL,
                             &telemetry_enabled,
                             true,
                             PGC_SIGHUP,
                             0,
                             NULL,
                             NULL,
                             NULL);

    env = getenv("PT_DEBUG");
    if (env != NULL)
    {
        /* file path */
        DefineCustomStringVariable("percona_telemetry.pg_telemetry_folder",
                                "Directory path for writing database info file(s)",
                                NULL,
                                &t_folder,
                                PT_DEFAULT_FOLDER_PATH,
                                PGC_SIGHUP,
                                0,
                                NULL,
                                NULL,
                                NULL);

        /* scan time interval for the main launch process */
        DefineCustomIntVariable("percona_telemetry.scrape_interval",
                                "Data scrape interval",
                                NULL,
                                &scrape_interval,
                                HOURS_PER_DAY * MINS_PER_HOUR * SECS_PER_MINUTE,
                                1,
                                INT_MAX,
                                PGC_SIGHUP,
                                GUC_UNIT_S,
                                NULL,
                                NULL,
                                NULL);

        /* Number of files to keep */
        DefineCustomIntVariable("percona_telemetry.files_to_keep",
                                "Number of JSON files to keep for this instance.",
                                NULL,
                                &files_to_keep,
                                7,
                                1,
                                100,
                                PGC_SIGHUP,
                                0,
                                NULL,
                                NULL,
                                NULL);
    }

#if PG_VERSION_NUM >= 150000
    MarkGUCPrefixReserved("percona_telemetry");
#endif
}

/*
 * Sets up a background worker. If we have received a pid to notify, then
 * setup a dynamic worker and wait until it shuts down. This prevents ending
 * up with multiple background workers and prevents overloading system
 * resources. So, we process databases sequentially which is fine.
 *
 * datid is ignored for launcher which is identified by a notify_pid = 0
 */
static BgwHandleStatus
setup_background_worker(const char *bgw_function_name, const char *bgw_name, const char *bgw_type, Oid datid, pid_t bgw_notify_pid)
{
    BackgroundWorker worker;
    BackgroundWorkerHandle *handle;

    MemSet(&worker, 0, sizeof(BackgroundWorker));
    worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
    worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = BGW_NEVER_RESTART;
    strcpy(worker.bgw_library_name, "percona_telemetry");
    strcpy(worker.bgw_function_name, bgw_function_name);
    strcpy(worker.bgw_name, bgw_name);
    strcpy(worker.bgw_type, bgw_type);
    worker.bgw_main_arg = ObjectIdGetDatum(datid);
    worker.bgw_notify_pid = bgw_notify_pid;

    /* Case of a launcher */
    if (bgw_notify_pid == 0)
    {
        /* Leader should never connect to a valid database. */
        worker.bgw_main_arg = ObjectIdGetDatum(InvalidOid);

        RegisterBackgroundWorker(&worker);

        /* Let's be optimistic about it's start. */
        return BGWH_STARTED;
    }

    /* Validate that it's a valid database Oid */
    Assert(datid != InvalidOid);
    worker.bgw_main_arg = ObjectIdGetDatum(datid);

    /*
     * Register the work and wait until it shuts down. This enforces creation
     * only one background worker process. So, don't have to implement any
     * locking for error handling or file writing.
     */
    RegisterDynamicBackgroundWorker(&worker, &handle);
    return WaitForBackgroundWorkerShutdown(handle);
}

/*
 * Returns string contain server uptime in seconds
 */
static long
server_uptime(void)
{
    long secs;
    int microsecs;

    TimestampDifference(PgStartTime, GetCurrentTimestamp(), &secs, &microsecs);

    return secs;
}

/*
 * Getting pg_settings values:
 * -> name, units, setting, boot_val, and reset_val
 */

#define PT_SETTINGS_COL_COUNT 5

static void
write_pg_settings(void)
{
    SPITupleTable *tuptable;
    int spi_result;
    char *query = "SELECT name, unit, setting, reset_val, boot_val FROM pg_settings where vartype != 'string'";
    char msg[2048] = {0};
    char msg_json[4096] = {0};
    size_t sz_json;
    FILE *fp;
    int flags;

    sz_json = sizeof(msg_json);

    /* Open file in append mode. */
    fp = json_file_open(ptss->dbtemp_filepath, "a+");

    /* Construct and initiate the active extensions array block. */
    construct_json_block(msg_json, sz_json, "", "settings", PT_JSON_ARRAY_START, &ptss->json_file_indent);
    write_json_to_file(fp, msg_json);

    SetCurrentStatementStartTimestamp();
    StartTransactionCommand();

    /* Initialize SPI */
    if (SPI_connect() != SPI_OK_CONNECT)
    {
        ereport(ERROR, (errmsg("Failed to connect to SPI")));
    }

    PushActiveSnapshot(GetTransactionSnapshot());

    /* Execute the query */
    spi_result = SPI_execute(query, true, 0);
    if (spi_result != SPI_OK_SELECT)
    {
        SPI_finish();
        ereport(ERROR, (errmsg("Query failed execution.")));
    }

    /* Process the result */
    if (SPI_processed > 0)
    {
        tuptable = SPI_tuptable;

        for (int row_count = 0; row_count < SPI_processed; row_count++)
        {
            char *null_value = "NULL";
            char  *value_str[PT_SETTINGS_COL_COUNT];


            /* Construct and initiate the active extensions array block. */
            construct_json_block(msg_json, sz_json, "setting", "", PT_JSON_BLOCK_ARRAY_VALUE, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Process the tuple as needed */
            for (int col_count = 1; col_count <= tuptable->tupdesc->natts; col_count++)
            {
                char *str = SPI_getvalue(tuptable->vals[row_count], tuptable->tupdesc, col_count);
                value_str[col_count - 1] = (str == NULL || str[0] == '\0') ? null_value : str;

                flags = (col_count == tuptable->tupdesc->natts) ? (PT_JSON_BLOCK_SIMPLE | PT_JSON_BLOCK_LAST) : PT_JSON_BLOCK_SIMPLE;

                construct_json_block(msg_json, sz_json, NameStr(tuptable->tupdesc->attrs[col_count - 1].attname),
                                            value_str[col_count - 1], flags, &ptss->json_file_indent);
                write_json_to_file(fp, msg_json);
            }

            /* Close the array */
            construct_json_block(msg, sizeof(msg), "setting", "", PT_JSON_ARRAY_END | PT_JSON_BLOCK_LAST, &ptss->json_file_indent);
            strcpy(msg_json, msg);

            /* Close the extension block */
            flags = (row_count == (SPI_processed - 1)) ? (PT_JSON_BLOCK_END | PT_JSON_BLOCK_LAST) : PT_JSON_BLOCK_END;
            construct_json_block(msg, sizeof(msg), "setting", "", flags, &ptss->json_file_indent);
            strlcat(msg_json, msg, sz_json);

            /* Write both to file. */
            write_json_to_file(fp, msg_json);
        }
    }

    /* Close the array */
    construct_json_block(msg, sizeof(msg), "settings", "", PT_JSON_ARRAY_END, &ptss->json_file_indent);
    strcpy(msg_json, msg);

    /* Write both to file. */
    write_json_to_file(fp, msg_json);

    /* Clean up */
    fclose(fp);

    /* Disconnect from SPI */
    SPI_finish();

    PopActiveSnapshot();
    CommitTransactionCommand();
}

#undef PT_SETTINGS_COL_COUNT

/*
 * Return a list of databases from the pg_database catalog
 */
static List *
get_database_list(void)
{
    List          *dblist = NIL;
    Relation      rel;
    TableScanDesc scan;
    HeapTuple     tup;
    MemoryContext oldcxt;
    ScanKeyData   key;

    /* Start a transaction to access pg_database */
    StartTransactionCommand();

    rel = relation_open(DatabaseRelationId, AccessShareLock);

    /* Ignore databases that we can't connect to */
    ScanKeyInit(&key,
                Anum_pg_database_datallowconn,
                BTEqualStrategyNumber,
                F_BOOLEQ,
                BoolGetDatum(true));

    scan = table_beginscan_catalog(rel, 1, &key);

    while (HeapTupleIsValid(tup = heap_getnext(scan, ForwardScanDirection)))
    {
        PTDatabaseInfo  *dbinfo;
        int64 datsize;
        Form_pg_database pgdatabase = (Form_pg_database) GETSTRUCT(tup);

        datsize = DatumGetInt64(DirectFunctionCall1(pg_database_size_oid, ObjectIdGetDatum(pgdatabase->oid)));

        /* Switch to our memory context instead of the transaction one */
        oldcxt = MemoryContextSwitchTo(pt_cxt);
        dbinfo = (PTDatabaseInfo *) palloc(sizeof(PTDatabaseInfo));

        /* Fill in the structure */
        dbinfo->datid = pgdatabase->oid;
        strncpy(dbinfo->datname, NameStr(pgdatabase->datname), sizeof(dbinfo->datname));
        dbinfo->datsize = datsize;

        /* Add to the list */
        dblist = lappend(dblist, dbinfo);

        /* Switch back the memory context */
        pt_cxt = MemoryContextSwitchTo(oldcxt);
    }

    /* Clean up */
    table_endscan(scan);
    relation_close(rel, AccessShareLock);

    CommitTransactionCommand();

    /* Return the list */
    return dblist;
}

/*
 * Get a list of installed extensions for a database
 */
static List *
get_extensions_list(PTDatabaseInfo *dbinfo, MemoryContext cxt)
{
    List *extlist = NIL;
    Relation rel;
    TableScanDesc scan;
    HeapTuple tup;
    MemoryContext oldcxt;

    Assert(dbinfo);

    /* Start a transaction to access pg_extensions */
    StartTransactionCommand();

    /* Open the extension catalog... */
    rel = table_open(ExtensionRelationId, AccessShareLock);
    scan = table_beginscan_catalog(rel, 0, NULL);

    while (HeapTupleIsValid(tup = heap_getnext(scan, ForwardScanDirection)))
    {
        PTExtensionInfo *extinfo;
        Form_pg_extension extform = (Form_pg_extension) GETSTRUCT(tup);

        /* Switch to the given memory context */
        oldcxt = MemoryContextSwitchTo(cxt);
        extinfo = (PTExtensionInfo *) palloc(sizeof(PTExtensionInfo));

        /* Fill in the structure */
        extinfo->db_data = dbinfo;
        strncpy(extinfo->extname, NameStr(extform->extname), sizeof(extinfo->extname));

        /* Add to the list */
        extlist = lappend(extlist, extinfo);

        /* Switch back the memory context */
        cxt = MemoryContextSwitchTo(oldcxt);
    }

    /* Clean up */
    table_endscan(scan);
    table_close(rel, AccessShareLock);

    CommitTransactionCommand();

    /* Return the list */
    return extlist;
}

/*
 * Writes database information along with names of the active extensions to
 * the file.
 */
static bool
write_database_info(PTDatabaseInfo *dbinfo, List *extlist)
{
    char msg[2048] = {0};
    char msg_json[4096] = {0};
    size_t sz_json;
    FILE *fp;
    ListCell *lc;
    int flags;

    sz_json = sizeof(msg_json);

    /* Open file in append mode. */
    fp = json_file_open(ptss->dbtemp_filepath, "a+");

    if (ptss->first_db_entry)
    {
        /* Construct and initiate the active extensions array block. */
        construct_json_block(msg_json, sz_json, "", "databases", PT_JSON_ARRAY_START, &ptss->json_file_indent);
        write_json_to_file(fp, msg_json);
    }

    /* Construct and initiate the active extensions array block. */
    construct_json_block(msg_json, sz_json, "database", "value", PT_JSON_BLOCK_ARRAY_VALUE, &ptss->json_file_indent);
    write_json_to_file(fp, msg_json);

    /* Construct and write the database OID block. */
    snprintf(msg, sizeof(msg), "%u", dbinfo->datid);
    construct_json_block(msg_json, sz_json, "database_oid", msg, PT_JSON_BLOCK_SIMPLE, &ptss->json_file_indent);
    write_json_to_file(fp, msg_json);

    /* Construct and write the database size block. */
    snprintf(msg, sizeof(msg), "%lu", dbinfo->datsize);
    construct_json_block(msg_json, sz_json, "database_size", msg, PT_JSON_BLOCK_SIMPLE, &ptss->json_file_indent);
    write_json_to_file(fp, msg_json);

    /* Construct and initiate the active extensions array block. */
    construct_json_block(msg_json, sz_json, "active_extensions", "value", PT_JSON_BLOCK_ARRAY_VALUE, &ptss->json_file_indent);
    write_json_to_file(fp, msg_json);

    /* Iterate through all extensions and those to the array. */
    foreach(lc, extlist)
	{
        PTExtensionInfo *extinfo = lfirst(lc);

        flags = (list_tail(extlist) == lc) ? (PT_JSON_BLOCK_SIMPLE | PT_JSON_BLOCK_LAST) : PT_JSON_BLOCK_SIMPLE;

        construct_json_block(msg_json, sz_json, "extension_name", extinfo->extname, flags, &ptss->json_file_indent);
        write_json_to_file(fp, msg_json);
    }

    /* Close the array and block and write to file */
    construct_json_block(msg, sizeof(msg), "active_extensions", "active_extensions", PT_JSON_ARRAY_END | PT_JSON_BLOCK_END | PT_JSON_BLOCK_LAST, &ptss->json_file_indent);
    strcpy(msg_json, msg);
    write_json_to_file(fp, msg_json);

    /* Close the array */
    construct_json_block(msg, sizeof(msg), "database", "", PT_JSON_ARRAY_END | PT_JSON_BLOCK_LAST, &ptss->json_file_indent);
    strcpy(msg_json, msg);

    /* Close the database block */
    flags = (ptss->last_db_entry) ? (PT_JSON_BLOCK_END | PT_JSON_BLOCK_LAST) : PT_JSON_BLOCK_END;
    construct_json_block(msg, sizeof(msg), "database", "", flags, &ptss->json_file_indent);
    strlcat(msg_json, msg, sz_json);

    /* Write both to file. */
    write_json_to_file(fp, msg_json);

    if (ptss->last_db_entry)
    {
        /* Close the array */
        construct_json_block(msg, sizeof(msg), "databases", "", PT_JSON_ARRAY_END | PT_JSON_BLOCK_LAST, &ptss->json_file_indent);
        strcpy(msg_json, msg);

        /* Write both to file. */
        write_json_to_file(fp, msg_json);
    }

    /* Clean up */
    fclose(fp);

    return true;
}

/*
 * Main function for the background launcher process
 */
void
percona_telemetry_main(Datum main_arg)
{
	int rc = 0;
    List *dblist = NIL;
    ListCell *lc = NULL;
    char json_pg_version[1024];
    FILE *fp;
    char msg[2048] = {0};
    char msg_json[4096] = {0};
    size_t sz_json = sizeof(msg_json);
    bool first_time = true;

    /* Save the version in a JSON escaped stirng just to be safe. */
    strcpy(json_pg_version, PG_VERSION);

    /* Setup signal callbacks */
    pqsignal(SIGTERM, pt_sigterm);
#if PG_VERSION_NUM >= 130000
    pqsignal(SIGHUP, SignalHandlerForConfigReload);
#else
    pqsignal(SIGHUP, PostgresSigHupHandler);
#endif

    /* We can now receive signals */
    BackgroundWorkerUnblockSignals();

    /* Initialize shmem */
    pt_shmem_init();

    /* Cleanup the directory */
    cleaup_telemetry_dir();

    /* Set up connection */
    BackgroundWorkerInitializeConnectionByOid(InvalidOid, InvalidOid, 0);

    /* Set name to make percona_telemetry visible in pg_stat_activity */
    pgstat_report_appname("percona_telemetry");

    /* This is the context that we will allocate our data in */
    pt_cxt = AllocSetContextCreate(TopMemoryContext, "Percona Telemetry Context", ALLOCSET_DEFAULT_SIZES);

    /* Should never really terminate unless... */
    while (!sigterm_recvd && ptss->error_code == PT_SUCCESS)
	{
        /* Don't sleep the first time */
        if (first_time == false)
        {
            rc = WaitLatch(MyLatch,
                        WL_LATCH_SET | WL_TIMEOUT | WL_POSTMASTER_DEATH,
                        scrape_interval * 1000L,
                        PG_WAIT_EXTENSION);

            ResetLatch(MyLatch);
        }

        CHECK_FOR_INTERRUPTS();

        if (ConfigReloadPending)
        {
            ConfigReloadPending = false;
            ProcessConfigFile(PGC_SIGHUP);
        }

        /* Don't do any processing but keep the launcher alive */
        if (telemetry_enabled == false)
            continue;

        /* Time to end the loop as the server is shutting down */
		if ((rc & WL_POSTMASTER_DEATH) || ptss->error_code != PT_SUCCESS)
			break;

        /* We are not processing a cell at the moment. So, let's get the updated database list. */
        if (dblist == NIL && (rc & WL_TIMEOUT || first_time))
        {
            char temp_buff[100];

            /* Data collection will start now */
            first_time = false;

            dblist = get_database_list();

            /* Set writing state to true */
            Assert(ptss->write_in_progress == false);
            ptss->write_in_progress = true;

            /* Open file for writing. */
            fp = json_file_open(ptss->dbtemp_filepath, "w");

            construct_json_block(msg_json, sz_json, "", "", PT_JSON_BLOCK_START, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Construct and write the database size block. */
            pg_snprintf(msg, sizeof(msg), "%lu", GetSystemIdentifier());
            construct_json_block(msg_json, sz_json, "db_instance_id", msg, PT_JSON_KEY_VALUE_PAIR, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Construct and initiate the active extensions array block. */
            construct_json_block(msg_json, sz_json, "pillar_version", json_pg_version, PT_JSON_KEY_VALUE_PAIR, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Construct and initiate the active extensions array block. */
            pg_snprintf(msg, sizeof(msg), "%ld", server_uptime());
            construct_json_block(msg_json, sz_json, "uptime", msg, PT_JSON_KEY_VALUE_PAIR, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Construct and initiate the active extensions array block. */
            pg_snprintf(temp_buff, sizeof(temp_buff), "%d", list_length(dblist));
            construct_json_block(msg_json, sz_json, "databases_count", temp_buff, PT_JSON_KEY_VALUE_PAIR, &ptss->json_file_indent);
            write_json_to_file(fp, msg_json);

            /* Let's close the file now so that processes may add their stuff. */
            fclose(fp);
        }

        /* Must be a valid list */
        if (dblist != NIL)
        {
            PTDatabaseInfo *dbinfo;
            BgwHandleStatus status;

            /* First or the next cell */
#if PG_VERSION_NUM >= 130000
            lc = (lc) ? lnext(dblist, lc) : list_head(dblist);
#else
            lc = (lc) ? lnext(lc) : list_head(dblist);
#endif

            ptss->first_db_entry = (lc == list_head(dblist));

            /*
             * We've reached end of the list. So, let's cleanup and go to
             * sleep until the timer runs out. Also, we need to move the
             * file to mark the process complete.
             */
            if (lc == NULL)
            {
                char filename[MAXPGPATH] = {0};

                list_free_deep(dblist);
                dblist = NIL;

                /* We should always have write_in_progress true here. */
                Assert(ptss->write_in_progress == true);

                /* Open file, writing the closing bracket and close it. */
                fp = json_file_open(ptss->dbtemp_filepath, "a+");
                construct_json_block(msg_json, sz_json, "", "", PT_JSON_BLOCK_END | PT_JSON_BLOCK_LAST, &ptss->json_file_indent);
                write_json_to_file(fp, msg_json);
                fclose(fp);

                /* Generate and save the filename */
                telemetry_file_next(generate_filename(filename));

	            /* Let's rename the temp file so that agent can pick it up. */
	            if (rename(ptss->dbtemp_filepath, telemetry_curr_filename()) < 0)
	            {
                    ereport(LOG,
                            (errcode_for_file_access(),
                             errmsg("could not rename file \"%s\" to \"%s\": %m",
                                    ptss->dbtemp_filepath,
                                    telemetry_curr_filename())));

                    ptss->error_code = PT_FILE_ERROR;
                    break;
                }

                ptss->write_in_progress = false;
                continue;
            }

            ptss->last_db_entry = (list_tail(dblist) == lc);
            dbinfo = lfirst(lc);
            memcpy(&ptss->dbinfo, dbinfo, sizeof(PTDatabaseInfo));

            /*
             * Run the dynamic background worker and wait for it's completion
             * so that we can wake up the launcher process.
             */
        	status = setup_background_worker("percona_telemetry_worker",
                                                "percona_telemetry worker",
                                                "percona_telemetry worker",
                                                ptss->dbinfo.datid, MyProcPid);

            /* Wakeup the main process since the worker has stopped. */
            if (status == BGWH_STOPPED)
                SetLatch(&MyProc->procLatch);
        }
    }

    /* Shouldn't really ever be here unless an error was encountered. So exit with the error code */
    ereport(LOG,
            (errmsg("Percona Telemetry main (PID %d) exited due to errono %d with enabled = %d",
                    MyProcPid,
                    ptss->error_code,
                    telemetry_enabled)));
	PT_WORKER_EXIT(PT_SUCCESS);
}

/*
 * Worker process main function
 */
void
percona_telemetry_worker(Datum main_arg)
{
    Oid datid;
    MemoryContext tmpcxt;
    List *extlist = NIL;

    /* Get the argument. Ensure that it's a valid oid in case of a worker */
    datid = DatumGetObjectId(main_arg);

    /* Initialize shmem */
    pt_shmem_init();
    Assert(datid != InvalidOid && ptss->dbinfo.datid == datid);

    /* Set up connection */
    BackgroundWorkerInitializeConnectionByOid(datid, InvalidOid, 0);

    /* This is the context that we will allocate our data in */
    tmpcxt = AllocSetContextCreate(TopMemoryContext, "Percona Telemetry Context (tmp)", ALLOCSET_DEFAULT_SIZES);

    /* Set name to make percona_telemetry visible in pg_stat_activity */
    pgstat_report_appname("percona_telemetry_worker");

    /* Get the settings */
    if (ptss->first_db_entry)
        write_pg_settings();

    extlist = get_extensions_list(&ptss->dbinfo, tmpcxt);

    if (write_database_info(&ptss->dbinfo, extlist) == false)
        PT_WORKER_EXIT(PT_FILE_ERROR);

    /* Ending the worker... */
    PT_WORKER_EXIT(PT_SUCCESS);
}