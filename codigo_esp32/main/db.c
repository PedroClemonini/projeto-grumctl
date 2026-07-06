#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "sqlite3.h"
#include "db.h"
#include <stdio.h>
static const char *TAG = "db";
static sqlite3 *s_db = NULL;
static SemaphoreHandle_t s_db_mutex;

#define DB_PATH "/spiffs/pesagens.db"

static bool spiffs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao montar LittleFS (%s)", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    esp_littlefs_info(conf.partition_label, &total, &used);
    ESP_LOGI(TAG, "LittleFS montado. total=%d used=%d", (int)total, (int)used);
    return true;
}
static void sqlite_error_log_callback(void *pArg, int iErrCode, const char *zMsg)
{
    ESP_LOGE("sqlite_internal", "(%d) %s", iErrCode, zMsg);
}
bool db_init(void)
{
    s_db_mutex = xSemaphoreCreateMutex();

    sqlite3_config(SQLITE_CONFIG_LOG, sqlite_error_log_callback, NULL);

    int rc_init = sqlite3_initialize();
    if (rc_init != SQLITE_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar SQLite (codigo %d)", rc_init);
        return false;
    }

    if (!spiffs_init()) {
        return false;
    }

    // -------------------------------------------------------------------------------

    int rc = sqlite3_open(DB_PATH, &s_db);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao abrir banco: %s", sqlite3_errmsg(s_db));
        return false;
    }

    sqlite3_extended_result_codes(s_db, 1);

    char *errmsg = NULL;

    rc = sqlite3_exec(s_db, "PRAGMA page_size = 512;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao definir page_size: %s (codigo estendido: %d)", errmsg, sqlite3_extended_errcode(s_db));
        sqlite3_free(errmsg);
        return false;
    }
    rc = sqlite3_exec(s_db, "PRAGMA journal_mode = OFF;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao desligar journal: %s (codigo estendido: %d)", errmsg, sqlite3_extended_errcode(s_db));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(s_db, "PRAGMA synchronous = OFF;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao desligar synchronous: %s (codigo estendido: %d)", errmsg, sqlite3_extended_errcode(s_db));
        sqlite3_free(errmsg);
        return false;
    }

    rc = sqlite3_exec(s_db, "PRAGMA locking_mode = EXCLUSIVE;", NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao definir locking_mode: %s (codigo estendido: %d)", errmsg, sqlite3_extended_errcode(s_db));
        sqlite3_free(errmsg);
        return false;
    }

    const char *sql_create =
        "CREATE TABLE IF NOT EXISTS pesagens ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "lim REAL,"
        "peso REAL,"
        "status TEXT,"
        "tempo_s INTEGER"
        ");";

    rc = sqlite3_exec(s_db, sql_create, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "Erro ao criar tabela: %s (codigo estendido: %d)", errmsg, sqlite3_extended_errcode(s_db));
        sqlite3_free(errmsg);
        return false;
    }

    ESP_LOGI(TAG, "Banco de dados pronto em %s", DB_PATH);
    return true;
}bool db_insert_record(float lim, float peso, const char *status)
{
    if (s_db == NULL) return false;

    bool ok = false;
    if (xSemaphoreTake(s_db_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        int64_t tempo_s = esp_timer_get_time() / 1000000; // segundos desde o boot

        sqlite3_stmt *stmt;
        const char *sql = "INSERT INTO pesagens (lim, peso, status, tempo_s) VALUES (?, ?, ?, ?);";

        if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_double(stmt, 1, lim);
            sqlite3_bind_double(stmt, 2, peso);
            sqlite3_bind_text(stmt, 3, status, -1, SQLITE_STATIC);
            sqlite3_bind_int64(stmt, 4, tempo_s);

            if (sqlite3_step(stmt) == SQLITE_DONE) {
                ok = true;
            } else {
                ESP_LOGE(TAG, "Erro ao inserir: %s", sqlite3_errmsg(s_db));
            }
            sqlite3_finalize(stmt);
        } else {
            ESP_LOGE(TAG, "Erro ao preparar insert: %s", sqlite3_errmsg(s_db));
        }

        xSemaphoreGive(s_db_mutex);
    }
    return ok;
}

char *db_get_history_json(int limit)
{
    if (s_db == NULL) return strdup("[]");

    size_t cap = 4096;
    char *json = malloc(cap);
    size_t len = 0;
    json[0] = '\0';
    strcat(json, "[");
    len = 1;

    if (xSemaphoreTake(s_db_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        sqlite3_stmt *stmt;
        const char *sql = "SELECT id, lim, peso, status, tempo_s FROM pesagens ORDER BY id DESC LIMIT ?;";

        if (sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);

            bool first = true;
            char row[160];

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int id          = sqlite3_column_int(stmt, 0);
                double lim      = sqlite3_column_double(stmt, 1);
                double peso     = sqlite3_column_double(stmt, 2);
                const unsigned char *status = sqlite3_column_text(stmt, 3);
                int tempo_s     = sqlite3_column_int(stmt, 4);

                int n = snprintf(row, sizeof(row),
                    "%s{\"id\":%d,\"lim\":%.1f,\"peso\":%.1f,\"status\":\"%s\",\"tempo_s\":%d}",
                    first ? "" : ",", id, lim, peso, status, tempo_s);
                first = false;

                if (len + n + 2 > cap) {
                    cap *= 2;
                    json = realloc(json, cap);
                }
                strcat(json, row);
                len += n;
            }
            sqlite3_finalize(stmt);
        }
        xSemaphoreGive(s_db_mutex);
    }

    strcat(json, "]");
    return json;
}
