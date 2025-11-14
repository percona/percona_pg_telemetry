/*-------------------------------------------------------------------------
 *
 * percona_pg_telemetry.c
 *      Collects telemetry information for the database cluster.
 *
 * IDENTIFICATION
 *    contrib/percona_pg_telemetry/percona_pg_telemetry.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

/* General defines */
#define PT_BUILD_VERSION    "1.2"

/* Init and exported functions */
void		_PG_init(void);

PG_FUNCTION_INFO_V1(percona_pg_telemetry_status);
PG_FUNCTION_INFO_V1(percona_pg_telemetry_version);

/*
 * Initializing everything here
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	elog(NOTICE, "percona_pg_telemetry has been deprecated");
}

/*
 * Select the status of percona_pg_telemetry.
 */
Datum
percona_pg_telemetry_status(PG_FUNCTION_ARGS)
{
#define PT_STATUS_COLUMN_COUNT  2

	TupleDesc	tupdesc;
	Datum		values[PT_STATUS_COLUMN_COUNT];
	bool		nulls[PT_STATUS_COLUMN_COUNT] = {false};
	HeapTuple	tup;
	Datum		result;

	tupdesc = CreateTemplateTupleDesc(PT_STATUS_COLUMN_COUNT);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "latest_output_filename", TEXTOID, -1, 0);
	TupleDescInitEntry(tupdesc, (AttrNumber) 2, "pt_enabled", BOOLOID, -1, 0);
	tupdesc = BlessTupleDesc(tupdesc);

	nulls[0] = true;
	values[1] = BoolGetDatum(false);

	tup = heap_form_tuple(tupdesc, values, nulls);
	result = HeapTupleGetDatum(tup);

	PG_RETURN_DATUM(result);
}

/*
 * Select the version of percona_pg_telemetry.
 */
Datum
percona_pg_telemetry_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(PT_BUILD_VERSION));
}
