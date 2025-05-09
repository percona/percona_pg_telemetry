/*-------------------------------------------------------------------------
 *
 * pt_json.c
 *      For building the required json structure for telemetry.
 *
 * IDENTIFICATION
 *    contrib/percona_pg_telemetry/pt_json.c
 *
 *-------------------------------------------------------------------------
 */

#include "pt_json.h"
#include "percona_pg_telemetry.h"

#include <sys/stat.h>

/* Local functions */
static char *json_escape_str(char *str);

/*
 * Escapes a JSON string to avoid malformation of a json value. Returns
 * a palloced string that caller must pfree.
 */
char *
json_escape_str(char *str)
{
	int			len;
	int			maxlen;
	char	   *str_escaped;
	char	   *s;

	if (str == NULL)
		return NULL;

	len = strlen(str);
	maxlen = (len > 0) ? len * 2 : 1;

	/* Max we'd need twice the space. */
	str_escaped = (char *) palloc(maxlen);
	s = str_escaped;

	for (int i = 0; i < len; i++)
	{
		/* Escape the quote and backslash characters. */
		if (str[i] == '"' || str[i] == '\\')
			*s++ = '\\';

		*s++ = str[i];
	}

	/* Ensure that we always end up with a string value. */
	*s = '\0';

	return str_escaped;
}

/*
 * Construct a JSON block for writing. The function created json segment based
 * on flags argument and provided key and value. json_file_indent is used
 * to keep track of the indentation level. Result is written to the provided buffer.
 */
char *
construct_json_block(char *buf, size_t buf_sz, char *key, char *raw_value, int flags, int *json_file_indent)
{
	char	   *value = NULL;
	char		str[2048] = {0};
	char		fmt_str[2048] = {0};
	char		comma = (flags & PT_JSON_LAST_ELEMENT) ? '\0' : ',';

	/* Make the string empty so that we can always concat. */
	buf[0] = '\0';

	if (raw_value)
		value = json_escape_str(raw_value);

	if (flags & PT_JSON_KEY)
		snprintf(str, sizeof(str), "\"%s\": ", key);

	if (flags & PT_JSON_OBJECT_START)
	{
		strlcat(str, "{", sizeof(str));
		PT_FORMAT_JSON(fmt_str, sizeof(fmt_str), str, (*json_file_indent));
		strlcat(buf, fmt_str, buf_sz);

		(*json_file_indent)++;
	}

	if (flags & PT_JSON_VALUE)
	{
		char		v[2048] = {0};

		snprintf(v, sizeof(v), "\"%s\"%c", value, comma);

		strlcat(str, v, sizeof(str));
		PT_FORMAT_JSON(fmt_str, sizeof(fmt_str), str, (*json_file_indent));
		strlcat(buf, fmt_str, buf_sz);
	}

	if (flags & PT_JSON_ARRAY_START)
	{
		strlcat(str, "[", sizeof(str));
		PT_FORMAT_JSON(fmt_str, sizeof(fmt_str), str, (*json_file_indent));
		strlcat(buf, fmt_str, buf_sz);

		(*json_file_indent)++;
	}

	/* Value is not an array so we can close the block. */
	if (flags & PT_JSON_ARRAY_END)
	{
		char		closing[3] = {']', comma, '\0'};

		(*json_file_indent)--;

		PT_FORMAT_JSON(fmt_str, sizeof(fmt_str), closing, (*json_file_indent));
		strlcat(buf, fmt_str, buf_sz);
	}

	/* Value is not an array so we can close the block. */
	if (flags & PT_JSON_OBJECT_END)
	{
		char		closing[3] = {'}', comma, '\0'};

		(*json_file_indent)--;

		PT_FORMAT_JSON(fmt_str, sizeof(fmt_str), closing, (*json_file_indent));
		strlcat(buf, fmt_str, buf_sz);
	}

	if (value)
		pfree(value);

	return buf;
}

/*
 * Open telemetry file in the given mode.
 */
FILE *
open_telemetry_file(char *filename, char *mode)
{
	FILE	   *fp;

	fp = fopen(filename, mode);
	if (fp == NULL)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("Could not open file %s for writing.", filename)));
		PT_WORKER_EXIT(PT_FILE_ERROR);
	}

	return fp;
}

/*
 * Write data to telemetry file.
 */
void
write_telemetry_file(FILE *fp, char *data)
{
	int			len;
	int			bytes_written;

	len = strlen(data);
	bytes_written = fwrite(data, 1, len, fp);

	if (len != bytes_written)
	{
		ereport(LOG,
				(errcode_for_file_access(),
				 errmsg("Could not write to json file.")));

		fclose(fp);
		PT_WORKER_EXIT(PT_FILE_ERROR);
	}
}
