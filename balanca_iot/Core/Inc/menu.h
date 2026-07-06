/*
 * menu.h
 *
 *  Created on: Jul 1, 2026
 *      Author: tmribeiro
 */

#ifndef INC_MENU_H_
#define INC_MENU_H_

#include <stdint.h>
#include "ssd1306.h"

typedef enum {
	TELA_PRINCIPAL = 0,
	TELA_MENU,
	TELA_LIMITE,
	TELA_PESAGEM
}BalancaState;

extern volatile BalancaState current_state;
extern float limit_weight;

/**
 * @brief Inicializa a máquina de estados do menu.
 *
 * Armazena o timer responsável pela leitura do encoder e
 * posiciona o sistema na tela principal.
 */
void menu_init(TIM_HandleTypeDef *htim);


/**
 * @brief Atualiza a interface gráfica conforme o estado atual.
 *
 * @param peso_atual Valor atual medido pela célula de carga.
 * @param encoder_value Contagem atual do encoder rotativo.
 */
void menu_update(float peso_atual,uint32_t encoder_value);


/**
 * @brief Trata o pressionamento do botão do encoder.
 *
 * Dependendo do estado atual da máquina de estados,
 * executa a ação correspondente à opção selecionada.
 */
void menu_action_btn(void);



#endif /* INC_MENU_H_ */
