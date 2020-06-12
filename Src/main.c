/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SIZE_OF_CHECK(type_name, type_size) \
__attribute__((unused)) typedef char type_name ## _sizeof_check[(sizeof(type_name)==type_size)*2-1];
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
const float g_duty_cycle_scale[] = {0.000000, 0.115996, 0.146932, 0.168969,
  0.186752, 0.201959, 0.215411, 0.227578, 0.238758, 0.249153, 0.258906,
  0.268124, 0.276889, 0.285264, 0.293301, 0.301041, 0.308518, 0.315761,
  0.322795, 0.329641, 0.336317, 0.342837, 0.349217, 0.355469, 0.361603,
  0.367629, 0.373556, 0.379392, 0.385143, 0.390818, 0.396421, 0.401958,
  0.407434, 0.412855, 0.418225, 0.423547, 0.428826, 0.434065, 0.439268,
  0.444439, 0.449580, 0.454695, 0.459787, 0.464858, 0.469911, 0.474948,
  0.479974, 0.484989, 0.489997, 0.495000, 0.500000, 0.505000, 0.510003,
  0.515011, 0.520026, 0.525052, 0.530089, 0.535142, 0.540213, 0.545305,
  0.550420, 0.555561, 0.560732, 0.565935, 0.571174, 0.576453, 0.581775,
  0.587145, 0.592566, 0.598042, 0.603579, 0.609182, 0.614857, 0.620608,
  0.626444, 0.632371, 0.638397, 0.644531, 0.650783, 0.657163, 0.663683,
  0.670359, 0.677205, 0.684239, 0.691482, 0.698959, 0.706699, 0.714736,
  0.723111, 0.731876, 0.741094, 0.750847, 0.761242, 0.772422, 0.784589,
  0.798041, 0.813248, 0.831031, 0.853068, 0.884004, 1.000000};

/* The keypad check is set at 50Hz based off a timer interrupt The debounce
   buffer will be 120ms long, so a size of 6. By having the size at 6 and the
   threshold to be a valid button press at 4, you are naturally allowing some
   buffer between changing from one key index to another. If we set the size to
   6 and the threshold to 3, we could theoretically toggle between NO_BUTTON_IDX
   and any other button index quickly. With size 6 and threshold 4, a button
   index has to drop to 2 (from 4 or more) before NO_BUTTON_IDX becomes valid
   again. To summarize, it's ideal to have the threshold greater than
   KEYPAD_DEBOUNCE_BUF_LEN/2. */
#define KEYPAD_DEBOUNCE_BUF_LEN 6
// Must find a button pressed for this many keypad check iterations 
#define KEYPAD_DEBOUNCE_THRESH 4
#define KEYPAD_NUM_ROW 4
#define KEYPAD_NUM_COL 4
// Add one extra button for "no button"
#define KEYPAD_NUM_BUTTON 17  // KEYPAD_NUM_ROW * KEYPAD_NUM_COL + 1
#define NO_BUTTON_IDX 16  // KEYPAD_NUM_BUTTON-1
uint8_t g_key_deb_buf[KEYPAD_DEBOUNCE_BUF_LEN] = {NO_BUTTON_IDX},
        g_key_deb_buf_idx = 0,
        g_prev_key_idx = NO_BUTTON_IDX;

#define HANGER_DEBOUNCE_BUF_LEN 25
#define HANGER_DEBOUNCE_THRESH 10
bool g_hanger_deb_buf[HANGER_DEBOUNCE_BUF_LEN] = {true},
     g_hanger_prev_state = true;
uint8_t g_hanger_deb_buf_idx = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void SetDimmerDutyCycle(const uint16_t duty_cycle) {
  if (duty_cycle > 100) {
    return;
  }
  const uint16_t ccr = g_zero_cross_period -
    g_zero_cross_period*g_duty_cycle_scale[duty_cycle];
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_4, ccr);
}

/*
  R1 - Brn - Brn     - PC0
  R2 - Red - Wht/Brn - PC1
  R3 - Orn - Orn     - PC2
  R4 - Yel - Wht/Orn - PC3
  C1 - Blu - Blu     - PA0
  C2 - Gry - Wht/Blu - PA1
  C3 - Wht - Grn     - PA4
  C4 - Blk - Wht/Grn - PB0

  See KeyIndexToChar() for index to button mapping.
*/
uint8_t GetKeypadPress() {
  GPIO_TypeDef *row_gpio_port[] = {R1_OUT_GPIO_Port, R2_OUT_GPIO_Port,
                                   R3_OUT_GPIO_Port, R4_OUT_GPIO_Port};
  uint16_t row_gpio_pin[] = {R1_OUT_Pin, R2_OUT_Pin, R3_OUT_Pin, R4_OUT_Pin};
  GPIO_TypeDef *col_gpio_port[] = {C1_IN_GPIO_Port, C2_IN_GPIO_Port,
                                   C3_IN_GPIO_Port, C4_IN_GPIO_Port};
  uint16_t col_gpio_pin[] = {C1_IN_Pin, C2_IN_Pin, C3_IN_Pin, C4_IN_Pin};
  uint8_t r, i, c;
  uint8_t pressed_key_idx = NO_BUTTON_IDX;
  for (r = 0; r < KEYPAD_NUM_ROW; ++r) {
    for (i = 0; i < KEYPAD_NUM_ROW; ++i) {
      HAL_GPIO_WritePin(row_gpio_port[i], row_gpio_pin[i],
        r == i ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
    for (c = 0; c < KEYPAD_NUM_COL; ++c) {
      if (HAL_GPIO_ReadPin(col_gpio_port[c], col_gpio_pin[c]) == GPIO_PIN_SET) {
        pressed_key_idx = r*KEYPAD_NUM_COL + c;
        goto ret_key_idx;
      }
    }
  }
ret_key_idx:
  // This reset is not necessary for continuous scanning...
  if (r == 4) { r = 3; }
  HAL_GPIO_WritePin(row_gpio_port[r], row_gpio_pin[r], GPIO_PIN_RESET);
  // Save the pressed key index to the global debounce buffer
  g_key_deb_buf[g_key_deb_buf_idx] = pressed_key_idx;
  ++g_key_deb_buf_idx;
  if (g_key_deb_buf_idx >= KEYPAD_DEBOUNCE_BUF_LEN) {
    g_key_deb_buf_idx = 0;
  }
  return pressed_key_idx;
}

uint8_t GetFilteredKeypadPress() {
  uint8_t key_idx_cnt[KEYPAD_NUM_BUTTON] = {0}, i;
  // Loop through debounce buffer and make key count histogram (key_idx_cnt)
  for (i = 0; i < KEYPAD_DEBOUNCE_BUF_LEN; ++i) {
    uint8_t j = g_key_deb_buf[i];
    if (j < KEYPAD_NUM_BUTTON) {
      key_idx_cnt[j] += 1;
    }
  }
  // Find the index with the largest histogram bin
  uint8_t max_idx = NO_BUTTON_IDX, 
          max_cnt_idx = 0;
  for (i = 0; i < KEYPAD_NUM_BUTTON; ++i) {
    if (key_idx_cnt[i] > max_cnt_idx) {
      max_cnt_idx = key_idx_cnt[i];
      max_idx = i;
    }
  }
  // Check that this index histogram bin count is large enough to be considered
  // a key press. Do not send the same key index twice (allow NO_BUTTON_IDX to
  // be sent in between valid key indicies).
  if (max_cnt_idx > KEYPAD_DEBOUNCE_THRESH) {
    if (max_idx != g_prev_key_idx) {
      g_prev_key_idx = max_idx;
      return max_idx;
    }
  }
  return NO_BUTTON_IDX;
}

char KeyIndexToChar(const uint8_t key_idx) {
  // This is the actual row/col arragement of the buttons (not including 'N')
  // The 'n' is "not connected"
  char keys[] = {'1', '2', '3', 'n',
                 '4', '5', '6', 'F',
                 '7', '8', '9', 'n',
                 '*', '0', '#', 'R', 'N'};
  SIZE_OF_CHECK(keys, KEYPAD_NUM_BUTTON)
  return key_idx < KEYPAD_NUM_BUTTON ? keys[key_idx] : 'N';
}

void UpdateHangerState() {
  g_hanger_deb_buf[g_hanger_deb_buf_idx] =
    HAL_GPIO_ReadPin(HANGER_IN_GPIO_Port, HANGER_IN_Pin) == GPIO_PIN_SET;
  ++g_hanger_deb_buf_idx;
  if (g_hanger_deb_buf_idx >= HANGER_DEBOUNCE_BUF_LEN) {
    g_hanger_deb_buf_idx = 0;
  }
}

bool GetFilteredHangerState() {
  int i;
  uint8_t num_true = 0;
  for (i = 0; i < HANGER_DEBOUNCE_BUF_LEN; ++i) {
    if (g_hanger_deb_buf[i]) {
      ++num_true;
    }
  }
  const uint8_t num_false = HANGER_DEBOUNCE_BUF_LEN - num_true;
  if (num_true >= HANGER_DEBOUNCE_THRESH) {
    g_hanger_prev_state = true;
  } else if (num_false >= HANGER_DEBOUNCE_THRESH) {
    g_hanger_prev_state = false;
  }
  return g_hanger_prev_state;
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM1_Init();
  MX_TIM6_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  HAL_TIM_Base_Start_IT(&htim6); // 50Hz interupt
  HAL_TIM_IC_Start_IT(&htim1, TIM_CHANNEL_1);  // zero cross sync pulse
  HAL_Delay(100); // wait for things to init
  SetDimmerDutyCycle(100);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t sec_cnt = 0,
           grn_led_on_cnt = 0;
  uint8_t prev_key_idx = UINT8_MAX,
          puzzle_keys_idx = 0;
  bool prev_hanger_state = true,
       run_puzzle = false,
       puzzle_solved = false;
  const uint8_t PUZZLE_LEN = 14; // 10 digit number + 4 digit extension
  char puzzle_keys[PUZZLE_LEN];
  bool tile_puzzle_win = false;
  while (1)
  {
    if (g_tim_check_keypad) {
      // Check Tile Puzzle input
      if (HAL_GPIO_ReadPin(PUZZLE_IN_GPIO_Port, PUZZLE_IN_Pin) == GPIO_PIN_RESET) {
        if (!tile_puzzle_win) {
          tile_puzzle_win = true;
          HAL_GPIO_WritePin(RELAY_0_OUT_GPIO_Port, RELAY_0_OUT_Pin, GPIO_PIN_SET);
          HAL_Delay(5000);
          HAL_GPIO_WritePin(RELAY_0_OUT_GPIO_Port, RELAY_0_OUT_Pin, GPIO_PIN_RESET);
        }
      } else {
        tile_puzzle_win = false;
      }

      g_tim_check_keypad = false;
      GetKeypadPress();
      const uint8_t key_idx = GetFilteredKeypadPress();

      if (key_idx == 1) {
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
        ++grn_led_on_cnt;
      }
      if (grn_led_on_cnt > 0) {
        if (grn_led_on_cnt > 10) {
          grn_led_on_cnt = 0;
          HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        } else {
          ++grn_led_on_cnt;
        }
      }

      if (key_idx != prev_key_idx) {
        prev_key_idx = key_idx;
        uint8_t key_char = KeyIndexToChar(key_idx);
        HAL_UART_Transmit(&huart1, &key_char, 1, 1000);
        if (puzzle_solved && key_char == '#') {
          // Stop MP3
          uint8_t start = 'O';
          HAL_UART_Transmit(&huart3, &start, 1, 1000);
          // Turn light back on all the way
          SetDimmerDutyCycle(100);
        } else if (run_puzzle && key_char != 'N') {
          puzzle_keys[puzzle_keys_idx] = key_char;
          ++puzzle_keys_idx;
          if (puzzle_keys_idx == PUZZLE_LEN) {
                        // b    r    i    d    g    e    t
            char ans[] = {'2', '7', '4', '3', '4', '3', '8',
                        // s    a    m
                          '7', '2', '6', '3', '3', '7', '1'};
            SIZE_OF_CHECK(ans, PUZZLE_LEN)
            int i;
            for (i = 0; i < PUZZLE_LEN; ++i) {
              if (puzzle_keys[i] != ans[i]) {
                break;
              }
            }
            if (i == PUZZLE_LEN) {
              // You won!
              puzzle_solved = true;
              for (i = 0; i < 10; ++i) {
                HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
                HAL_Delay(50);
                HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
                HAL_Delay(50);
              }
              // Start dimming
              for (i = 100; i > 50; --i) {
                SetDimmerDutyCycle(i);
                HAL_Delay(50);
              }
              // Play MP3
              uint8_t start = 'O';
              HAL_UART_Transmit(&huart3, &start, 1, 1000);
              // Continue dimming
              for (i = 50; i > 39; --i) {
                SetDimmerDutyCycle(i);
                HAL_Delay(50);
              }
              // Open Box
              HAL_Delay(3000);
              HAL_GPIO_WritePin(RELAY_1_OUT_GPIO_Port, RELAY_1_OUT_Pin, GPIO_PIN_SET);
              HAL_Delay(250);
              HAL_GPIO_WritePin(RELAY_1_OUT_GPIO_Port, RELAY_1_OUT_Pin, GPIO_PIN_RESET);
            }
            // User must apply headset to hanger to restart puzzle
            run_puzzle = false;
            puzzle_keys_idx = 0;
          }
        }
      }

      UpdateHangerState();
      const bool hanger_state = GetFilteredHangerState();
      HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin,
        hanger_state ? GPIO_PIN_SET : GPIO_PIN_RESET);
      if (prev_hanger_state != hanger_state) {
        prev_hanger_state = hanger_state;
        uint8_t key_char = hanger_state ? 'D' : 'U';
        HAL_UART_Transmit(&huart1, &key_char, 1, 1000);
        run_puzzle = !hanger_state;
        if (run_puzzle) {
          puzzle_keys_idx = 0;
        }
      }

      ++sec_cnt;
      if (sec_cnt == 50) {
        if (run_puzzle) {
          HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
          HAL_Delay(10);
          HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        }
        sec_cnt = 0;
      }
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI48;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
