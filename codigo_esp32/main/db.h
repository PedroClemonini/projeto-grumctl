#ifndef DB_H
#define DB_H

#include <stdbool.h>

bool db_init(void);
bool db_insert_record(float lim, float peso, const char *status);
char *db_get_history_json(int limit);

#endif
