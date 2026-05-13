#include <msp430.h>

// --- AYARLAR ---
#define LCD_ADDR 0x27  
#define BH1750_ADDR 0x23

volatile int temp = 0, hum = 0;
volatile unsigned int luxValue = 0, micValue = 0;

// --- UART (BLUETOOTH) FONKSİYONLARI ---
void UART_Init(void) {
    P1SEL |= BIT1 + BIT2; 
    P1SEL2 |= BIT1 + BIT2;
    UCA0CTL1 |= UCSWRST;
    UCA0CTL1 |= UCSSEL_2; 
    UCA0BR0 = 104;        
    UCA0BR1 = 0;
    UCA0MCTL = UCBRS0;
    UCA0CTL1 &= ~UCSWRST;
}

void UART_Write(char c) {
    while (!(IFG2 & UCA0TXIFG));
    UCA0TXBUF = c;
}

void UART_Print(char *str) {
    while(*str) UART_Write(*str++);
}

void UART_PrintNum(int num) {
    char buf[6]; int i = 0;
    if (num == 0) UART_Write('0');
    if (num < 0) { UART_Write('-'); num = -num; }
    while (num > 0) { buf[i++] = (num % 10) + '0'; num /= 10; }
    while (i > 0) UART_Write(buf[--i]);
}

// --- I2C VE LCD FONKSİYONLARI ---
void I2C_Init(void) {
    P1SEL |= BIT6 + BIT7; P1SEL2 |= BIT6 + BIT7;
    UCB0CTL1 |= UCSWRST;
    UCB0CTL0 = UCMST + UCMODE_3 + UCSYNC;
    UCB0CTL1 = UCSSEL_2 + UCSWRST;
    UCB0BR0 = 20; UCB0CTL1 &= ~UCSWRST;
}

void I2C_Write(unsigned char addr, unsigned char data) {
    UCB0I2CSA = addr;
    UCB0CTL1 |= UCTR + UCTXSTT;
    while (!(IFG2 & UCB0TXIFG));
    UCB0TXBUF = data;
    while (!(IFG2 & UCB0TXIFG));
    UCB0CTL1 |= UCTXSTP;
    while (UCB0CTL1 & UCTXSTP);
}

unsigned int BH1750_Read(void) {
    unsigned char h, l;
    UCB0I2CSA = BH1750_ADDR;
    UCB0CTL1 &= ~UCTR;
    UCB0CTL1 |= UCTXSTT;
    unsigned int timeout = 2000;
    while (UCB0CTL1 & UCTXSTT && --timeout);
    if (timeout == 0) return 0;
    while (!(IFG2 & UCB0RXIFG)); h = UCB0RXBUF;
    UCB0CTL1 |= UCTXSTP;
    while (!(IFG2 & UCB0RXIFG)); l = UCB0RXBUF;
    return (h << 8) | l;
}

void LCD_Pulse(unsigned char data) {
    I2C_Write(LCD_ADDR, data | 0x04); __delay_cycles(2000);
    I2C_Write(LCD_ADDR, data & ~0x04); __delay_cycles(1000);
}

void LCD_Send(unsigned char data, unsigned char mode) {
    unsigned char high = (data & 0xF0) | mode | 0x08;
    unsigned char low = ((data << 4) & 0xF0) | mode | 0x08;
    I2C_Write(LCD_ADDR, high); LCD_Pulse(high);
    I2C_Write(LCD_ADDR, low);  LCD_Pulse(low);
}

void LCD_Init() {
    __delay_cycles(200000);
    LCD_Send(0x33, 0); LCD_Send(0x32, 0);
    LCD_Send(0x28, 0); LCD_Send(0x0C, 0);
    LCD_Send(0x01, 0); __delay_cycles(50000);
}

void LCD_SetCursor(unsigned char row, unsigned char col) {
    unsigned char addr = (row == 0 ? 0x80 : 0xC0) + col;
    LCD_Send(addr, 0);
}

void LCD_PrintNum(int num) {
    char buf[6]; int i = 0;
    if (num == 0) LCD_Send('0', 1);
    if (num < 0) { LCD_Send('-', 1); num = -num; }
    while (num > 0) { buf[i++] = (num % 10) + '0'; num /= 10; }
    while (i > 0) LCD_Send(buf[--i], 1);
}

// --- DHT22 OKUMA ---
void DHT_Read(void) {
    unsigned char data[5] = {0,0,0,0,0};
    unsigned int i, j;
    P2DIR |= BIT0; P2OUT &= ~BIT0; __delay_cycles(20000);
    P2OUT |= BIT0; P2DIR &= ~BIT0; __delay_cycles(40);
    if (!(P2IN & BIT0)) {
        while (!(P2IN & BIT0)); while (P2IN & BIT0);
        for (i=0; i<5; i++) {
            for (j=0; j<8; j++) {
                while (!(P2IN & BIT0)); __delay_cycles(40);
                if (P2IN & BIT0) data[i] |= (1 << (7-j));
                while (P2IN & BIT0);
            }
        }
        hum = (data[0] << 8 | data[1]) / 10;
        temp = (data[2] << 8 | data[3]) / 10;
    }
}

// --- KONFOR ANALİZİ ---
void Konfor_Analizi(void) {
    char *mesaj;
    if (micValue > 650) mesaj = "GURULTU ";
    else if (temp > 28) mesaj = "SICAK   ";
    else if (luxValue > 700) mesaj = "COK ISIK";
    else if (luxValue < 50) mesaj = "KARANLIK"; 
    else if (temp >= 20 && temp <= 26 && hum >= 40 && hum <= 60) mesaj = "KONFORLU";
    else mesaj = "STANDART";

    // 2. satırın sonuna (8. karakterden sonrasına) yaz
    LCD_SetCursor(1, 8); 
    while(*mesaj) LCD_Send(*mesaj++, 1);
    
    UART_Print("Durum: "); UART_Print(mesaj); UART_Print("\r\n");
}

// --- ANA PROGRAM ---
int main(void) {
    WDTCTL = WDTPW | WDTHOLD;
    P1DIR |= BIT0;

    UART_Init();
    I2C_Init();
    LCD_Init();
    
    ADC10CTL1 = INCH_4; // Mikrofon P1.4'te
    ADC10CTL0 = ADC10SHT_2 + ADC10ON;
    ADC10AE0 |= BIT4;

    I2C_Write(BH1750_ADDR, 0x01); I2C_Write(BH1750_ADDR, 0x10);

    while(1) {
        luxValue = BH1750_Read();
        DHT_Read();
        ADC10CTL0 |= ENC + ADC10SC;
        while (ADC10CTL0 & ADC10BUSY);
        micValue = ADC10MEM;

        LCD_Send(0x01, 0); __delay_cycles(10000);

        // --- YENİ YERLEŞİM ---
        // Üst Satır: T:25 H:45 M:120
        LCD_SetCursor(0, 0);
        LCD_Send('T', 1); LCD_Send(':', 1); LCD_PrintNum(temp);
        LCD_Send(' ', 1); LCD_Send('H', 1); LCD_Send(':', 1); LCD_PrintNum(hum);
        LCD_Send(' ', 1); LCD_Send('M', 1); LCD_Send(':', 1); LCD_PrintNum(micValue);

        // Alt Satır: L:500 + [KONFOR DURUMU]
        LCD_SetCursor(1, 0);
        LCD_Send('L', 1); LCD_Send(':', 1); LCD_PrintNum(luxValue);
        
        Konfor_Analizi();

        // Bluetooth
        UART_Print("\r\n--- ODA RAPORU ---\r\n");
        UART_Print("Sicaklik: "); UART_PrintNum(temp); UART_Print(" C\r\n");
        UART_Print("Nem: %"); UART_PrintNum(hum); UART_Print("\r\n");
        UART_Print("Isik: "); UART_PrintNum(luxValue); UART_Print(" Lux\r\n");
        UART_Print("Mikrofon: "); UART_PrintNum(micValue); UART_Print("\r\n");

        P1OUT ^= BIT0;
        __delay_cycles(2000000);
    }
}