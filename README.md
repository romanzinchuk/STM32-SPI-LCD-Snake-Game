# STM32 Snake Game (ILI9341 & DMA)

A classic Snake game implemented on an STM32 microcontroller. The project uses an ILI9341 TFT display over SPI, leveraging DMA (Direct Memory Access) for high-performance rendering without blocking the CPU.

## Technical Implementation Details

* **Zero Frame Buffer Architecture:** A full 320x240 16-bit color frame buffer requires ~153.6 KB of RAM, which exceeds the capacity of many mid-range STM32 MCUs. To solve this, the game uses **partial rendering**. It only updates the localized 20x20 pixel blocks that change state (the new head and the deleted tail) directly into the display's GRAM.
* **Asynchronous DMA SPI Transfers:** Pixel data is transmitted using SPI combined with DMA (`HAL_SPI_Transmit_DMA`). A volatile hardware state flag (`spi_dma_ready`) and the `HAL_SPI_TxCpltCallback` interrupt ensure synchronization. The CPU computes the next game tick while the DMA controller independently handles the high-speed pixel data transfer.
* **Static Buffer & Overflow Protection:** To prevent HardFaults and memory corruption, the main rendering function uses a statically allocated 800-byte buffer (`20x20 pixels * 2 bytes/pixel`). A strict safety guard blocks any render requests exceeding this capacity. Full-screen clears bypass the DMA buffer and use standard blocking SPI polling.
* **Direct Register Input Polling:** While the project relies on the STM32 HAL for peripheral initialization, button inputs are read via direct memory access to the GPIO Input Data Register (e.g., `GPIOA->IDR`). This minimizes function call overhead during the critical game loop.

## Hardware Connections

### Display (ILI9341)
| ILI9341 Pin | STM32 Pin   | Function               |
| :---------- | :---------- | :--------------------- |
| CS          | PB6         | Chip Select            |
| DC/RS       | PC7         | Data / Command         |
| RESET       | PA9         | Hardware Reset         |
| MOSI        | SPI1 MOSI   | SPI Data (Usually PA7) |
| SCK         | SPI1 SCK    | SPI Clock (Usually PA5)|

### Controls
| Button Action | STM32 Pin | Configuration   |
| :------------ | :-------- | :-------------- |
| **UP** | PA0       | Input Pull-up   |
| **DOWN** | PA1       | Input Pull-up   |
| **LEFT** | PA4       | Input Pull-up   |
| **RIGHT** | PC1       | Input Pull-up   |

*Note: The buttons are configured with internal pull-ups and are triggered on a LOW state (connected to GND when pressed).*

## Getting Started
1. Clone this repository.
2. Open the project in STM32CubeIDE.
3. Compile and flash the firmware to your STM32 board.
4. Connect the ILI9341 display and buttons according to the pinout table.
5. Press the RESET button on your STM32 to start playing.
