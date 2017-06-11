#ifndef _H_VACDB
#define _H_VACDB 1
#include <time.h>

typedef struct {
	char *id;
	time_t report_date;
	time_t banned_date;
} vacdb_entry_t;

typedef struct {
	vacdb_entry_t **table;
	int size;
	int length;
} vacdb_t;

vacdb_t *vacdb_init(void);
int vacdb_load(vacdb_t * db, char *dbfile);
vacdb_entry_t *vacdb_get_by_id(vacdb_t * db, char *id);
int vacdb_add(vacdb_t * db, vacdb_entry_t * entry);
int vacdb_write(vacdb_t * db, char *dbfile);
void vacdb_free(vacdb_t * db);

#endif
