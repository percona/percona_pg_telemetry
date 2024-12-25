/*-------------------------------------------------------------------------
 *
 * pt_json.h
 *      For building the required json structure for telemetry.
 *
 * IDENTIFICATION
 *    contrib/percona_pg_telemetry/pt_json.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef __PG_PT_JSON_H__
#define __PG_PT_JSON_H__

#include "postgres.h"

/* JSON formatting defines */
#define PT_INDENT_SIZE      2
#define PT_FORMAT_JSON(dest, dest_size, str, indent_size)       \
            pg_snprintf(dest, dest_size, "%*s%s\n",             \
                        (indent_size) * PT_INDENT_SIZE, "", str)

#define PT_JSON_OBJECT_START             1
#define PT_JSON_OBJECT_END               1 << 1
#define PT_JSON_KEY                      1 << 2
#define PT_JSON_VALUE                    1 << 3
#define PT_JSON_ARRAY_START              1 << 4
#define PT_JSON_ARRAY_END                1 << 5
#define PT_JSON_LAST_ELEMENT             1 << 6

#define PT_JSON_KEY_VALUE               (PT_JSON_KEY | PT_JSON_VALUE)
#define PT_JSON_NAMED_OBJECT_START      (PT_JSON_KEY | PT_JSON_OBJECT_START)
#define PT_JSON_NAMED_ARRAY_START       (PT_JSON_KEY | PT_JSON_ARRAY_START)

/* JSON functions */
char	   *construct_json_block(char *buf, size_t buf_size, char *key, char *raw_value, int flags, int *json_file_indent);
FILE	   *open_telemetry_file(char *filename, char *mode);
void		write_telemetry_file(FILE *fp, char *data);

#endif							/* __PG_PT_JSON_H__ */
