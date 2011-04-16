#include "sqlite.h"
#include "../common/config.h"

// the database handler
sqlite3 *db;


int 
exec_query(char *query)
{

    sqlite3_stmt  *stmt;
    
    int ret;
#ifdef DEBUG_SQLITE
    printf("sql: %s\n", query);
#endif

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("sqlite3_prepare_v2() error\n");
    }

    ret = sqlite3_step(stmt);

    // database is busy, retry query
    if (ret == SQLITE_BUSY)
    {
        // wait for 0.5 sec
        usleep(500000);

#ifdef DEBUG_SQLITE
        printf("retrying query...\n");

        return exec_query(query);
        printf("SUCCESSFULLY RETRIED!\n");

        return 0;
#else
        return exec_query(query);
#endif

    }

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
    }


// we need to retry, so this is not enough
/*
    if (sqlite3_exec(db, query, 0, 0, 0) != SQLITE_OK)
    {
        printf("couldn't exec query: '%s\n", query);
        perror("error");
    }
*/
    return 0;
}


int
create_table(char *name)
{
    char query[1024];

    snprintf(query, sizeof(query), 
        "CREATE TABLE IF NOT EXISTS %s ( \
             id    INTEGER PRIMARY KEY, \
             time  DATE, \
             value FLOAT \
         )", name);

    return exec_query(query);
}

int
insert_value(char *name, float value)
{
    char          query[1024];

    snprintf(query, sizeof(query),
        "INSERT INTO %s VALUES ( NULL, DATETIME('NOW'), %f )",
        name, value);

    return exec_query(query);
}

float
get_value(char *table)
{
    return get_row("value", table);
}

float
get_row(char *row, char *table)
{
    char          query[1024];
    float         value;
    sqlite3_stmt  *res;

    snprintf(query, sizeof(query),
            "SELECT %s FROM %s ORDER BY id DESC LIMIT 1",
            row, table);

#ifdef DEBUG_SQLITE
    printf("sql: %s\n", query);
#endif

    if (sqlite3_prepare_v2(db, query, strlen(query), &res, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        perror("error");
        return -1;
    }

    if (sqlite3_step(res) == SQLITE_ROW)
    {
        if (EOF == sscanf( (const char * __restrict__) sqlite3_column_text(res, 0), "%f", &value) )
            value = -1;
    }
    else
        value = -1;

    sqlite3_finalize(res);

    return value;
}

float
get_average(char *row, char *table, int time)
{
    char          query[1024];
    float         average;
    sqlite3_stmt  *res;

    if (time > 0)
    {
        snprintf(query, sizeof(query),
            "SELECT AVG (%s) FROM %s WHERE time > DATETIME('NOW', '-%d minutes')",
            row, table, time);
    }
    else
    {
        snprintf(query, sizeof(query),
            "SELECT AVG (%s) FROM %s",
            row, table);
    }

#ifdef DEBUG_SQLITE
    printf("sql: %s\n", query);
#endif

    if (sqlite3_prepare_v2(db, query, strlen(query), &res, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        perror("error");
        return -1;
    }

    if (sqlite3_step(res) == SQLITE_ROW)
    {
        if (EOF == sscanf( (const char * __restrict__) sqlite3_column_text(res, 0), "%f", &average) )
            average = -1;
    }
    else
        average = -1;

    sqlite3_finalize(res);

    return average;
}

int
calc_consumption(void)
{
    char  query[1024];
    float per_h;
    float per_km;
    float speed;

    // calculate consumption per hour
    per_h = 60 * 4 * MULTIPLIER * get_value("rpm") * (get_value("injection_time") - INJ_SUBTRACT);

    // calculate consumption per hour
    if ( (speed = get_value("speed")) > 0)    
        per_km = per_h / speed * 100;
    else
        per_km = -1;

    snprintf(query, sizeof(query),
        "INSERT INTO consumption VALUES ( \
             NULL, DATETIME('NOW'), %f, %f )",
        per_h, per_km);

    return exec_query(query);
}

int
init_db(void)
{
    // open database file
    if (sqlite3_open(DB_RAM, &db))
    {
        printf("Can not open database: %s", DB_RAM);
        return -1;
    }
  
    // create tables (if not existent) 
    create_table("speed"); 
    create_table("rpm");
    create_table("temp_engine");
    create_table("temp_air_intake");
    create_table("oil_pressure");
    create_table("injection_time");
    create_table("voltage");
    create_table("tank_content");

    // create consumption table
    exec_query("CREATE TABLE IF NOT EXISTS consumption ( \
                    id     INTEGER PRIMARY KEY, \
                    time   DATE, \
                    per_h  FLOAT, \
                    per_km FLOAT \
                )");

    return 0;
}

void
close_db(void)
{
    sqlite3_close(db);
}


// save the db to disk once in a while
void
save_db(void)
{
    int status;

    if (fork() > 0)
    {
        printf("syncing db file to disk...");
        execlp("rsync", "rsync", "-a", DB_RAM, DB_DISK, NULL);
    }
    while(waitpid(-1, &status, WNOHANG) > 0);
}