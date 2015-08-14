/**
 * UKHASnet scraper node
 *
 * Jon Sowman 2015
 */

#include <avr/io.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#define EN_DDR DDRA
#define EN_PIN 3

/** Enable reg by Hi-Z'ing the pin and enable pull up */
#define REG_ENABLE() do { EN_DDR &= ~_BV(EN_PIN); } while(0)

/** Disable the reg by driving low */
#define REG_DISABLE() do { EN_DDR |= _BV(EN_PIN); } while(0)

int main()
{
    /* disable watchdog */
    wdt_disable();

    /* EN pin should be 0 */
    PORTA &= ~_BV(EN_PIN);

    /* EN on */
    REG_ENABLE();

    /* disable pullups */
    MCUCR |= _BV(PUD);

    /* All periphs off */
    PRR |= _BV(PRTIM0) | _BV(PRUSI) | _BV(PRADC);

    while(1)
    {
        /* Interrupt on INT0 low level */
        MCUCR &= ~(_BV(ISC01) | _BV(ISC00));
        GIMSK |= _BV(INT0);

        /* And sleep ZzZzZ */
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        /* turn off reg and sleep */
        REG_DISABLE();
        sei();
        sleep_cpu();
        cli();
        GIMSK = 0x00;
        sleep_disable();
        /* Need about 5ms for the cap to charge back up */
        _delay_ms(100);
    }

    return 0;
}

/* turn on reg */
ISR(INT0_vect)
{
    // Voltage low, enable the reg
    REG_ENABLE();
}
