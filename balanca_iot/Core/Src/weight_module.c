/*
 * weight_module.c
 *
 *  Created on: Jun 17, 2026
 *      Author: tmribeiro
 */

#include "weight_module.h"
#include "HX711.h"
#include "stm32f1xx_hal.h"
#include "ssd1306.h"
#include "menu.h"
#include "ssd1306_fonts.h"
#include <stdio.h>
#include <string.h>


extern TIM_HandleTypeDef htim2;
extern ADC_HandleTypeDef hadc1;


void update_bright_display_oled(void){
	uint32_t adc_value = 0;
	uint8_t bright = 0;

	// Inicia uma conversão ADC
	HAL_ADC_Start(&hadc1);

	// Aguarda a conclusão da conversão (timeout de 10 ms)
	if(HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK){
		// Pega o valor do ADC (0 - 4095)
		adc_value = HAL_ADC_GetValue(&hadc1);

		// Mapeia de 12 bits (4095) para 8 bits (255)
		bright = (uint8_t)(adc_value / 16);


		// Setando um brilho minimo para que a tela na fique 100% apagada
		if(bright < 10){
			bright = 10;
		}

		// Definindo o valor de brilho no ssd1306
		ssd1306_SetContrast(bright);

	}

	// Para o ADC para economizar energia até o próximo loop
	HAL_ADC_Stop(&hadc1);
}



void update_led_pwm(float current_weight, float limit_weight, BalancaState current_state)
{
    uint32_t duty_cycle = 0;

    // O LED permanece apagado fora da tela de pesagem
    if (current_state != TELA_PESAGEM)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        return;
    }

    // Evita divisão por zero
    if (limit_weight <= 0.0f)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        return;
    }

    // Sem peso sobre a balança
    if (current_weight <= 0.0f)
    {
        duty_cycle = 0;
    }
    // Peso igual ou acima do limite
    else if (current_weight >= limit_weight)
    {
        duty_cycle = 999;
    }
    else
    {
        // Calcula proporcionalmente o brilho do LED
        duty_cycle = (uint32_t)((current_weight / limit_weight) * 999.0f);
    }

    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, duty_cycle);
}


float measure_weight(hx711_t *hx711){
	float weight = 0.0;

	//Armazena a gramatura definada pelas amostras
	weight = hx711_weight(hx711, 10);

	//Sendo o "peso" < 0, não retornamos um peso negativo
	if(weight > 0.0){
		weight = weight;
	}else{
		weight = 0.0;
	}
	return weight;
}

void send_json_to_esp32(float limite, float peso, const char* status) {
    char json_buffer[128]; // Buffer para armazenar a string

    int len = sprintf(json_buffer, "{\"lim\":%.1f,\"pes\":%.1f,\"sta\":\"%s\"}",
                      limite, peso, status);


    HAL_UART_Transmit(&huart2, (uint8_t*)json_buffer, len, 100);
}


