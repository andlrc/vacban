#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>

#include "vacban.h"
#include "vacdb.h"

static void print_version(void)
{
	printf("%s %s\n", PROGRAM_NAME, PROGRAM_VERSION);
}

static void print_usage(void)
{
	fprintf(stderr, "Usage: %s [OPTION]\n", PROGRAM_NAME);
}

static void print_help(void)
{
	printf("Usage: %s [OPTION]\n"
	       "  -a, --add      Add ID to database\n"
	       "  -b, --banned   Show list of banned accounts\n"
	       "  -d, --dbfile   Database file in use. ``~/.vacbandb'' is used if blank\n"
	       "  -u, --update   Update database with banned information\n"
	       "                   Use twice to recheck already banned accounts\n"
	       "  -h, --help     Show this help and exit\n"
	       "  -V, --version  Output version information\n",
	       PROGRAM_NAME);
}

static struct option const long_options[] = {
	{"version", no_argument, NULL, 'V'},
	{"help", no_argument, NULL, 'h'},
	{"add", required_argument, NULL, 'a'},
	{"banned", no_argument, NULL, 'b'},
	{"dbfile", required_argument, NULL, 'd'},
	{"update", no_argument, NULL, 'u'},
	{NULL, 0, NULL, 0}
};

static char short_options[] = "a:bd:uhV";

struct curl_buff_s {
	char *buffer;
	size_t size;
};

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *data)
{
	size_t realsize = size * nmemb;

	struct curl_buff_s *mem = (struct curl_buff_s *) data;

	mem->buffer = realloc(mem->buffer, mem->size + realsize + 1);

	if (mem->buffer) {
		memcpy(&(mem->buffer[mem->size]), ptr, realsize);
		mem->size += realsize;
		mem->buffer[mem->size] = 0;
	}
	return realsize;
}

static int steam_id(char *dest, size_t size, char *pos_id)
{
	/* Copy out everything from last slash and forward unless a trailing
	 * slash is present:
	 * http://steamcommunity.com/profiles/NNNNNN -> NNNNNN
	 * http://steamcommunity.com/profiles/NNNNNN/ -> NNNNNN
	 * http://steamcommunity.com/profiles/NNNNNN/ -> NNNNNN
	 * http://steamcommunity.com/id/XXXXXX/ -> XXXXXX
	 * XXXXXX -> XXXXXX */

	char *ls, *ns;

	if (!*pos_id) {
		return 1;
	}

	for (ns = pos_id; *ns ; ns++) {
		ls = ns;
		if (!(ns = strchr(ns, '/')))
			break;
	}

	while (*ls && *ls != '/' && --size)
		*dest++ = *ls++;
	*dest = '\0';

	return 0;
}

static int add(vacdb_t *db, char *pos_id)
{
	vacdb_entry_t entry, *pentry;
	char cdate[11]; /* YYYY-MM-DD\0 */
	char id[128];
	struct tm *tm;

	steam_id(id, sizeof(id), pos_id);

	if ((pentry = vacdb_get_by_id(db, id))) {
		tm = localtime(&pentry->report_date);
		strftime(cdate, sizeof(cdate), "%F", tm);
		fprintf(stderr, "%s: '%s' was already added %s\n",
				PROGRAM_NAME, pentry->id, cdate);
		return 1;
	}

	entry.id = id;
	time(&entry.report_date);
	entry.banned_date = 0;

	return vacdb_add(db, &entry);
}

static int is_banned(char *url)
{
	CURL *myHandle;
	CURLcode rc;
	struct curl_buff_s output;
	int banned = 0;

	output.buffer = 0;
	output.size = 0;

	myHandle = curl_easy_init();
	curl_easy_setopt(myHandle, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt(myHandle, CURLOPT_WRITEDATA, (void *) &output);
	curl_easy_setopt(myHandle, CURLOPT_URL, url);
	rc = curl_easy_perform(myHandle);
	curl_easy_cleanup(myHandle);

	if (!rc && output.buffer) {
		banned = strstr(output.buffer, "profile_ban_info") != 0;
	}

	free(output.buffer);

	return banned;
}

static int child_is_banned(vacdb_entry_t *entry, int fd[2], pid_t pid)
{
	char cban[2];

	if (pid == 0)
		return 0;

	waitpid(pid, NULL, 0);

	if (read(fd[0], cban, sizeof(cban)) == -1) {
		perror("read");
		return EXIT_FAILURE;
	}
	if (*cban == '1') {
		if (!entry->banned_date)
			time(&entry->banned_date);
	} else {
		entry->banned_date = 0;
	}

	return 0;
}

static int update(vacdb_t *db, int check_banned)
{
#define FORK_MAX 8
	vacdb_entry_t *entries[FORK_MAX];
	char *accid, url[512];
	int i, forkix = 0, fds[FORK_MAX][2];
	pid_t pids[FORK_MAX] = {0};

	curl_global_init(CURL_GLOBAL_ALL);

	for (i = 0; i < db->length; i++) {
		if (db->table[i]->banned_date && !check_banned)
			continue;

		/* The slot is already beeing used, wait for it to be free */
		if (pids[forkix]) {
			if (child_is_banned(entries[forkix], fds[forkix],
					    pids[forkix]))
				return EXIT_FAILURE;
		}

		entries[forkix] = db->table[i];

		/* Integer ID's is requisted via ``/profiles'', while anything
		 * else uses ``/id'' */
		/* Remove all leading digits, if nothing is left we got an
		 * integer */
		for (accid = entries[forkix]->id; isdigit(*accid); accid++);
		if (*accid) {
			strcpy(url, "http://steamcommunity.com/id/");
		} else {
			strcpy(url, "http://steamcommunity.com/profiles/");
		}
		strcat(url, entries[forkix]->id);

		if (pipe(fds[forkix]) == -1) {
			perror("pipe");
			return EXIT_FAILURE;
		}
		if ((pids[forkix] = fork()) == -1) {
			perror("fork");
			return EXIT_FAILURE;
		}
		else if (pids[forkix] == 0) {
#ifdef VACBAN_DEBUG
			printf("%d - %d - %d - %s\n", forkix,
			       getpid(), getppid(),
			       entries[forkix]->id);
#endif
			close(fds[forkix][0]);
			if (write(fds[forkix][1],
				  is_banned(url) ? "1" : "0", 2) == -1) {
				perror("write");
				exit(1);
			}
			exit(0);
		}
		else {
			close(fds[forkix][1]);
		}

		forkix = (forkix + 1) % FORK_MAX;
	}

	for (i = 0; i < FORK_MAX; i++) {
		if (child_is_banned(entries[i], fds[i], pids[forkix]))
			return EXIT_FAILURE;
	}

	return 0;
}

static int show_banned(vacdb_t *db, int show_all)
{
	vacdb_entry_t *entry;
	char cdate[11]; /* YYYY-MM-DD\0 */
	struct tm *tm;
	int i;

	for (i = 0; i < db->length; i++) {
		entry = db->table[i];
		if (entry->banned_date) {
			tm = localtime(&entry->report_date);
			strftime(cdate, sizeof(cdate), "%F", tm);
			printf("%s was banned %s\n", entry->id, cdate);
		} else if (show_all) {
			printf("%s is clean\n", entry->id);
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	vacdb_t *db = 0;
	struct passwd *pw;
	int c, i, addlen = 0, sflag = 0, uflag = 0;
	char *addarr[32],
	     *dbfile = 0,
	     dbfiledft[PATH_MAX] = "";

	while ((c = getopt_long(argc, argv, short_options, long_options,
				NULL)) != -1) {
		switch (c) {
		case 'V':
			print_version();
			return EXIT_SUCCESS;
			break;
		case 'h':
			print_help();
			return EXIT_SUCCESS;
			break;
		case 'a':
			addarr[addlen++] = optarg;
			break;
		case 'b':
			sflag++;
			break;
		case 'd':
			dbfile = optarg;
			break;
		case 'u':
			uflag++;
			break;
		default:
			print_usage();
			return EXIT_FAILURE;
			break;
		}
	}

	db = vacdb_init();

	if (!dbfile) {
		pw = getpwuid(getuid());
		strcat(dbfiledft, pw->pw_dir);
		strcat(dbfiledft, "/.vacbandb");
		dbfile = dbfiledft;

		/* Don't complain if the default database file doesn't exists */
		if (access(dbfile, F_OK) != -1)
			vacdb_load(db, dbfile);
	} else {
		vacdb_load(db, dbfile);
	}

	for (i = 0; i < addlen; i++) {
		add(db, addarr[i]);
	}

	if (uflag)
		update(db, uflag >= 2);

	if (sflag)
		show_banned(db, sflag >= 2);

	/* Flags that possible changed the database */
	if (addlen || uflag)
		vacdb_write(db, dbfile);

	vacdb_free(db);
}
