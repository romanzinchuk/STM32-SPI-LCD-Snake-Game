/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : STM32 Snake Game with ILI9341 & DMA
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include <stdlib.h>

/* --- Hardware Handles --- */
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma;

/* --- Macros --- */
#define CS_LOW()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET)
#define CS_HIGH()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET)

#define DC_CMD()    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_RESET)
#define DC_DATA()   HAL_GPIO_WritePin(GPIOC, GPIO_PIN_7, GPIO_PIN_SET)

#define RST_LOW()   HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_RESET)
#define RST_HIGH()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_9, GPIO_PIN_SET)

/* --- Game Constants --- */
#define BLOCK_SIZE  20
#define GRID_W      (240 / BLOCK_SIZE)
#define GRID_H      (320 / BLOCK_SIZE)
#define SNAKE_MAX   100

#define BLACK       0x0000
#define RED         0xF800
#define GREEN       0x07E0
#define BLUE        0x001F
#define WHITE       0xFFFF

/* --- Game State --- */
typedef struct { int x, y; } Point;
Point snake[SNAKE_MAX];
int snakeLen = 3;
Point food;
int dirX = 1, dirY = 0;
int gameOver = 0;

volatile uint8_t spi_dma_ready = 1; // 1: Free, 0: Busy

/* --- Function Prototypes --- */
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_SPI1_Init(void);
static void DMA_Init(void);

/* --- Display Driver --- */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if(hspi->Instance == SPI1) {
        CS_HIGH();
        spi_dma_ready = 1;
    }
}

void SPI_Send(uint8_t data) {
    HAL_SPI_Transmit(&hspi1, &data, 1, 10);
}

void Write_Cmd(uint8_t cmd) {
    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {}
    DC_CMD();
    CS_LOW();
    SPI_Send(cmd);
    CS_HIGH();
}

void Write_Data(uint8_t data) {
    while (HAL_SPI_GetState(&hspi1) != HAL_SPI_STATE_READY) {}
    DC_DATA();
    CS_LOW();
    SPI_Send(data);
    CS_HIGH();
}

void ILI9341_Init(void) {
    // Hardware reset
    RST_LOW();  HAL_Delay(100);
    RST_HIGH(); HAL_Delay(100);

    // Initialization sequence
    Write_Cmd(0x01); HAL_Delay(100); // SW Reset
    Write_Cmd(0x11); HAL_Delay(100); // Sleep Out

    Write_Cmd(0x3A); Write_Data(0x55); // Pixel Format 16bit

    // Gamma & Power
    Write_Cmd(0xC0); Write_Data(0x23);
    Write_Cmd(0xC1); Write_Data(0x10);
    Write_Cmd(0xC5); Write_Data(0x3E); Write_Data(0x28);
    Write_Cmd(0xC7); Write_Data(0x86);

    Write_Cmd(0x36); Write_Data(0x48); // BGR Order
    Write_Cmd(0xB1); Write_Data(0x00); Write_Data(0x18);
    Write_Cmd(0xB6); Write_Data(0x08); Write_Data(0x82); Write_Data(0x27);

    Write_Cmd(0x29); HAL_Delay(100); // Display ON
}

void ILI9341_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    Write_Cmd(0x2A);
    Write_Data(x >> 8); Write_Data(x & 0xFF);
    Write_Data((x + w - 1) >> 8); Write_Data((x + w - 1) & 0xFF);

    Write_Cmd(0x2B);
    Write_Data(y >> 8); Write_Data(y & 0xFF);
    Write_Data((y + h - 1) >> 8); Write_Data((y + h - 1) & 0xFF);

    Write_Cmd(0x2C);
}

void Fill_Rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    uint32_t total_pixels = w * h;
    uint32_t bytes_to_send = total_pixels * 2;

    // Prevent buffer overflow
    if (bytes_to_send > (BLOCK_SIZE * BLOCK_SIZE * 2)) return;

    while (!spi_dma_ready) {}

    ILI9341_SetWindow(x, y, w, h);

    static uint8_t buffer[BLOCK_SIZE * BLOCK_SIZE * 2];

    for (uint32_t i = 0; i < bytes_to_send; i += 2) {
        buffer[i] = color >> 8;
        buffer[i+1] = color & 0xFF;
    }

    spi_dma_ready = 0;
    DC_DATA();
    CS_LOW();
    HAL_SPI_Transmit_DMA(&hspi1, buffer, bytes_to_send);
}

void Clear_Screen(uint16_t color) {
    while (!spi_dma_ready) {}

    ILI9341_SetWindow(0, 0, 240, 320);
    DC_DATA();
    CS_LOW();

    uint8_t color_high = color >> 8;
    uint8_t color_low = color & 0xFF;

    for (uint32_t i = 0; i < 240 * 320; i++) {
        SPI_Send(color_high);
        SPI_Send(color_low);
    }
    CS_HIGH();
}

/* --- Game Logic --- */
void SpawnFood() {
    srand(HAL_GetTick());
    food.x = rand() % GRID_W;
    food.y = rand() % GRID_H;
}

void ResetGame() {
    snakeLen = 3;
    snake[0].x = 5; snake[0].y = 5;
    snake[1].x = 4; snake[1].y = 5;
    snake[2].x = 3; snake[2].y = 5;
    dirX = 1; dirY = 0;
    gameOver = 0;

    Clear_Screen(BLACK);
    SpawnFood();

    // Draw initial snake
    for (int i = 0; i < snakeLen; i++) {
        Fill_Rect(snake[i].x * BLOCK_SIZE, snake[i].y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, WHITE);
    }
}

void Input() {
    // UP (PA0)
    if (!(GPIOA->IDR & GPIO_PIN_0)) {
        if (dirY == 0) { dirX = 0; dirY = -1; }
    }
    // DOWN (PA1)
    if (!(GPIOA->IDR & GPIO_PIN_1)) {
        if (dirY == 0) { dirX = 0; dirY = 1; }
    }
    // LEFT (PA4)
    if (!(GPIOA->IDR & GPIO_PIN_4)) {
        if (dirX == 0) { dirX = -1; dirY = 0; }
    }
    // RIGHT (PC1)
    if (!(GPIOC->IDR & GPIO_PIN_1)) {
        if (dirX == 0) { dirX = 1; dirY = 0; }
    }
}

void Logic() {
    if (gameOver) return;

    Point tail = snake[snakeLen-1];

    for (int i = snakeLen-1; i > 0; i--) {
        snake[i] = snake[i-1];
    }
    snake[0].x += dirX;
    snake[0].y += dirY;

    // Wrap around screen edges
    if (snake[0].x >= GRID_W) snake[0].x = 0;
    if (snake[0].x < 0)       snake[0].x = GRID_W - 1;
    if (snake[0].y >= GRID_H) snake[0].y = 0;
    if (snake[0].y < 0)       snake[0].y = GRID_H - 1;

    // Food collision
    if (snake[0].x == food.x && snake[0].y == food.y) {
        if (snakeLen < SNAKE_MAX) {
            snake[snakeLen] = tail;
            snakeLen++;
        }
        SpawnFood();
    } else {
        Fill_Rect(tail.x * BLOCK_SIZE, tail.y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, BLACK);
    }

    // Self collision
    for (int i = 1; i < snakeLen; i++) {
        if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
            gameOver = 1;
            Clear_Screen(RED);
            HAL_Delay(2000);
            ResetGame();
        }
    }
}

void Draw() {
    if (gameOver) return;

    // Draw head
    Fill_Rect(snake[0].x * BLOCK_SIZE, snake[0].y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, WHITE);

    // Draw food
    Fill_Rect(food.x * BLOCK_SIZE, food.y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE, RED);
}

/* --- Main Application --- */
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    DMA_Init();
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_SPI1_Init();

    ILI9341_Init();
    ResetGame();

    while (1)
    {
        Input();
        Logic();
        Draw();
        HAL_Delay(200);
    }
}

/* --- System Configuration --- */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 100;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK) {
        Error_Handler();
    }
}

static void DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma.Instance = DMA2_Stream2;
    hdma.Init.Channel = DMA_CHANNEL_2;
    hdma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma.Init.MemInc = DMA_MINC_ENABLE;
    hdma.Init.MemDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma.Init.PeriphDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma.Init.Mode = DMA_NORMAL;
    hdma.Init.Priority = DMA_PRIORITY_LOW;
    hdma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdma.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma.Init.PeriphBurst = DMA_PBURST_SINGLE;

    if (HAL_DMA_Init(&hdma) != HAL_OK) {
        Error_Handler();
    }

    __HAL_LINKDMA(&hspi1, hdmatx, hdma);

    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 15;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port, LCD_RST_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BTN_RIGHT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BTN_RIGHT_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BTN_UP_Pin|BTN_DOWN_Pin|BTN_LEFT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LCD_DC_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_DC_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LCD_RST_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_RST_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LCD_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LCD_CS_GPIO_Port, &GPIO_InitStruct);
}

/* --- Interrupt Handlers --- */
void DMA2_Stream2_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma);
}

void SPI1_IRQHandler(void) {
    HAL_SPI_IRQHandler(&hspi1);
}

void Error_Handler(void) {
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
