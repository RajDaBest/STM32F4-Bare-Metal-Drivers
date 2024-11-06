#include "../Include/uart.h"

// Pin and bit definitions
#define UE_BIT 13
#define M_BIT 12
#define STOP_BIT 12
#define TX_PIN 2
#define RX_PIN 3
#define GPIOA_EN 0
#define USART2_EN 17
#define OVER8 15
#define ONEBIT 11
#define PCE 10
#define TXEIE 7
#define TCIE 6
#define TE 3
#define RE 2
#define TXE 7
#define RXNE 5
#define RXNEIE 5
#define TC 6

// Buffer configurations
#define TX_BUFFER_SIZE 128
#define RX_BUFFER_SIZE 128

// TX Buffer structure
typedef struct
{
    volatile uint8_t data[TX_BUFFER_SIZE];
    volatile uint8_t index;
    volatile uint8_t length;
    volatile bool busy;
} TxBuffer;

// RX Buffer structure
typedef struct
{
    volatile uint8_t data[RX_BUFFER_SIZE];
    volatile uint8_t write_index;
    volatile uint8_t read_index;
} RxBuffer;

// Global buffer instances
static TxBuffer tx_buffer = {0};
static RxBuffer rx_buffer = {0};

// Buffer management functions
static bool tx_buffer_is_busy(void)
{
    return tx_buffer.busy;
}

static bool tx_buffer_write(const char *str, uint8_t len)
{
    if (tx_buffer_is_busy() || !str || len == 0 || len > TX_BUFFER_SIZE)
    {
        return false;
    }

    tx_buffer.busy = true;
    tx_buffer.length = len;
    tx_buffer.index = 0;

    for (uint8_t i = 0; i < len; i++)
    {
        tx_buffer.data[i] = (uint8_t)str[i];
    }

    return true;
}

static void tx_buffer_reset(void)
{
    tx_buffer.index = 0;
    tx_buffer.length = 0;
    tx_buffer.busy = false;
}

static bool rx_buffer_is_full(void)
{
    uint8_t next_write = (rx_buffer.write_index + 1) & (RX_BUFFER_SIZE - 1);
    return next_write == rx_buffer.read_index;
}

static bool rx_buffer_write(uint8_t data)
{
    if (rx_buffer_is_full())
    {
        return false;
    }

    rx_buffer.data[rx_buffer.write_index] = data;
    rx_buffer.write_index = (rx_buffer.write_index + 1) & (RX_BUFFER_SIZE - 1);
    return true;
}

static bool rx_buffer_read(uint8_t *data)
{
    if (rx_buffer.read_index == rx_buffer.write_index)
    {
        return false;
    }

    *data = rx_buffer.data[rx_buffer.read_index];
    rx_buffer.read_index = (rx_buffer.read_index + 1) & (RX_BUFFER_SIZE - 1);
    return true;
}

// UART interrupt handler
void USART2_Handler(void)
{
    // Handle TX
    if (IS_SET(USART2->SR, TXE))
    {
        if (tx_buffer.index < tx_buffer.length)
        {
            USART2->DR = tx_buffer.data[tx_buffer.index++];
        }
        else
        {
            while (!IS_SET(USART2->SR, TC))
            {
                // Wait for transmission complete
            }

            tx_buffer_reset();
            CLEAR_BIT(USART2->CR1, TXEIE);
            CLEAR_BIT(USART2->CR1, TCIE);
        }
    }

    // Handle RX
    if (IS_SET(USART2->SR, RXNE))
    {
        uint8_t received_data = USART2->DR; // i dont check the receive errors since this is a general driver and i dont have any specific thing in mind according to the application which will force me to do certain things when certain errors arise
        if (!rx_buffer_write(received_data))
        {
            // Buffer full - data is discarded
        }
    }
}

// Public UART functions
uint8_t UART2_write(const char *str, uint8_t len)
{
    if (!tx_buffer_write(str, len))
    {
        return 0;
    }

    SET_BIT(USART2->CR1, TXEIE);
    SET_BIT(USART2->CR1, TCIE);
    return len;
}

bool UART2_read(uint8_t *data)
{
    return rx_buffer_read(data);
}

void UART2_init(void)
{
    // GPIOA configuration
    SET_BIT(RCC->AHB1ENR, GPIOA_EN);

    // Configure TX pin
    CLEAR_BIT(GPIOA->MODER, TX_PIN * 2);
    SET_BIT(GPIOA->MODER, TX_PIN * 2 + 1);
    SET_BIT(GPIOA->AFR[0], TX_PIN * 4);
    SET_BIT(GPIOA->AFR[0], TX_PIN * 4 + 1);
    SET_BIT(GPIOA->AFR[0], TX_PIN * 4 + 2);
    CLEAR_BIT(GPIOA->AFR[0], TX_PIN * 4 + 3);

    // Configure RX pin
    CLEAR_BIT(GPIOA->MODER, RX_PIN * 2);
    SET_BIT(GPIOA->MODER, RX_PIN * 2 + 1);
    SET_BIT(GPIOA->AFR[0], RX_PIN * 4);
    SET_BIT(GPIOA->AFR[0], RX_PIN * 4 + 1);
    SET_BIT(GPIOA->AFR[0], RX_PIN * 4 + 2);
    CLEAR_BIT(GPIOA->AFR[0], RX_PIN * 4 + 3);
    SET_BIT(GPIOA->PUPDR, RX_PIN * 2);
    CLEAR_BIT(GPIOA->PUPDR, RX_PIN * 2 + 1);

    // USART2 configuration
    SET_BIT(RCC->APB1ENR, USART2_EN);
    CLEAR_BIT(USART2->CR1, M_BIT);
    CLEAR_BIT(USART2->CR2, STOP_BIT);
    CLEAR_BIT(USART2->CR2, STOP_BIT + 1);
    CLEAR_BIT(USART2->CR1, OVER8);
    CLEAR_BIT(USART2->CR3, ONEBIT);
    CLEAR_BIT(USART2->CR1, PCE);

    // Set baud rate to 115200
    USART2->BRR = (8 << 4) | (11);

    // Enable RX interrupt
    SET_BIT(USART2->CR1, RXNEIE);

    // Enable USART2 interrupts in NVIC
    NVIC_EnableIRQ(USART2_IRQn);

    // Enable transmitter and receiver
    SET_BIT(USART2->CR1, TE);
    SET_BIT(USART2->CR1, RE);

    // Enable USART2 module
    SET_BIT(USART2->CR1, UE_BIT);
}

// Example main function
#define PIN5 5
#define LED_PIN PIN5

int main(void)
{
    UART2_init();

    // Configure LED pin
    SET_BIT(GPIOA->MODER, 2 * LED_PIN);
    CLEAR_BIT(GPIOA->MODER, 2 * LED_PIN + 1);

    uint8_t received_byte;
    while (true)
    {
        if (UART2_read(&received_byte))
        {
            char echo_byte = received_byte + 1;

            // Try to echo until successful
            while (UART2_write(&echo_byte, 1) == 0)
            {
                // Optional: Could add a timeout here
            }
        }
    }

    return 0;
}