/* ------------------------------------------------------------------
 * Session Utility - Shared Project Header
 * ------------------------------------------------------------------ */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <sqlite3.h>

#ifndef SESSUTIL_H
#define SESSUTIL_H

#define SESSUTIL_VERSION "1.0.18"
#define S_ID "id"
#define S_DOMAIN "baseDomain"
#define S_OATTR "originAttributes"
#define S_NAME "name"
#define S_VALUE "value"
#define S_HOST "host"
#define S_PATH "path"
#define S_EXPIRY "expiry"
#define S_SECURE "isSecure"
#define S_HTTP "isHttpOnly"

#define S_INDENT "    "
#define P_DOMAIN "domain"
#define P_OATTR "oattr"
#define P_NAME "name"
#define P_VALUE "value"
#define P_HOST "host"
#define P_PATH "path"
#define P_EXPIRY "expiry"
#define P_SECURE "secure"
#define P_HTTP "http"

#define BUFSIZE 256
#define LBUFSIZE 4096
#define COOKIES_TABLE "moz_cookies"
#define YESNO(x) ((x)?"yes":"no")

#define TASK_IMPORT 1
#define TASK_EXPORT 2
#define TASK_TRUNCATE 3

/**
 * Database statistics
 */
struct dbstats_t
{
    int passed;
    int failed;
};

/**
 * Export callback context
 */
struct callback_ctx
{
    int fd;
    const char *filter;
    struct dbstats_t *stats;
};

/**
 * Cookie entry with string references
 */
struct entry_ref_t
{
    char *domain;
    char *oattr;
    char *name;
    char *value;
    char *host;
    char *path;
    long long int expiry;
    int secure;
    int http;
};

/**
 * Cookies entry with string buffer
 */
struct entry_mem_t
{
    char domain[LBUFSIZE];
    char oattr[LBUFSIZE];
    char name[LBUFSIZE];
    char value[LBUFSIZE];
    char host[LBUFSIZE];
    char path[LBUFSIZE];
    long long int expiry;
    int secure;
    int http;
};

#endif
