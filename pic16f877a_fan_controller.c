// PIC16F877A Configuration Bit Settings

// CONFIG
#pragma config FOSC = HS        // Oscillator Selection bits (HS oscillator)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable bit (PWRT enabled)
#pragma config BOREN = ON       // Brown-out Reset Enable bit (BOR enabled)
#pragma config LVP = OFF        // Low-Voltage Programming disabled
#pragma config CPD = OFF        // Data EEPROM code protection off
#pragma config WRT = OFF        // Flash Program Memory Write protection off
#pragma config CP = OFF         // Flash Program Memory code protection off

#include <xc.h>

#define _XTAL_FREQ 4000000 // 4MHz clock frequency

// Motor and button mapping
#define IN1 RD0             // L293D Input 1
#define IN2 RD1             // L293D Input 2
#define START_STOP_BTN RB0  // Start/Stop Toggle
#define DIR_BTN RB1         // Direction Toggle

// LCD mapping in 4-bit mode
// LCD data pins: D4->RD4, D5->RD5, D6->RD6, D7->RD7
#define LCD_RS RD2
#define LCD_EN RD3
#define LCD_D4 RD4
#define LCD_D5 RD5
#define LCD_D6 RD6
#define LCD_D7 RD7

// Mini traffic light and buzzer mapping
#define GREEN_LED  RC0     // Low speed
#define YELLOW_LED RC1     // Medium speed
#define RED_LED    RC3     // High/full speed
#define BUZZER     RC4     // Buzzes at maximum speed

// Global state variables
int is_running = 0;
int current_dir = 0; // 0 = Forward, 1 = Reverse

void lcd_pulse_enable(void) {
    LCD_EN = 1;
    __delay_us(5);
    LCD_EN = 0;
    __delay_us(100);
}

void lcd_send_nibble(unsigned char nibble) {
    LCD_D4 = (nibble >> 0) & 1;
    LCD_D5 = (nibble >> 1) & 1;
    LCD_D6 = (nibble >> 2) & 1;
    LCD_D7 = (nibble >> 3) & 1;
    lcd_pulse_enable();
}

void lcd_cmd(unsigned char cmd) {
    LCD_RS = 0;
    lcd_send_nibble(cmd >> 4);
    lcd_send_nibble(cmd & 0x0F);
    __delay_ms(2);
}

void lcd_data(unsigned char data) {
    LCD_RS = 1;
    lcd_send_nibble(data >> 4);
    lcd_send_nibble(data & 0x0F);
    __delay_us(100);
}

void lcd_print(const char *text) {
    while (*text) {
        lcd_data(*text++);
    }
}

void lcd_set_cursor(unsigned char row, unsigned char col) {
    unsigned char address;

    if (row == 1) {
        address = 0x80 + (col - 1);
    } else {
        address = 0xC0 + (col - 1);
    }

    lcd_cmd(address);
}

void lcd_clear(void) {
    lcd_cmd(0x01);
    __delay_ms(2);
}

void lcd_init(void) {
    __delay_ms(20);

    LCD_RS = 0;
    LCD_EN = 0;

    // Standard HD44780 4-bit initialization sequence
    lcd_send_nibble(0x03);
    __delay_ms(5);
    lcd_send_nibble(0x03);
    __delay_us(150);
    lcd_send_nibble(0x03);
    lcd_send_nibble(0x02);

    lcd_cmd(0x28); // 4-bit mode, 2 lines, 5x8 font
    lcd_cmd(0x0C); // Display ON, cursor OFF
    lcd_cmd(0x06); // Auto-increment cursor
    lcd_clear();
}

void init_adc(void) {
    ADCON0 = 0x41; // Fosc/8, Channel 0 (RA0), ADC On
    ADCON1 = 0x8E; // Right justified, RA0 is Analog, others Digital
}

unsigned int read_adc(void) {
    __delay_ms(2);    // Wait for acquisition time
    GO_nDONE = 1;     // Start conversion
    while (GO_nDONE); // Wait for finish
    return ((ADRESH << 8) + ADRESL);
}

void init_pwm(void) {
    TRISC2 = 0;        // RC2/CCP1 is PWM output
    CCP1CON = 0x0C;    // Set CCP1 to PWM mode
    PR2 = 0x63;        // Sets frequency to approx 10kHz
    T2CON = 0x04;      // Timer2 ON with 1:1 Prescaler
}

void set_speed(unsigned int duty) {
    // Direct 10-bit mapping (ADC 0-1023 to PWM Duty Cycle)
    CCPR1L = duty >> 2;
    CCP1CON = (CCP1CON & 0xCF) | ((duty & 0x03) << 4);
}

unsigned char adc_to_percent(unsigned int adc_value) {
    return (unsigned char)(((unsigned long)adc_value * 100UL) / 1023UL);
}

void display_number(unsigned char number) {
    if (number >= 100) {
        lcd_data('1');
        lcd_data('0');
        lcd_data('0');
    } else {
        lcd_data((number / 10) + '0');
        lcd_data((number % 10) + '0');
        lcd_data(' ');
    }
}

void update_traffic_light(unsigned char speed_percent) {
    GREEN_LED = 0;
    YELLOW_LED = 0;
    RED_LED = 0;
    BUZZER = 0;

    if (!is_running || speed_percent == 0) {
        return;
    }

    if (speed_percent < 34) {
        GREEN_LED = 1;       // Low speed
    } else if (speed_percent < 67) {
        YELLOW_LED = 1;      // Medium speed
    } else {
        RED_LED = 1;         // High/full speed
    }

    if (speed_percent >= 95) {
        BUZZER = 1;          // Warning at maximum speed
    }
}

void update_lcd(unsigned char speed_percent) {
    lcd_set_cursor(1, 1);
    lcd_print("Speed: ");
    display_number(speed_percent);
    lcd_print("%   ");

    lcd_set_cursor(2, 1);
    lcd_print("Dir: ");

    if (!is_running) {
        lcd_print("Stopped ");
    } else if (current_dir == 0) {
        lcd_print("Forward ");
    } else {
        lcd_print("Reverse ");
    }
}

void main(void) {
    unsigned int pot_value = 0;
    unsigned char speed_percent = 0;

    // Port Directions
    TRISA = 0x01; // RA0 as input (Potentiometer)
    TRISB = 0x03; // RB0, RB1 as inputs (Buttons)
    TRISD = 0x00; // RD0/RD1 motor direction, RD2-RD7 LCD

    // RC0, RC1, RC3, RC4 outputs. RC2 is PWM output.
    TRISC0 = 0;
    TRISC1 = 0;
    TRISC3 = 0;
    TRISC4 = 0;

    PORTC = 0x00;
    PORTD = 0x00;

    init_adc();
    init_pwm();
    lcd_init();

    lcd_set_cursor(1, 1);
    lcd_print("Fan Controller");
    lcd_set_cursor(2, 1);
    lcd_print("Ready...");
    __delay_ms(1000);
    lcd_clear();

    while (1) {
        // Handle Start/Stop Button (Toggle)
        if (START_STOP_BTN == 0) {
            __delay_ms(50); // Simple debounce
            if (START_STOP_BTN == 0) {
                is_running = !is_running;
                while (START_STOP_BTN == 0); // Wait for release
            }
        }

        // Handle Direction Button (Toggle)
        if (DIR_BTN == 0) {
            __delay_ms(50); // Simple debounce
            if (DIR_BTN == 0) {
                current_dir = !current_dir;
                while (DIR_BTN == 0); // Wait for release
            }
        }

        if (is_running) {
            pot_value = read_adc();
            speed_percent = adc_to_percent(pot_value);
            set_speed(pot_value);

            if (current_dir == 0) {
                IN1 = 1;
                IN2 = 0; // Forward
            } else {
                IN1 = 0;
                IN2 = 1; // Reverse
            }
        } else {
            pot_value = 0;
            speed_percent = 0;
            set_speed(0); // Stop PWM
            IN1 = 0;
            IN2 = 0;      // Coast to stop
        }

        update_traffic_light(speed_percent);
        update_lcd(speed_percent);
        __delay_ms(150);
    }
}
