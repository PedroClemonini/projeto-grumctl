/*
 * weight_module.h
 *
 *  Created on: Jun 17, 2026
 *      Author: tmribeiro
 */
#include "HX711.h"
#include "menu.h"
#include "stm32f1xx_hal.h"


#ifndef INC_WEIGHT_MODULE_H_
#define INC_WEIGHT_MODULE_H_



/**
 * @brief Realiza a leitura da célula de carga.
 *
 * Obtém a média de 10 amostras do HX711 e garante que
 * valores negativos sejam tratados como zero.
 *
 * @param hx711 Ponteiro para a estrutura do HX711.
 * @return Peso em gramas.
 */
float measure_weight(hx711_t *hx711);


void update_display_balanca(float current_weight, float target_weight);

/**
 * @brief Ajusta via ADC o contraste do display OLED.
 *
 * Realiza uma leitura do ADC conectado ao potenciometro,
 * convertendo o valor para a faixa aceita pelo SSD1306 e atualizando o
 * contraste do display.
 */
void update_bright_display_oled(void);


/**
 * @brief Atualiza o brilho do LED sinalizador através de PWM.
 *
 * O LED permanece apagado até 70% do peso limite e aumenta sua
 * intensidade proporcionalmente até atingir brilho máximo quando
 * o limite é alcançado.
 *
 * @param current_weight Peso atual medido.
 * @param limit_weight Peso limite configurado.
 * @param current_state Estado atual da stateMachine
 */
void update_led_pwm(float current_weight, float limit_weight, BalancaState current_state);

/**
 * @brief Envia os dados atuais da pesagem ao ESP32.
 *
 * Monta uma mensagem JSON contendo o peso atual,
 * o peso limite configurado e o estado da pesagem,
 * transmitindo-a através da interface I2C.
 *
 * @param limite Peso limite configurado.
 * @param peso Peso atualmente medido.
 * @param status Estado da pesagem
 *               ("iniciado", "andamento" ou "finalizado").
 */
void send_json_to_esp32(float limite, float peso, const char* status) 

#endif /* INC_WEIGHT_MODULE_H_ */
