/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Coin drop sensor - TIM6-triggered ADC sampling at 20 kHz
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define SAMPLE_COUNT       512      // Power of 2 for FFT (25.6 ms at 20 kHz)
#define TRIGGER_THRESHOLD  650     // ADC value that indicates coin impact
#define BASELINE_ADC       450      // Quiescent piezo reading (for reference)
#define COOLDOWN_MS        500      // Ignore further triggers for this long after a capture
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

TIM_HandleTypeDef htim6;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
// Capture buffer - filled by the ADC conversion-complete ISR
static volatile uint16_t adcBuffer[SAMPLE_COUNT];
static volatile uint16_t sampleIndex  = 0;
static volatile bool     capturing    = false;   // true while filling adcBuffer
static volatile bool     captureReady = false;   // set when buffer is full
static volatile uint16_t latestSample = 0;       // most recent ADC reading (for trigger check)
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM6_Init(void);
/* USER CODE BEGIN PFP */
static void TransmitCapturedWaveform(void);
static void PrintString(const char *s);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Convenience UART print for null-terminated strings (blocking).
  */
static void PrintString(const char *s)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)s, strlen(s), HAL_MAX_DELAY);
}

/**
  * @brief  Stream the captured waveform over UART in CSV form.
  *         Format:
  *             COIN_DROP_DETECTED
  *             BEGIN_CSV
  *             sample_index,adc_value
  *             0,784
  *             1,791
  *             ...
  *             END_CSV
  *         A host-side Python script listens for the BEGIN_CSV / END_CSV
  *         markers and writes the intermediate lines to a .csv file.
  */
static void TransmitCapturedWaveform(void)
{
    char line[24];

    PrintString("COIN_DROP_DETECTED\r\n");
    PrintString("BEGIN_CSV\r\n");
    PrintString("sample_index,adc_value\r\n");

    for (uint16_t i = 0; i < SAMPLE_COUNT; i++)
    {
        int n = snprintf(line, sizeof(line), "%u,%u\r\n", i, adcBuffer[i]);
        HAL_UART_Transmit(&huart2, (uint8_t *)line, n, HAL_MAX_DELAY);
    }

    PrintString("END_CSV\r\n");
}

/**
  * @brief  ADC end-of-conversion callback (hardware-triggered by TIM6 TRGO).
  *         Runs at 20 kHz. Keep it short.
  */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance != ADC1) return;

    uint16_t sample = (uint16_t)HAL_ADC_GetValue(&hadc1);
    latestSample = sample;

    if (capturing)
    {
        adcBuffer[sampleIndex++] = sample;
        if (sampleIndex >= SAMPLE_COUNT)
        {
            capturing    = false;
            captureReady = true;
        }
    }
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
  MX_USART2_UART_Init();
  MX_ADC1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

  // Start ADC in interrupt mode - conversions will be triggered by TIM6 TRGO.
  // Each completed conversion fires HAL_ADC_ConvCpltCallback().
  HAL_ADC_Start_IT(&hadc1);

  // Start TIM6 so it begins producing update events that trigger the ADC.
  HAL_TIM_Base_Start(&htim6);

  PrintString("Coin drop sensor ready.\r\n");
  PrintString("Sampling at 20 kHz, waiting for trigger...\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

      // Trigger condition: not currently capturing, no pending unsent buffer,
      // and the most recent sample exceeds the coin-impact threshold.
//      if (!capturing && !captureReady && latestSample >= TRIGGER_THRESHOLD)
//      {
//          // LED on - capture in progress
//          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);
//
//          // Log the trigger event (visible in Termite before the CSV dump).
//          char trigMsg[64];
//          snprintf(trigMsg, sizeof(trigMsg),
//                   ">>> TRIGGERED at ADC = %u (threshold = %u)\r\n",
//                   latestSample, TRIGGER_THRESHOLD);
//          PrintString(trigMsg);
//          PrintString(">>> Capturing 512 samples...\r\n");
//
//          // Arm the capture. The ISR will fill adcBuffer[0..SAMPLE_COUNT-1].
//          sampleIndex  = 0;
//          captureReady = false;
//          capturing    = true;
//      }

	  // 1. QUICK TRIGGER CHECK
	  if (!capturing && !captureReady && latestSample >= TRIGGER_THRESHOLD)
	  {
	      // SAVE THE TRIGGER VALUE IMMEDIATELY
	      // This ensures your 700+ value is actually in the CSV.
	      adcBuffer[0] = latestSample;
	      sampleIndex  = 1;

	      // START CAPTURE IMMEDIATELY
	      capturing = true;

	      // Visual feedback is okay because it's a fast register write
	      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_SET);

	      /* CRITICAL: Do NOT use PrintString or snprintf here!
	         Doing so creates a delay that makes you miss the waveform.
	      */
	  }

	  // 2. DATA PROCESSING (Talk after capturing)
	  if (captureReady)
	  {
	      // NOW it is safe to print your status messages
	      PrintString("\r\n>>> TRIGGER EVENT DETECTED <<<\r\n");

	      // Stream the data
	      TransmitCapturedWaveform();

	      PrintString(">>> Capture complete. Ready for next coin drop.\r\n\r\n");

	      HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
	      HAL_Delay(COOLDOWN_MS);

	      captureReady = false;
	      latestSample = 0;
	  }

//      // Buffer full - transmit it over UART as CSV.
//      if (captureReady)
//      {
//          TransmitCapturedWaveform();
//
//          PrintString(">>> Capture complete. Ready for next coin drop.\r\n\r\n");
//
//          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3, GPIO_PIN_RESET);
//
//          // Cooldown: let the piezo settle and avoid re-triggering on ringdown.
//          HAL_Delay(COOLDOWN_MS);
//
//          captureReady = false;
//          latestSample = 0;   // force the threshold check to re-arm cleanly
//      }
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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure LSE Drive Capability
  */
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSE|RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 16;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Enable MSI Auto calibration
  */
  HAL_RCCEx_EnableMSIPLLMode();
}

/**
  * @brief ADC1 Initialization Function
  *        NOTE: In the .ioc, set:
  *              External Trigger Conversion Source = Timer 6 Trigger Out event
  *              External Trigger Conversion Edge   = Trigger detection on the rising edge
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_2CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief TIM6 Initialization Function
  *        With SYSCLK = 32 MHz and APB1 timer clock = 32 MHz:
  *              f_tim = 32 MHz / ((Prescaler+1) * (Period+1))
  *        For 20 kHz sampling:
  *              Prescaler=1, Period=799  ->  32 MHz / 2 / 800 = 20 kHz
  *
  *        Also set in the .ioc:
  *              Trigger Event Selection = Update Event
  *              Master/Slave Mode       = Disable
  * @retval None
  */
static void MX_TIM6_Init(void)
{

  /* USER CODE BEGIN TIM6_Init 0 */

  /* USER CODE END TIM6_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM6_Init 1 */

  /* USER CODE END TIM6_Init 1 */
  htim6.Instance = TIM6;
  htim6.Init.Prescaler = 1;
  htim6.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6.Init.Period = 799;    // 20 kHz @ 32 MHz APB1 timer clock
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim6) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;   // TRGO on update event -> ADC trigger
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM6_Init 2 */

  /* USER CODE END TIM6_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD3_GPIO_Port, LD3_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : LD3_Pin */
  GPIO_InitStruct.Pin = LD3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD3_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
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
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
