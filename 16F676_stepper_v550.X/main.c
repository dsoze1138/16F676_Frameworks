/*
 * File:   main.c
 * Author: dan1138
 * Target: PIC16F676
 * Compiler: XC8 v2.35
 * IDE: MPLABX v5.50
 *
 * Created on March 13, 2023, 12:22 PM
 * 
 *                              PIC16F676
 *                    +------------:_:------------+
 *           5v0 -> 1 : VDD                   VSS : 14 <- GND
 *               <> 2 : RA5/T1CKI     PGD/AN0/RA0 : 13 <- PGD/ADC in 0
 *               <> 3 : RA4/AN3/T1G   PGC/AN1/RA1 : 12 <- PGC/ADC in 1
 *           VPP -> 4 : RA3/VPP       INT/AN2/RA2 : 11 <> HOME_SW (0=switch pressed)
 *               <> 5 : RC5               AN4/RC0 : 10 <> Orange PA1
 *               <> 6 : RC4               AN5/RC1 : 9  <> Yellow PB1
 *    Blue   PB2 <> 7 : RC3/AN7           AN6 RC2 : 8  <> Pink   PA2
 *                    +---------------------------:
 *                               DIP-14
 * 
 */

/* define the system clock frequency that this code will setup */
#define _XTAL_FREQ (4000000ul)
/*
 * Initialize the target specific configuration bits
 */
#pragma config FOSC = INTRCIO
#pragma config WDTE = OFF
#pragma config PWRTE = OFF
#pragma config MCLRE = ON 
#pragma config BOREN = OFF
#pragma config CP = OFF
#pragma config CPD = OFF
/*
 * Include target specific definitions
 */
#include <xc.h>
/*
 * Initialize this PIC for the hardware target.
 */
void Init_PIC(void)
{   
    INTCON = 0;     /* turn off interrupts */
    PIE1   = 0;
    
    CMCON = 0x07;       /* turn off comparators */
    VRCON = 0x00;

    ADCON1 = 0x10;      /* set FOSC/8 as ADC clock source */
    ADCON0 = 0x80;      /* Right justified, VREF is VDD, select channel 0 and turn off ADC */
    ANSEL  = 0x03;      /* set RA0,RA1 as analog inputs, all others as digital */
    ADCON0bits.ADON = 1;
    ADCON0bits.GO_nDONE = 1;
    
    __delay_ms(500);    /* wait for ICD before making PGC and PGD outputs */
    
    OPTION_REG = 0x51;  /* Select FOSC/4 as clock source for TIMER0 and 1:4 prescaler, enable pull-ups on PORTA */
    WPUA = 0xFC;        /* Turn off weak pull-ups on RA0,RA1 */
    TRISA = 0xFF;       /* Set PORTA RA0,RA1,RA2,RA4,RA5 as inputs */
    TRISC = 0xF0;       /* Set PORTC RC0,RC1,RC2,RC3 as outputs */
    PORTC = 0;          /* turn off drivers to stepper motor */
    
    /*
     * TIMER0 is setup to clock from FOSC/4 with a 1:4 prescale. The timer will assert
     * an overflow event every (prescale * count length). The TIMER0 count length is fixed 
     * at 256 counts, so the event is asserted every 1024 clocks. This is 1.024 milliseconds
     * based on a 4MHz system clock.
     */
}
/*
 * Interrupt handlers
 */
void __interrupt() ISR_Handler(void)
{
}
/*
 * Step motor
 * 
 * Inputs: Count, 16-bit integer, Positive values set clockwise, negative counterclockwise.
 *         Wait, 8-bit unsigned integer, Number of TIMER0 events to wait until next step.
 * 
 *  Sequence to Rotate in clockwise direction using half steps
 * +------------+-----------------------------------------------+
 * :   Motor    :                     Step                      :
 * +   Wire     :-----+-----+-----+-----+-----+-----+-----+-----+
 * :   Color    :  1  :  2  :  3  :  4  :  5  :  6  :  7  :  8  :
 * +------------+-----+-----+-----+-----+-----+-----+-----+-----+
 * : Orange PA1 : GND : GND : off : off : off : off : off : GND :
 * : Yellow PB1 : off : GND : GND : GND : off : off : off : off :
 * : Pink   PA2 : off : off : off : GND : GND : GND : off : off :
 * : Blue   PB2 : off : off : off : off : off : GND : GND : GND :
 * : Red    COM : 5v0 : 5v0 : 5v0 : 5v0 : 5v0 : 5v0 : 5v0 : 5v0 :
 * +------------+-----+-----+-----+-----+-----+-----+-----+-----+
 */
static const uint8_t HalfSteps[8] = {0x01, 0x03, 0x02, 0x06, 0x04, 0x0C, 0x08, 0x09};

void StepMotor(int16_t Count, uint8_t Wait)
{
    static uint8_t state = 0;
    uint8_t delay;

    do
    {
        if (Count > 0)
        {
            PORTC = HalfSteps[state];   /* drive stepper to select state */
            state++;                    /* step one state clockwise */
            state &= 0x07;              /* keep state within HalfStep table */
            Count--;                    /* update step count */
        }
        else if (Count < 0)
        {
            PORTC = HalfSteps[state];  /* drive stepper to select state */ 
            state--;                   /* step one state counterclockwise */ 
            state &= 0x07;             /* keep state within HalfStep table */ 
            Count++;                   /* update step count */ 
        }
        /* Wait between steps */
        if(Wait > 0) 
        {
            delay = Wait;
            do {
               while(INTCONbits.T0IF == 0) {}
               INTCONbits.T0IF = 0;
            } while (--delay > 0);
        }
    } while (Count != 0);
}

#define HOME_SW PORTAbits.RA2
#define HOME_SW_RELEASED (HOME_SW == 1)

void StepMotorHome(int16_t Count, uint8_t Wait)
{
    static uint8_t state = 0;
    uint8_t delay;
    
    do
    {
        if(HOME_SW_RELEASED) break;     /* exit loop when at home position */
        if (Count > 0)
        {
            PORTC = HalfSteps[state];   /* drive stepper to select state */
            state++;                    /* step one state clockwise */
            state &= 0x07;              /* keep state within HalfStep table */
            Count--;                    /* update step count */
        }
        else if (Count < 0)
        {
            PORTC = HalfSteps[state];  /* drive stepper to select state */ 
            state--;                   /* step one state counterclockwise */ 
            state &= 0x07;             /* keep state within HalfStep table */ 
            Count++;                   /* update step count */ 
        }
        /* Wait between steps */
        if(Wait > 0) 
        {
            delay = Wait;
            do {
               while(INTCONbits.T0IF == 0) {}
               INTCONbits.T0IF = 0;
            } while (--delay > 0);
        }
    } while (Count != 0);
}
/*
 * Main application
 */
void main(void) 
{
    /*
     * Application initialization
     */
    Init_PIC();
    StepMotorHome(-4076, 20);   /* step motor counterclockwise one full revolution or until home position is reached */
    /*
     * Application process loop
     */
    while(1)
    {
        StepMotor(1019, 4);     /* step about 1/4 a revolution clockwise at 4.096 milliseconds per step (about 4 seconds) */
        __delay_ms(500);
        StepMotor(-1019, 2);    /* step about 1/4 a revolution counterclockwise at 2.048 milliseconds per step (about 2 seconds) */
        __delay_ms(250);
    }
}
