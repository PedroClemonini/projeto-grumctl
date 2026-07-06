/*
 * menu.c
 *
 * Created on: Jul 1, 2026
 * Author: tmribeiro
 */

#include "menu.h"
#include "weight_module.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include <stdio.h>

// Estado atual da maquina de estados
volatile BalancaState current_state = TELA_PRINCIPAL;

// Variavel estatica para definicao das telas entre as opcoes do menu
static int index_menu = 0;
// Definicao de um peso limite 
float limit_weight = 500.0;
// Timer utilizado pelo encoder rotativo
static TIM_HandleTypeDef *encoder_timer;
// Booleano identificador para a operacao de TARA da balança
extern volatile uint8_t flag_solicita_tara;


// Inicializa a máquina de estados e armazena o timer do encoder
void menu_init(TIM_HandleTypeDef *htim){
    current_state = TELA_PRINCIPAL;
    encoder_timer = htim; // Salvando para a máquina de estados usar depois

}

void menu_update(float peso_atual,uint32_t encoder_value){
    switch(current_state){
        case TELA_PRINCIPAL:
        	ssd1306_Fill(Black);
        	ssd1306_SetCursor(15, 5);
			ssd1306_WriteString("INICIAR PESAGEM", Font_7x10, White);

			// Desenho da base da balança
			ssd1306_FillRectangle(44, 54, 84, 60, White); // Base trapezoidal/retangular
			ssd1306_FillRectangle(62, 24, 66, 54, White); // Pilar central (fino)

			// Desenha a haste superior
			ssd1306_FillRectangle(30, 24, 98, 26, White);

			// Desenha o prato esquerdo
			ssd1306_Line(32, 26, 20, 44, White);          // Fio esquerdo
			ssd1306_Line(32, 26, 44, 44, White);          // Fio direito
			ssd1306_FillRectangle(15, 44, 49, 47, White); // Bandeja esquerda

			// Desenha o prato direito
			ssd1306_Line(96, 26, 84, 44, White);          // Fio esquerdo
			ssd1306_Line(96, 26, 108, 44, White);         // Fio direito
			ssd1306_FillRectangle(79, 44, 113, 47, White);// Bandeja direita

			// Atualiza o display para mostrar o desenho
			ssd1306_UpdateScreen();
        	
			break;

        case TELA_PESAGEM:

        	ssd1306_Fill(Black);
			// Desenha o peso atual grande na tela
			char weight_buf[32];
			sprintf(weight_buf, "%.1f g", peso_atual);
			ssd1306_SetCursor(10, 20);
			ssd1306_WriteString(weight_buf, Font_11x18, White);

            // Condicional para validar se o peso limite foi excedido
			if (limit_weight > 0.0 && peso_atual >= limit_weight) {
				ssd1306_SetCursor(10, 45);

				ssd1306_WriteString("LIMITE EXCEDIDO", Font_7x10, White);
			} else {
				// Exibe o limite configurado enquanto ele não for excedido
				char lim_buf[32];
				sprintf(lim_buf, "Lim: %.1fg", limit_weight);
				ssd1306_SetCursor(0, 50);
				ssd1306_WriteString(lim_buf, Font_7x10, White);
			}

			ssd1306_UpdateScreen();
			break;

        case TELA_MENU:
            // Limita o índice do menu entre 0 e 2 (3 opções)
            index_menu = (encoder_value / 4) % 3;

            ssd1306_Fill(Black);
            ssd1306_SetCursor(0, 0);
            ssd1306_WriteString("   --- MENU ---", Font_7x10, White);

            // Iniciar Pesagem
            ssd1306_SetCursor(0, 15);
            if(index_menu == 0){
                ssd1306_WriteString(">", Font_7x10, White);
            }
            ssd1306_SetCursor(10, 15);
            ssd1306_WriteString("1. Iniciar Pesagem", Font_7x10, White);

            // Realizar o TARA
            ssd1306_SetCursor(0, 30);
            if(index_menu == 1){
                ssd1306_WriteString(">", Font_7x10, White);
            }
            ssd1306_SetCursor(10, 30);
            ssd1306_WriteString("2. Fazer Tara", Font_7x10, White);

            // Definir limite de peso
            ssd1306_SetCursor(0, 45);
            if(index_menu == 2){
                ssd1306_WriteString(">", Font_7x10, White);
            }
            ssd1306_SetCursor(10, 45);
            ssd1306_WriteString("3. Define Limite", Font_7x10, White);

            ssd1306_UpdateScreen();
            break;

        case TELA_LIMITE:
            // Converte a posição do encoder para o valor do peso limite
        	if (encoder_value > 60000) {
				__HAL_TIM_SET_COUNTER(encoder_timer, 0);
				encoder_value = 0;
			}

			// Define o limite com passos de 5 em 5 gramas
			limit_weight = (encoder_value / 4) * 5.0;

			ssd1306_Fill(Black);
			ssd1306_SetCursor(0, 10);
			ssd1306_WriteString("Novo Limite:", Font_7x10, White);

			char buffer[32];
			sprintf(buffer, "%.1f g", limit_weight);
			ssd1306_SetCursor(0, 30);
			ssd1306_WriteString(buffer, Font_11x18, White);

			ssd1306_UpdateScreen();
			break;

    }
}

void menu_action_btn(void){
    if (current_state == TELA_PRINCIPAL) {
        current_state = TELA_MENU;

        __HAL_TIM_SET_COUNTER(encoder_timer, 0);

    } else if (current_state == TELA_MENU) {

        if (index_menu == 0) {
            //Opção: "Iniciar Pesag": Inicia a tela de pesagem
            current_state = TELA_PESAGEM;

        } else if (index_menu == 1) {

            //Opção: "Fazer Tara": Realiza o TARA e volta para o menu
        	flag_solicita_tara = 1;
        	current_state = TELA_MENU;

        } else if (index_menu == 2) {
            // Opção "Definir Limite": Vai para a tela de ajuste

            current_state = TELA_LIMITE;

            uint32_t encoder_current_value = (uint32_t)((limit_weight / 5.0) * 4);

            __HAL_TIM_SET_COUNTER(encoder_timer, encoder_current_value);
        }

    } else if (current_state == TELA_LIMITE) {
        current_state = TELA_MENU;
    }else if (current_state == TELA_PESAGEM){
    	current_state = TELA_PRINCIPAL;
    }
}
