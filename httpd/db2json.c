#include "httpd.h"

int
json_get_engine_data(sqlite3 *db, json_object *data)
{
    char          query[LEN_QUERY];
    sqlite3_stmt  *stmt;

    // query engine data
    snprintf(query, sizeof(query),
             "SELECT rpm, speed, injection_time, \
             oil_pressure, consumption_per_100km, consumption_per_h \
             FROM engine_data \
             ORDER BY id \
             DESC LIMIT 1");


    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        add_double(data, "rpm", sqlite3_column_double(stmt, 0));
        add_double(data, "speed", sqlite3_column_double(stmt, 1));
        add_double(data, "injection_time", sqlite3_column_double(stmt, 2));
        add_double(data, "oil_pressure", sqlite3_column_double(stmt, 3));
        add_double(data, "consumption_per_100km", sqlite3_column_double(stmt, 4));
        add_double(data, "consumption_per_h", sqlite3_column_double(stmt, 5));
    }

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
        return -1;
    }

    return 0;
}


int
json_get_other_data(sqlite3 *db, json_object *data)
{
    char          query[LEN_QUERY];
    sqlite3_stmt  *stmt;

    // query other data
    snprintf(query, sizeof(query),
             "SELECT temp_engine, temp_air_intake, voltage \
             FROM other_data \
             ORDER BY id \
             DESC LIMIT 1");

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        add_double(data, "temp_engine", sqlite3_column_double(stmt, 0));
        add_double(data, "temp_air_intake", sqlite3_column_double(stmt, 1));
        add_double(data, "voltage", sqlite3_column_double(stmt, 2));
    }

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
        return -1;
    }

    return 0;
}


int
json_get_averages(sqlite3 *db, json_object *data)
{
    char          query[LEN_QUERY];
    sqlite3_stmt  *stmt;

    // average since last startup
    snprintf(query, sizeof(query),
             "SELECT SUM(speed*consumption_per_100km)/SUM(speed) \
             FROM engine_data \
             WHERE consumption_per_100km != -1 \
             AND id > ( SELECT engine_data FROM setpoints WHERE name = 'startup' )");

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
        add_double(data, "consumption_average_startup", sqlite3_column_double(stmt, 0));

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
        return -1;
    }

    // average since timespan
    /*
    snprintf(query, sizeof(query),
             "SELECT SUM(speed*consumption_per_100km)/SUM(speed) \
             FROM engine_data \
             WHERE time > DATETIME('NOW', '-%lu seconds', 'localtime') \
             AND consumption_per_100km != -1",
             timespan);
    */
    

    // overall consumption average
    snprintf(query, sizeof(query),
             "SELECT SUM(speed*consumption_per_100km)/SUM(speed) \
             FROM engine_data \
             WHERE consumption_per_100km != -1");

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return -1;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
        add_double(data, "consumption_average_total", sqlite3_column_double(stmt, 0));

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
        return -1;
    }

    return 0;
}


// get latest data from database
const char *
json_latest_data(sqlite3 *db)
{
    json_object *data = json_object_new_object();

    exec_query(db, "BEGIN TRANSACTION");

    json_get_engine_data(db, data);
    json_get_other_data(db, data);
    json_get_averages(db, data);

    exec_query(db, "END TRANSACTION");

    return json_object_to_json_string(data);
}


/*
 * get json data for graphing table key
 * getting all data since id index
 * but not older than timepsan seconds
 */
const char *
json_graph_data(sqlite3 *db, char *key, unsigned long int index, unsigned long int timespan)
{
    char          query[LEN_QUERY];
    sqlite3_stmt  *stmt;

    json_object *graph = json_object_new_object();
    json_object *data = add_array(graph, "data");

    exec_query(db, "BEGIN TRANSACTION");

    snprintf(query, sizeof(query),
             "SELECT id, strftime('%%s000', time), %s \
              FROM   engine_data \
              WHERE id > %lu \
              AND time > DATETIME('NOW', '-%lu seconds', 'localtime') \
              ORDER BY id",
             key, index, timespan);

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return NULL;
    }

    if (sqlite3_prepare_v2(db, query, strlen(query), &stmt, NULL) != SQLITE_OK)
    {
        printf("couldn't execute query: '%s'\n", query);
        return NULL;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        add_data(data, sqlite3_column_double(stmt, 1),
                       sqlite3_column_double(stmt, 2));

        index = sqlite3_column_int(stmt, 0);
    }

    if (sqlite3_finalize(stmt) != SQLITE_OK)
    {
        printf("sqlite3_finalize() error\n");
        return NULL;
    }

    exec_query(db, "END TRANSACTION");

    add_int(graph, "index", index);

    return json_object_to_json_string(graph);
}
