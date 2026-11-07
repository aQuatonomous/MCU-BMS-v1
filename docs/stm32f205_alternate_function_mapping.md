| Pin Name | AF0 (SYS) | AF1 (TIM1/2) | AF2 (TIM3/4/5) | AF3 (TIM8/9/10/11) | AF4 (I2C1/2/3) | AF5 (SPI1/2/I2S2) | AF6 (SPI3/I2S3) | AF7 (USART1/2/3) | AF8 (UART4/5/USART6) | AF9 (CAN1/2/TIM12/13/14) | AF10 (OTG_FS/HS) | AF11 (ETH) | AF12 (FSMC/SDIO/OTG_HS) | AF13 (DCMI) | AF14 | AF15 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **PA0-WKUP** | - | TIM2_CH1_ETR | TIM5_CH1 | TIM8_ETR | - | - | - | USART2_CTS | UART4_TX | - | - | ETH_MII_CRS | - | - | - | EVENTOUT |
| **PA1** | - | TIM2_CH2 | TIM5_CH2 | - | - | - | - | USART2_RTS | UART4_RX | - | - | ETH_MII_RX_CLK, ETH_RMII_REF_CLK | - | - | - | EVENTOUT |
| **PA2** | - | TIM2_CH3 | TIM5_CH3 | TIM9_CH1 | - | - | - | USART2_TX | - | - | - | ETH_MDIO | - | - | - | EVENTOUT |
| **PA3** | - | TIM2_CH4 | TIM5_CH4 | TIM9_CH2 | - | - | - | USART2_RX | - | - | OTG_HS_ULPI_D0 | ETH_MII_COL | - | - | - | EVENTOUT |
| **PA4** | - | - | - | - | - | SPI1_NSS | SPI3_NSS, I2S3_WS | USART2_CK | - | - | - | - | OTG_HS_SOF | DCMI_HSYNC | - | EVENTOUT |
| **PA5** | - | TIM2_CH1_ETR | - | TIM8_CH1N | - | SPI1_SCK | - | - | - | - | OTG_HS_ULPI_CK | - | - | - | - | EVENTOUT |
| **PA6** | - | TIM1_BKIN | TIM3_CH1 | TIM8_BKIN | - | SPI1_MISO | - | - | - | TIM13_CH1 | - | - | - | DCMI_PIXCK | - | EVENTOUT |
| **PA7** | - | TIM1_CH1N | TIM3_CH2 | TIM8_CH1N | - | SPI1_MOSI | - | - | - | TIM14_CH1 | - | ETH_MII_RX_DV, ETH_RMII_CRS_DV | - | - | - | EVENTOUT |
| **PA8** | MCO1 | TIM1_CH1 | - | - | I2C3_SCL | - | - | USART1_CK | - | - | OTG_FS_SOF | - | - | - | - | EVENTOUT |
| **PA9** | - | TIM1_CH2 | - | - | I2C3_SMBA | - | - | USART1_TX | - | - | - | - | - | DCMI_D0 | - | EVENTOUT |
| **PA10** | - | TIM1_CH3 | - | - | - | - | - | USART1_RX | - | - | OTG_FS_ID | - | - | DCMI_D1 | - | EVENTOUT |
| **PA11** | - | TIM1_CH4 | - | - | - | - | - | USART1_CTS | - | CAN1_RX | OTG_FS_DM | - | - | - | - | EVENTOUT |
| **PA12** | - | TIM1_ETR | - | - | - | - | - | USART1_RTS | - | CAN1_TX | OTG_FS_DP | - | - | - | - | EVENTOUT |
| **PA13** | JTMS-SWDIO | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PA14** | JTCK-SWCLK | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PA15** | JTDI | TIM2_CH1, TIM2_ETR | - | - | - | SPI1_NSS | SPI3_NSS, I2S3_WS | - | - | - | - | - | - | - | - | EVENTOUT |
| **PB0** | - | TIM1_CH2N | TIM3_CH3 | TIM8_CH2N | - | - | - | - | - | - | OTG_HS_ULPI_D1 | ETH_MII_RXD2 | - | - | - | EVENTOUT |
| **PB1** | - | TIM1_CH3N | TIM3_CH4 | TIM8_CH3N | - | - | - | - | - | - | OTG_HS_ULPI_D2 | ETH_MII_RXD3 | - | - | - | EVENTOUT |
| **PB2** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PB3** | JTDO/TRACESWO | TIM2_CH2 | - | - | - | SPI1_SCK | SPI3_SCK, I2S3_SCK | - | - | - | - | - | - | - | - | EVENTOUT |
| **PB4** | JTRST | - | TIM3_CH1 | - | - | SPI1_MISO | SPI3_MISO | - | - | - | - | - | - | - | - | EVENTOUT |
| **PB5** | - | - | TIM3_CH2 | - | I2C1_SMBA | SPI1_MOSI | SPI3_MOSI, I2S3_SD | - | - | CAN2_RX | OTG_HS_ULPI_D7 | ETH_PPS_OUT | - | DCMI_D10 | - | EVENTOUT |
| **PB6** | - | - | TIM4_CH1 | - | I2C1_SCL | - | - | USART1_TX | - | CAN2_TX | - | - | - | DCMI_D5 | - | EVENTOUT |
| **PB7** | - | - | TIM4_CH2 | - | I2C1_SDA | - | - | USART1_RX | - | - | - | - | FSMC_NL | DCMI_VSYNC | - | EVENTOUT |
| **PB8** | - | - | TIM4_CH3 | TIM10_CH1 | I2C1_SCL | - | - | - | - | CAN1_RX | - | ETH_MII_TXD3 | SDIO_D4 | DCMI_D6 | - | EVENTOUT |
| **PB9** | - | - | TIM4_CH4 | TIM11_CH1 | I2C1_SDA | SPI2_NSS, I2S2_WS | - | - | - | CAN1_TX | - | - | SDIO_D5 | DCMI_D7 | - | EVENTOUT |
| **PB10** | - | TIM2_CH3 | - | - | I2C2_SCL | SPI2_SCK, I2S2_SCK | - | USART3_TX | - | - | OTG_HS_ULPI_D3 | ETH_MII_RX_ER, ETH_MII_TX_EN | - | - | - | EVENTOUT |
| **PB11** | - | TIM2_CH4 | - | - | I2C2_SDA | - | - | USART3_RX | - | - | OTG_HS_ULPI_D4 | ETH_RMII_TX_EN | - | - | - | EVENTOUT |
| **PB12** | - | TIM1_BKIN | - | - | I2C2_SMBA | SPI2_NSS, I2S2_WS | - | USART3_CK | - | CAN2_RX | OTG_HS_ULPI_D5 | ETH_MII_TXD0, ETH_RMII_TXD0 | OTG_HS_ID | - | - | EVENTOUT |
| **PB13** | - | TIM1_CH1N | - | - | - | SPI2_SCK, I2S2_SCK | - | USART3_CTS | - | CAN2_TX | OTG_HS_ULPI_D6 | ETH_MII_TXD1, ETH_RMII_TXD1 | - | - | - | EVENTOUT |
| **PB14** | - | TIM1_CH2N | - | TIM8_CH2N | - | SPI2_MISO | - | USART3_RTS | - | TIM12_CH1 | - | - | OTG_HS_DM | - | - | EVENTOUT |
| **PB15** | RTC_50Hz | TIM1_CH3N | - | TIM8_CH3N | - | SPI2_MOSI | - | - | - | TIM12_CH2 | - | - | OTG_HS_DP | - | - | EVENTOUT |
| **PC0** | - | - | - | - | - | - | - | - | - | - | OTG_HS_ULPI_STP | - | - | - | - | EVENTOUT |
| **PC1** | - | - | - | - | - | - | - | - | - | - | - | ETH_MDC | - | - | - | EVENTOUT |
| **PC2** | - | - | - | - | - | SPI2_MISO | - | - | - | - | OTG_HS_ULPI_DIR | ETH_MII_TXD2 | - | - | - | EVENTOUT |
| **PC3** | - | - | - | - | - | SPI2_MOSI | - | - | - | - | OTG_HS_ULPI_NXT | ETH_MII_TX_CLK | - | - | - | EVENTOUT |
| **PC4** | - | - | - | - | - | - | - | - | - | - | - | ETH_MII_RXD0, ETH_RMII_RXD0 | - | - | - | EVENTOUT |
| **PC5** | - | - | - | - | - | - | - | - | - | - | - | ETH_MII_RXD1, ETH_RMII_RXD1 | - | - | - | EVENTOUT |
| **PC6** | - | - | TIM3_CH1 | TIM8_CH1 | - | I2S2_MCK | - | - | USART6_TX | - | - | - | SDIO_D6 | DCMI_D0 | - | EVENTOUT |
| **PC7** | - | - | TIM3_CH2 | TIM8_CH2 | - | - | I2S3_MCK | - | USART6_RX | - | - | - | SDIO_D7 | DCMI_D1 | - | EVENTOUT |
| **PC8** | - | - | TIM3_CH3 | TIM8_CH3 | - | - | - | - | USART6_CK | - | - | - | SDIO_D0 | DCMI_D2 | - | EVENTOUT |
| **PC9** | MCO2 | - | TIM3_CH4 | TIM8_CH4 | I2C3_SDA | I2S2_CKIN | I2S3_CKIN | - | - | - | - | - | SDIO_D1 | DCMI_D3 | - | EVENTOUT |
| **PC10** | - | - | - | - | - | - | SPI3_SCK, I2S3_SCK | USART3_TX | UART4_TX | - | - | - | SDIO_D2 | DCMI_D8 | - | EVENTOUT |
| **PC11** | - | - | - | - | - | - | SPI3_MISO | USART3_RX | UART4_RX | - | - | - | SDIO_D3 | DCMI_D4 | - | EVENTOUT |
| **PC12** | - | - | - | - | - | - | SPI3_MOSI, I2S3_SD | USART3_CK | UART5_TX | - | - | - | SDIO_CK | DCMI_D9 | - | EVENTOUT |
| **PC13** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PC14** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PC15** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PH0** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |
| **PH1** | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | EVENTOUT |