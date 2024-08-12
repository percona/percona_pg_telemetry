/*-------------------------------------------------------------------------
 *
 * percona_pg_telemetry.h
 *      Collects telemetry information for the database cluster.
 *
 * IDENTIFICATION
 *    contrib/percona_pg_telemetry/percona_pg_telemetry.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef __PERCONA_PG_TELEMETRY_H__
#define __PERCONA_PG_TELEMETRY_H__

#include "miscadmin.h"
#include "access/xact.h"
#include "storage/ipc.h"


/* Struct to store pg_settings data */
typedef struct PTSettingsInfo
{
	char	   *name;
	char	   *unit;
	char	   *settings;
	char	   *reset_val;
	char	   *boot_val;
} PTSetttingsInfo;

/* Struct to keep track of databases telemetry data */
typedef struct PTDatabaseInfo
{
	Oid			datid;
	char		datname[NAMEDATALEN];
	int64		datsize;
} PTDatabaseInfo;

/* Struct to keep track of extensions of a database */
typedef struct PTExtensionInfo
{
	char		extname[NAMEDATALEN];
	PTDatabaseInfo *db_data;
} PTExtensionInfo;

/*
 * Shared state to telemetry. We don't need any locks as we run only one
 * background worker at a time which may update this in case of an error.
 */
typedef struct PTSharedState
{
	int			error_code;
	int			json_file_indent;
	PTDatabaseInfo dbinfo;
	bool		first_db_entry;
	bool		last_db_entry;
	bool		write_in_progress;
	TimestampTz last_file_processed;
	int			curr_file_index;
	char		telemetry_path[MAXPGPATH];
	char		dbtemp_filepath[MAXPGPATH];
	char		telemetry_filenames[FLEXIBLE_ARRAY_MEMBER][MAXPGPATH];
} PTSharedState;

/* Defining error codes */
#define PT_SUCCESS          0
#define PT_DB_ERROR         1
#define PT_FILE_ERROR       2
#define PT_JSON_ERROR       3

/* Must use to exit a background worker process. */
#define PT_WORKER_EXIT(e_code)                  \
{                                               \
    if (IsTransactionBlock())                   \
        CommitTransactionCommand();             \
    if (e_code != PT_SUCCESS)                   \
        ereport(LOG, (errmsg("percona_pg_telemetry bgworker exiting with error_code = %d", e_code)));    \
    proc_exit(0);                               \
}

#endif
