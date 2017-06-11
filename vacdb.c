#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "vacdb.h"

vacdb_t *vacdb_init(void)
{
	vacdb_t *db;

	if (!(db = malloc(sizeof(vacdb_t)))) {
		perror("malloc");
		return NULL;
	}

	db->size = 64;
	db->length = 0;

	if (!(db->table = malloc(sizeof(vacdb_entry_t *) * db->size))) {
		perror("malloc");
		free(db);
		return NULL;
	}

	return db;
}

int vacdb_load(vacdb_t *db, char *dbfile)
{
	char line[512], *pline;
	vacdb_entry_t entry;
	FILE *dbfp;

	if (!(dbfp = fopen(dbfile, "r"))) {
		perror(dbfile);
		return 1;
	}

	while (fgets(line, sizeof(line), dbfp)) {
		/* Line = id:yyyy-mm-dd:{0|1} */
		pline = line;
		entry.id = strsep(&pline, ":");
		entry.report_date = atol(strsep(&pline, ":"));
		entry.banned_date = atol(strsep(&pline, ":"));

		if (vacdb_add(db, &entry)) {
			fclose(dbfp);
			return 1;
		}
	}

	return 0;
}

vacdb_entry_t *vacdb_get_by_id(vacdb_t *db, char *id)
{
	int i = 0;
	for (i = 0; i < db->length; i++)
		if (strcmp(db->table[i]->id, id) == 0)
			return db->table[i];

	return NULL;
}

int vacdb_add(vacdb_t *db, vacdb_entry_t *data)
{
	vacdb_entry_t **ttmp, *entry;

	if (!(entry = malloc(sizeof(vacdb_entry_t)))) {
		perror("malloc");
		return 1;
	}

	entry->id = strdup(data->id);
	entry->report_date = data->report_date;
	entry->banned_date = data->banned_date;
	db->table[db->length++] = entry;

	if (db->length == db->size) {
		db->size *= 2;
		if (!(ttmp = realloc(db->table,
				     sizeof(vacdb_entry_t *) * db->size))) {
			free(entry);
			return 1;
		}
		else {
			db->table = ttmp;
		}
	}
	return 0;
}

int vacdb_write(vacdb_t *db, char *dbfile)
{
	int i = 0;
	FILE *dbfp;
	vacdb_entry_t *entry;

	if (!(dbfp = fopen(dbfile, "w"))) {
		perror(dbfile);
		return 1;
	}

	for (i = 0; i < db->length; i++) {
		entry = db->table[i];
		fprintf(dbfp, "%s:%zu:%zu\n", entry->id,
			entry->report_date, entry->banned_date);
	}

	fclose(dbfp);
	return 0;
}

void vacdb_free(vacdb_t *db)
{
	vacdb_entry_t *entry;
	int i = 0;

	for (i = 0; i < db->length; i++) {
		entry = db->table[i];
		free(entry->id);
		free(entry);
	}
	free(db->table);
	free(db);
}
