#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "db.h"
#include "driver/uart.h"
#include "esp_heap_caps.h"
/* ---------------- Config UART ---------------- */

#define UART_PORT_NUM      UART_NUM_2
#define UART_TX_PIN        19
#define UART_RX_PIN        18
#define UART_BAUD_RATE     9600
#define UART_BUF_SIZE      256

static QueueHandle_t s_uart_queue;


/* ---------------- Config Wi-Fi AP ---------------- */
#define WIFI_AP_SSID        "Balanca_ESP32"
#define WIFI_AP_PASS        "12345678"
#define WIFI_AP_CHANNEL     1
#define WIFI_AP_MAX_CONN    4

static const char *TAG = "app";
static QueueHandle_t s_receive_queue;

typedef struct {
    float lim;
    float peso;
    char  status[16];
} weighing_data_t;

static weighing_data_t s_data = { .lim = 0, .peso = 0, .status = "aguardando" };
static SemaphoreHandle_t s_data_mutex;
static void uart_rx_task(void *arg);
static void process_json_payload(const uint8_t *buffer, uint32_t length);
static void uart_init_custom(void);

static void uart_init_custom(void)
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, UART_BUF_SIZE * 2, 0, 20, &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 5, NULL);
}

static void uart_rx_task(void *arg)
{
    uint8_t byte;
    char line_buf[256];
    int line_pos = 0;

    while (1) {
        int len = uart_read_bytes(UART_PORT_NUM, &byte, 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            if (byte == '\n') {
                line_buf[line_pos] = '\0';
                process_json_payload((uint8_t *)line_buf, line_pos);
                line_pos = 0;
            } else if (line_pos < sizeof(line_buf) - 1) {
                line_buf[line_pos++] = byte;
            }
        }
    }
}

static void uart_send_led_command(bool ligar)
{
    const char *cmd = ligar ? "on\n" : "off\n";
    uart_write_bytes(UART_PORT_NUM, cmd, strlen(cmd));
    ESP_LOGI(TAG, "Comando LED enviado: %s", ligar ? "on" : "off");
}

static void process_json_payload(const uint8_t *buffer, uint32_t length)
{
    char json_str[256];
    uint32_t copy_len = (length < sizeof(json_str) - 1) ? length : sizeof(json_str) - 1;
    memcpy(json_str, buffer, copy_len);
    json_str[copy_len] = '\0';

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGW(TAG, "JSON invalido recebido: %s", json_str);
        return;
    }

    const cJSON *status_item = cJSON_GetObjectItemCaseSensitive(root, "sta");
    const cJSON *lim_item    = cJSON_GetObjectItemCaseSensitive(root, "lim");
    const cJSON *peso_item   = cJSON_GetObjectItemCaseSensitive(root, "pes");

    const char *status = cJSON_IsString(status_item) ? status_item->valuestring : NULL;

    if (status != NULL) {
        if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (cJSON_IsNumber(lim_item))  s_data.lim  = (float)lim_item->valuedouble;
            if (cJSON_IsNumber(peso_item)) s_data.peso = (float)peso_item->valuedouble;
            strncpy(s_data.status, status, sizeof(s_data.status) - 1);
            s_data.status[sizeof(s_data.status) - 1] = '\0';
            xSemaphoreGive(s_data_mutex);
        }
        ESP_LOGI(TAG, "status=%s lim=%.1f peso=%.1f", status, s_data.lim, s_data.peso);
        if (strcmp(status, "finalizado") == 0) {
            float lim  = cJSON_IsNumber(lim_item)  ? (float)lim_item->valuedouble  : 0.0f;
            float peso = cJSON_IsNumber(peso_item) ? (float)peso_item->valuedouble : 0.0f;
            db_insert_record(lim, peso, status);
        }

    } else {
        ESP_LOGW(TAG, "Campo 'sta' ausente ou invalido no JSON: %s", json_str);
    }

    cJSON_Delete(root);
}




static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = WIFI_AP_PASS,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .required = false },
        },
    };

    if (strlen(WIFI_AP_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP iniciado. SSID:%s senha:%s canal:%d",
             WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);
}

static const char *html_page =
"<!DOCTYPE html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<title>Balanca - Monitor</title>"
"<style>"
"body{font-family:Arial,sans-serif;background:#111;color:#eee;text-align:center;padding-top:40px;}"
"h1{font-size:1.5em;color:#0af;}"
".card{background:#1c1c1c;border-radius:16px;padding:30px;max-width:340px;margin:20px auto;box-shadow:0 0 20px rgba(0,0,0,0.5);}"
".peso{font-size:3em;font-weight:bold;margin:10px 0;}"
".lim{font-size:1.1em;color:#999;}"
".status{font-size:1.3em;margin-top:15px;padding:8px 16px;border-radius:8px;display:inline-block;}"
".iniciado{background:#2a5;color:#fff;}"
".andamento{background:#a80;color:#fff;}"
".finalizado{background:#048;color:#fff;}"
".aguardando{background:#444;color:#ccc;}"
"</style></head><body>"
"<h1>Monitor de Pesagem</h1>"
"<div class='card'>"
"<div class='peso' id='peso'>-- g</div>"
"<div class='lim' id='lim'>Limite: -- g</div>"
"<div class='status aguardando' id='status'>aguardando</div>"
"</div>"

"<h2 style='margin-top:40px;'>Historico de Pesagens</h2>"
"<table id='tabela' style='margin:0 auto;border-collapse:collapse;width:90%;max-width:500px;'>"
"<thead><tr style='background:#222;'>"
"<th style='padding:8px;'>ID</th><th>Peso (g)</th><th>Limite (g)</th><th>Tempo (s)</th>"
"</tr></thead><tbody id='corpo-tabela'></tbody></table>"
"<div style='margin-top:30px;'>"
"<button onclick=\"ligarLed()\" style='background:#2a5;color:#fff;border:none;padding:12px 24px;border-radius:8px;font-size:1em;margin:5px;cursor:pointer;'>Ligar LED</button>"
"<button onclick=\"desligarLed()\" style='background:#a22;color:#fff;border:none;padding:12px 24px;border-radius:8px;font-size:1em;margin:5px;cursor:pointer;'>Desligar LED</button>"
"</div>"
"<script>"
"async function atualizarMonitor(){"
"  try{"
"    const r = await fetch('/data');"
"    const d = await r.json();"
"    document.getElementById('peso').innerText = d.peso.toFixed(1) + ' g';"
"    document.getElementById('lim').innerText = 'Limite: ' + d.lim.toFixed(1) + ' g';"
"    const st = document.getElementById('status');"
"    st.innerText = d.status;"
"    st.className = 'status ' + d.status;"
"  }catch(e){}"
"}"
"async function atualizarHistorico(){"
"  try{"
"    const r = await fetch('/history');"
"    const lista = await r.json();"
"    const corpo = document.getElementById('corpo-tabela');"
"    corpo.innerHTML = '';"
"    lista.forEach(item => {"
"      const tr = document.createElement('tr');"
"      tr.innerHTML = `<td style='padding:6px;border-bottom:1px solid #333;'>${item.id}</td>"
"                      <td style='border-bottom:1px solid #333;'>${item.peso.toFixed(1)}</td>"
"                      <td style='border-bottom:1px solid #333;'>${item.lim.toFixed(1)}</td>"
"                      <td style='border-bottom:1px solid #333;'>${item.tempo_s}</td>`;"
"      corpo.appendChild(tr);"
"    });"
"  }catch(e){ console.error(e); }"
"}"

  "async function ligarLed(){"
"  try{ await fetch('/led/on'); }catch(e){ console.error(e); }"
"}"
"async function desligarLed(){"
"  try{ await fetch('/led/off'); }catch(e){ console.error(e); }"
"}"
"setInterval(() => { atualizarMonitor(); atualizarHistorico(); }, 2000);"
"atualizarMonitor();"
"atualizarHistorico();"
"</script></body></html>";
static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t data_get_handler(httpd_req_t *req)
{
    weighing_data_t local_copy;

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        local_copy = s_data;
        xSemaphoreGive(s_data_mutex);
    } else {
        local_copy.lim = 0;
        local_copy.peso = 0;
        strcpy(local_copy.status, "erro");
    }

    char resp_buf[128];
    int len = snprintf(resp_buf, sizeof(resp_buf),
                        "{\"lim\":%.1f,\"peso\":%.1f,\"status\":\"%s\"}",
                        local_copy.lim, local_copy.peso, local_copy.status);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, resp_buf, len);
}

static esp_err_t led_on_handler(httpd_req_t *req)
{
    uart_send_led_command(true);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "LED ligado", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t led_off_handler(httpd_req_t *req)
{
    uart_send_led_command(false);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_send(req, "LED desligado", HTTPD_RESP_USE_STRLEN);
}

static const httpd_uri_t uri_led_on = {
    .uri      = "/led/on",
    .method   = HTTP_GET,
    .handler  = led_on_handler,
};

static const httpd_uri_t uri_led_off = {
    .uri      = "/led/off",
    .method   = HTTP_GET,
    .handler  = led_off_handler,
};


static const httpd_uri_t uri_root = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
};

static const httpd_uri_t uri_data = {
    .uri      = "/data",
    .method   = HTTP_GET,
    .handler  = data_get_handler,
};
static esp_err_t history_get_handler(httpd_req_t *req)
{
    char *json = db_get_history_json(50); 
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return ret;
}

static const httpd_uri_t uri_history = {
    .uri      = "/history",
    .method   = HTTP_GET,
    .handler  = history_get_handler,
};

static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    ESP_ERROR_CHECK(httpd_start(&server, &config));
    httpd_register_uri_handler(server, &uri_root);
    httpd_register_uri_handler(server, &uri_data);
    httpd_register_uri_handler(server, &uri_history); 
   httpd_register_uri_handler(server, &uri_led_on); 
    httpd_register_uri_handler(server, &uri_led_off);
    ESP_LOGI(TAG, "Servidor web iniciado em http://192.168.4.1/");
}



void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_data_mutex = xSemaphoreCreateMutex();

      if (!db_init()) {
        ESP_LOGE(TAG, "Falha ao iniciar banco de dados!");
    }

    wifi_init_softap();
    start_webserver();
    uart_init_custom();
}
