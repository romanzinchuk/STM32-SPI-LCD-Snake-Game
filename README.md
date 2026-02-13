# STM32 Snake Game (ILI9341)

A robust implementation of the classic Snake Game for the **STM32 Nucleo-F410RB** board using an **ILI9341 TFT display**.

This project demonstrates a hybrid approach to embedded programming: using **HAL** for complex peripherals (SPI initialization) and **Direct Register Access (CMSIS)** for performance-critical tasks (GPIO Input).

## Key Features
* **Hardware SPI:** High-speed display communication (ILI9341 driver).
* **Direct Register Access:** Input handling uses `GPIOA->IDR` instead of `HAL_GPIO_ReadPin` to eliminate overhead and ensure instant button response.
* **Optimized Rendering:** Partial screen updates (only redrawing the snake's head and tail) to prevent flickering.
* **Custom Logic:** Game engine written from scratch in C.

## Hardware Used
* **MCU:** STM32F410RB (Nucleo-64)
* **Display:** 2.4" TFT LCD (ILI9341 Controller) - SPI Interface
* **Input:** 4x Tactile Switches (Internal Pull-Up)
