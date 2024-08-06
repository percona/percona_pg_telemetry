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
#define PT_INDENT_SIZE      2 + (5 * 2)
#define PT_FORMAT_JSON(dest, dest_size, str, indent_size)       \
            pg_snprintf(dest, dest_size, "%*s%s\n",             \
                        (indent_size) * PT_INDENT_SIZE, "", str)

#define PT_JSON_BLOCK_START             1
#define PT_JSON_BLOCK_END               1 << 1
#define PT_JSON_BLOCK_LAST              1 << 3
#define PT_JSON_BLOCK_KEY               1 << 4
#define PT_JSON_BLOCK_VALUE             1 << 5
#define PT_JSON_ARRAY_START             1 << 6
#define PT_JSON_ARRAY_END               1 << 7
#define PT_JSON_KEY_VALUE_PAIR          1 << 8

#define PT_JSON_BLOCK_EMPTY             (PT_JSON_BLOCK_START | PT_JSON_BLOCK_END)
#define PT_JSON_BLOCK_SIMPLE            (PT_JSON_BLOCK_EMPTY | PT_JSON_BLOCK_KEY | PT_JSON_BLOCK_VALUE)
#define PT_JSON_BLOCK_ARRAY_VALUE       (PT_JSON_BLOCK_START | PT_JSON_BLOCK_KEY | PT_JSON_ARRAY_START)

/* JSON Hardcoded keys and values */
#define PT_JSON_KEY_SETTING             "setting"
#define PT_JSON_KEY_SETTINGS            "settings"
#define PT_JSON_KEY_DATABASE            "database"
#define PT_JSON_KEY_DATABASE_OID        "database_oid"
#define PT_JSON_KEY_DATABASE_SIZE       "database_size"
#define PT_JSON_KEY_DATABASES           "databases"
#define PT_JSON_KEY_ACTIVE_EXTENSIONS   "active_extensions"
#define PT_JSON_VALUE                   "value"

/* JSON functions */
bool json_state_init(void);
bool json_state_validate(void);
char *construct_json_block(char *msg_block, size_t msg_block_sz, char *key, char *raw_value, int flags, int *json_file_indent);
FILE *json_file_open(char *pathname, char *mode);
void write_json_to_file(FILE *fp, char *json_str);

#endif  /* __PG_PT_JSON_H__ */
