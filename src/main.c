/* ------------------------------------------------------------------
 * Session Utility - Main Source File
 * ------------------------------------------------------------------ */

#include "sessutil.h"

/**
 * Check if domain matches pattern
 */
static int filter_check ( const char *domain, const char *pattern )
{
    size_t len;
    int starts_with_asterisk;
    int ends_with_asterisk;
    char phrase[BUFSIZE];

    /* Only wildcard passed every domain */
    if ( pattern[0] == '*' && !pattern[1] )
    {
        return 1;
    }

    /* Setup flags */
    len = strlen ( pattern );
    starts_with_asterisk = pattern[0] == '*';
    ends_with_asterisk = pattern[len - 1] == '*';

    /* Validate phrase size */
    if ( len < ( size_t ) ( starts_with_asterisk + ends_with_asterisk )
        || len >= sizeof ( phrase ) )
    {
        return 0;
    }

    /* Prepare phrase string */
    memcpy ( phrase, pattern, len );
    phrase[len] = '\0';

    /* Remove first character if needed */
    if ( starts_with_asterisk )
    {
        memmove ( phrase, phrase + 1, len - 1 );
        len--;
        phrase[len] = '\0';
    }

    /* Remove last character if needed */
    if ( ends_with_asterisk )
    {
        len--;
        phrase[len] = 0;
    }

    /* Wildcard at the beginning */
    if ( starts_with_asterisk )
    {
        /* Wildcard at the beginning and at the end */
        if ( ends_with_asterisk )
        {
            return !!strstr ( domain, phrase );
        }

        /* Wildcard only at the beginning */
        return strstr ( domain, phrase ) == &domain[strlen ( domain )] - len;
    }

    /* Wildcard only at the end */
    if ( ends_with_asterisk )
    {
        return strstr ( domain, phrase ) == domain;
    }

    /* Extact match */
    return !strcmp ( domain, phrase );
}

/**
 * Check if string starts with another
 */
static int strstarts ( const char *a, const char *b )
{
    size_t i;
    size_t len;

    for ( i = 0, len = strlen ( b ); i < len; i++ )
    {
        if ( a[i] != b[i] )
        {
            return 0;
        }
    }

    return 1;
}

/**
 * Parse entry string parameter
 */
static int parse_entry_string ( const char *plain, size_t *posref, const char *name, char *value,
    size_t size )
{
    size_t len;
    size_t pos = *posref;
    size_t endpos;
    char phrase[BUFSIZE];
    const char *newline;

    /* Format search phrase */
    snprintf ( phrase, sizeof ( phrase ), "%s: ", name );

    /* Check if string starts with phrase */
    if ( !strstarts ( plain + pos, phrase ) )
    {
        return 0;
    }

    /* Skip search phrase */
    pos += strlen ( phrase );

    /* Lookup for line end */
    if ( !( newline = strstr ( plain + pos, "\n" ) ) )
    {
        return 0;
    }

    /* Get tag end offset */
    endpos = newline - plain;

    /* Validate buffer size */
    if ( ( len = endpos - pos ) >= size )
    {
        return 0;
    }

    /* Place value string */
    memcpy ( value, plain + pos, len );
    value[len] = '\0';

    /* Update scan position */
    *posref = endpos + 1;

    return 1;
}

/**
 * Parse entry long integer parameter
 */
static int parse_entry_long ( const char *plain, size_t *posref, const char *name,
    long long int *value )
{
    char buffer[BUFSIZE];

    /* Scan as string parameter */
    if ( !parse_entry_string ( plain, posref, name, buffer, sizeof ( buffer ) ) )
    {
        return 0;
    }

    /* Parse integer value */
    if ( sscanf ( buffer, "%lli", value ) )
    {
        value = 0;
    }

    return 1;
}

/**
 * Parse entry boolean parameter
 */
static int parse_entry_boolean ( const char *plain, size_t *posref, const char *name, int *value )
{
    char buffer[BUFSIZE];

    /* Scan as string parameter */
    if ( !parse_entry_string ( plain, posref, name, buffer, sizeof ( buffer ) ) )
    {
        return 0;
    }

    /* Parse boolean value */
    *value = !strcmp ( buffer, "yes" );

    return 1;
}

/**
 * Parse single cookie entry
 */
static int parse_entry ( const char *plain, size_t *posref, struct entry_mem_t *entry )
{
    size_t pos = *posref;
    char domain[LBUFSIZE];

    /* Reset cookie entry */
    memset ( entry, '\0', sizeof ( struct entry_ref_t ) );

    /* Skip empty lines */
    while ( plain[pos] == '\n' )
    {
        pos++;
    }

    /* Stop on EOF */
    if ( !plain[pos] )
    {
        *posref = pos;
        return 0;
    }

    /* Validate begin tag */
    if ( plain[pos++] != '[' || plain[pos++] != '\n' )
    {
        errno = EINVAL;
        return -1;
    }

    /* Parse parameters until end tag */
    while ( plain[pos] != ']' || ( plain[pos + 1] != '\n' && plain[pos + 1] ) )
    {
        while ( isblank ( plain[pos] ) )
        {
            pos++;
        }
        if ( !( parse_entry_string ( plain, &pos, P_DOMAIN, domain, sizeof ( domain ) )
                || parse_entry_string ( plain, &pos, P_OATTR, entry->oattr,
                    sizeof ( entry->oattr ) )
                || parse_entry_string ( plain, &pos, P_NAME, entry->name, sizeof ( entry->name ) )
                || parse_entry_string ( plain, &pos, P_VALUE, entry->value,
                    sizeof ( entry->value ) )
                || parse_entry_string ( plain, &pos, P_HOST, entry->host, sizeof ( entry->host ) )
                || parse_entry_string ( plain, &pos, P_PATH, entry->path, sizeof ( entry->path ) )
                || parse_entry_long ( plain, &pos, P_EXPIRY, &entry->expiry )
                || parse_entry_boolean ( plain, &pos, P_SECURE, &entry->secure )
                || parse_entry_boolean ( plain, &pos, P_HTTP, &entry->http ) ) )
        {
            errno = EINVAL;
            return -1;
        }
    }

    *posref = pos + 2;
    return 1;
}

/**
 * Import plain file to database
 */
static int import_task ( sqlite3 * db, int fd, const char *filter, struct dbstats_t *stats )
{
    int status;
    int id = 1;
    size_t size;
    size_t pos;
    char *plain;
    char *err_msg = NULL;
    struct entry_mem_t entry;
    sqlite3_stmt *stmt;

    /* Get plain file size */
    if ( ( off_t ) ( size = lseek ( fd, 0, SEEK_END ) ) < 0 || lseek ( fd, 0, SEEK_SET ) < 0 )
    {
        perror ( "lseek" );
        return -1;
    }

    /* Allocate plain text buffer */
    if ( !( plain = ( char * ) malloc ( size + 1 ) ) )
    {
        perror ( "malloc" );
        return -1;
    }

    /* Read plain text from file */
    if ( ( ssize_t ) ( pos = read ( fd, plain, size ) ) < 0 )
    {
        perror ( "read" );
        free ( plain );
        return -1;
    }

    /* Put strign terminator */
    plain[pos] = '\0';

    /* Reset scan position */
    pos = 0;

    /* Truncate cookies table */
    if ( sqlite3_exec ( db, "DELETE FROM " COOKIES_TABLE ";", NULL, NULL, &err_msg ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to empty cookies: %s\n", err_msg );
        sqlite3_free ( err_msg );
        free ( plain );
        return -1;
    }

    /* Begin SQL transaction */
    if ( sqlite3_exec ( db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to begin transaction: %s\n", err_msg );
        sqlite3_free ( err_msg );
        free ( plain );
        return -1;
    }

    /* Prepare SQL statement */
    if ( sqlite3_prepare_v2 ( db,
            "INSERT INTO " COOKIES_TABLE " (" S_ID "," S_OATTR "," S_NAME "," S_VALUE
            "," S_HOST "," S_PATH "," S_EXPIRY "," S_SECURE "," S_HTTP
            ") VALUES (?,?,?,?,?,?,?,?,?);", -1, &stmt, NULL ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to prepare statement: %s\n", err_msg );
        sqlite3_free ( err_msg );
        free ( plain );
        return -1;
    }

    /* Put data into database */
    while ( pos + 1 < size && ( status = parse_entry ( plain, &pos, &entry ) ) == 1 )
    {
        /* Check for obligatory values */
        if ( !entry.name[0] || !entry.value[0] || !entry.host[0] || !entry.path[0] )
        {
            stats->failed++;
            continue;
        }

        /* Filter entires by domain */
        if ( filter && !filter_check ( entry.host, filter ) )
        {
            continue;
        }

        /* Reset SQL statement. */
        sqlite3_reset ( stmt );

        /* Bind values parameters. */
        if ( sqlite3_bind_int ( stmt, 1, id ) != SQLITE_OK
            || sqlite3_bind_text ( stmt, 2, entry.oattr, -1, SQLITE_STATIC ) != SQLITE_OK
            || sqlite3_bind_text ( stmt, 3, entry.name, -1, SQLITE_STATIC ) != SQLITE_OK
            || sqlite3_bind_text ( stmt, 4, entry.value, -1, SQLITE_STATIC ) != SQLITE_OK
            || sqlite3_bind_text ( stmt, 5, entry.host, -1, SQLITE_STATIC ) != SQLITE_OK
            || sqlite3_bind_text ( stmt, 6, entry.path, -1, SQLITE_STATIC ) != SQLITE_OK
            || sqlite3_bind_int64 ( stmt, 7, entry.expiry ) != SQLITE_OK
            || sqlite3_bind_int ( stmt, 8, entry.secure ) != SQLITE_OK
            || sqlite3_bind_int ( stmt, 9, entry.http ) != SQLITE_OK )
        {
            stats->failed++;
            continue;
        }

        /* Perform SQL query below */
        if ( sqlite3_step ( stmt ) != SQLITE_DONE )
        {
            stats->failed++;
            continue;
        }

        id++;
        stats->passed++;
    }

    /* Free plain text buffer */
    free ( plain );

    /* Analyse error status */
    if ( status < 0 )
    {
        perror ( "parse" );
        sqlite3_free ( err_msg );
        return -1;
    }

    /* Commit current transaction. */
    if ( sqlite3_exec ( db, "COMMIT;", NULL, NULL, &err_msg ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to commit transaction: %s\n", err_msg );
        sqlite3_free ( err_msg );
        return -1;
    }

    return 0;
}

/**
 * Put cookie entry to plain file
 */
static int put_entry ( int fd, const struct entry_ref_t *entry )
{
    char plain[8192];

    /* Format plain text */
    snprintf ( plain, sizeof ( plain ),
        "[\n"
        S_INDENT P_OATTR ": %s\n"
        S_INDENT P_NAME ": %s\n"
        S_INDENT P_VALUE ": %s\n"
        S_INDENT P_HOST ": %s\n"
        S_INDENT P_PATH ": %s\n"
        S_INDENT P_EXPIRY ": %lli\n"
        S_INDENT P_SECURE ": %s\n"
        S_INDENT P_HTTP ": %s\n"
        "]\n",
        entry->oattr, entry->name, entry->value, entry->host, entry->path,
        entry->expiry, YESNO ( entry->secure ), YESNO ( entry->http ) );

    /* Store plain text to file */
    if ( write ( fd, plain, strlen ( plain ) ) < 0 )
    {
        perror ( "write" );
        return -1;
    }

    return 0;
}

/* Export callback */
static int export_callback ( void *context, int argc, char **argv, char **azColName )
{
    int i;
    struct entry_ref_t entry;
    struct callback_ctx *ctx = ( struct callback_ctx * ) context;

    /* Reset entry */
    memset ( &entry, '\0', sizeof ( entry ) );

    /* Parse parameters */
    for ( i = 0; i < argc; i++ )
    {
        if ( !argv[i] )
        {
            continue;
        }

        if ( !strcmp ( azColName[i], S_OATTR ) )
        {
            entry.oattr = argv[i];

        } else if ( !strcmp ( azColName[i], S_NAME ) )
        {
            entry.name = argv[i];

        } else if ( !strcmp ( azColName[i], S_VALUE ) )
        {
            entry.value = argv[i];

        } else if ( !strcmp ( azColName[i], S_HOST ) )
        {
            entry.host = argv[i];

        } else if ( !strcmp ( azColName[i], S_PATH ) )
        {
            entry.path = argv[i];

        } else if ( !strcmp ( azColName[i], S_EXPIRY ) )
        {
            sscanf ( argv[i], "%lli", &entry.expiry );

        } else if ( !strcmp ( azColName[i], S_SECURE ) )
        {
            sscanf ( argv[i], "%i", &entry.secure );

        } else if ( !strcmp ( azColName[i], S_HTTP ) )
        {
            sscanf ( argv[i], "%i", &entry.http );
        }
    }

    /* Skip incomplete entries */
    if ( !entry.name || !entry.value || !entry.host || !entry.path )
    {
        ctx->stats->failed++;
        return 0;
    }

    /* Origin attributes are optional */
    if ( !entry.oattr )
    {
        entry.oattr = "";
    }

    /* Prevent values containing new line symbol */
    if ( strchr ( entry.oattr, '\n' )
        || strchr ( entry.name, '\n' )
        || strchr ( entry.value, '\n' )
        || strchr ( entry.host, '\n' ) || strchr ( entry.path, '\n' ) )
    {
        ctx->stats->failed++;
        return 0;
    }

    /* Filter entires by domain */
    if ( ctx->filter && !filter_check ( entry.host, ctx->filter ) )
    {
        return 0;
    }

    /* Put entry to file */
    if ( put_entry ( ctx->fd, &entry ) < 0 )
    {
        ctx->stats->failed++;
        return 0;
    }

    ctx->stats->passed++;
    return 0;
}

/**
 * Export database to plain file
 */
static int export_task ( sqlite3 * db, int fd, const char *filter, struct dbstats_t *stats )
{
    char *sql =
        "SELECT " S_OATTR "," S_NAME "," S_VALUE "," S_HOST "," S_PATH "," S_EXPIRY ","
        S_SECURE "," S_HTTP " FROM " COOKIES_TABLE ";";
    char *err_msg = NULL;
    struct callback_ctx ctx;

    /* Prepare callback context. */
    memset ( &ctx, '\0', sizeof ( ctx ) );
    ctx.fd = fd;
    ctx.filter = filter;
    ctx.stats = stats;

    /* Export database content */
    if ( sqlite3_exec ( db, sql, export_callback, &ctx, &err_msg ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to select cookies: %s\n", err_msg );
        sqlite3_free ( err_msg );
        return -1;
    }

    return 0;
}

/**
 * Truncate database
 */
static int truncate_task ( sqlite3 * db )
{
    char *sql = "DELETE FROM " COOKIES_TABLE ";";
    char *err_msg = NULL;

    /* Export database content */
    if ( sqlite3_exec ( db, sql, NULL, NULL, &err_msg ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to truncate cookies: %s\n", err_msg );
        sqlite3_free ( err_msg );
        return -1;
    }

    return 0;
}

/**
 * Get operation description
 */
const char *opstr ( int task )
{
    switch ( task )
    {
    case TASK_IMPORT:
        return "imported";
    case TASK_EXPORT:
        return "exported";
    case TASK_TRUNCATE:
        return "truncated";
    default:
        return NULL;
    }
}

/**
 * Show program usage message
 */
static void show_usage ( void )
{
    fprintf ( stderr, "sessutil - ver. " SESSUTIL_VERSION "\n\n"
        "usage: sessutil option [src] [dst] [filter]\n\n"
        "options:\n"
        "    -i      import database from plain file\n"
        "    -e      export database to plain file\n"
        "    -t      delete all cookies from database\n"
        "    -h      show help message\n\n"
        "arguments:\n"
        "    src     source file path\n"
        "    dst     destination file path\n" "    filter  transfer domain filter\n\n" );
}

/**
 * Program entry point
 */
int main ( int argc, char *argv[] )
{
    int status = 0;
    int task;
    sqlite3 *db;
    int fd = -1;
    const char *dbpath;
    const char *filepath;
    const char *filter;
    struct dbstats_t stats;

    /* Validate arguments count */
    if ( argc < 3 )
    {
        show_usage (  );
        return 1;
    }

    /* Analyse task option */
    if ( argv[1][0] == '-' && argv[1][1] == 'i' )
    {
        if ( argc < 4 )
        {
            show_usage (  );
            return 1;
        }

        task = TASK_IMPORT;
        dbpath = argv[3];
        filepath = argv[2];

    } else if ( argv[1][0] == '-' && argv[1][1] == 'e' )
    {
        if ( argc < 4 )
        {
            show_usage (  );
            return 1;
        }

        task = TASK_EXPORT;
        dbpath = argv[2];
        filepath = argv[3];

    } else if ( argv[1][0] == '-' && argv[1][1] == 't' )
    {
        task = TASK_TRUNCATE;
        dbpath = argv[2];

    } else
    {
        show_usage (  );
        return 1;
    }

    /* Filter can be NULL */
    if ( task == TASK_IMPORT || task == TASK_EXPORT )
    {
        filter = argv[4];
    }

    /* Open database file */
    if ( sqlite3_open ( dbpath, &db ) != SQLITE_OK )
    {
        fprintf ( stderr, "failed to access database: %s\n", sqlite3_errmsg ( db ) );
        sqlite3_close ( db );
        return 1;
    }

    /* Open data file */
    if ( task == TASK_IMPORT )
    {
        fd = open ( filepath, O_RDONLY );

    } else if ( task == TASK_EXPORT )
    {
        fd = open ( filepath, O_CREAT | O_WRONLY | O_TRUNC, 0644 );
    }

    /* Check for file open error */
    if ( task == TASK_IMPORT || task == TASK_EXPORT )
    {
        if ( fd < 0 )
        {
            perror ( filepath );
            sqlite3_close ( db );
            return 1;
        }

        /* Reset task statistics */
        memset ( &stats, '\0', sizeof ( stats ) );
    }

    /* Branch to the task */
    if ( task == TASK_IMPORT )
    {
        status = import_task ( db, fd, filter, &stats );

    } else if ( task == TASK_EXPORT )
    {
        status = export_task ( db, fd, filter, &stats );

    } else if ( task == TASK_TRUNCATE )
    {
        status = truncate_task ( db );
    }

    /* Print task statistics */
    if ( task == TASK_IMPORT || task == TASK_EXPORT )
    {
        if ( !status )
        {
            printf ( "passed records: %i\n", stats.passed );
            printf ( "failed records: %i\n", stats.failed );
        }
    }

    /* Free resources */
    sqlite3_close ( db );
    if ( fd >= 0 )
    {
        close ( fd );
    }

    /* Analyse error status */
    if ( status < 0 )
    {
        printf ( "failure (%i)\n", errno );
        return 1;
    }

    /* Format summary */
    printf ( "database %s.\n", opstr ( task ) );
    return 0;
}
