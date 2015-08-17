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
#include <string.h>

#include "RFM69.h"
#include "RFM69Config.h"

#define EN_DDR DDRA
#define EN_PIN 3

// Node configuration options
#define NODE_ID     "JF0"
#define HOPS        "2"
#define WAKE_FREQ    1

/** Enable reg by Hi-Z'ing the pin and enable pull up */
#define REG_ENABLE() do { EN_DDR &= ~_BV(EN_PIN); } while(0)

/** Disable the reg by driving low */
#define REG_DISABLE() do { EN_DDR |= _BV(EN_PIN); } while(0)

// Starting seqid
static char seqid = 'a';

// How many times have we woken up?
static uint8_t wakes = 1;

static char packetbuf[64];
static char* p;

static char dummytemp[] = "T25.0";

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

    DDRA |= _BV(7);
    PORTA &= ~_BV(7);

    /* Enable and configure RFM69 */
    rf69_init();
    rf69_setMode(RFM69_MODE_SLEEP);

    /* All periphs off */
    PRR |= _BV(PRTIM0) | _BV(PRUSI) | _BV(PRADC);

    while(1)
    {
        if(wakes == WAKE_FREQ)
        {
            // Construct and send the packet
            p = packetbuf;
            // Add number of hops
            strcpy(p, HOPS);
            p += strlen(p);
            // Add seqid
            sprintf(p, "%c", seqid);
            p += strlen(p);
            // Add temperature
            strcpy(p, dummytemp);
            p += strlen(p);
            // Add node ID in []
            *p++ = '[';
            strcpy(p, NODE_ID);
            p += strlen(p);
            *p++ = ']';
            // Null terminate
            *p = '\0';

            // Send the packet
            rf69_send((uint8_t*)packetbuf, strlen(packetbuf), 10); 
            // Delay to allow the cap to recharge a bit extra after tx,
            // since it takes a little while after rf69_send() exits
            // for the PA to fully turn off and stop drawing current
            _delay_ms(5);

            // Reset the number of wakes
            wakes = 1;

            // Increase the sequence ID for the next time we enter here
            if(seqid == 'z')
                seqid = 'b';
            else
                seqid++;
        }
        else
        {
            wakes++;
        }


        // Interrupt on INT0 low level
        MCUCR &= ~(_BV(ISC01) | _BV(ISC00));
        GIMSK |= _BV(INT0);

        // And sleep ZzZzZ
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        // turn off reg and sleep
        REG_DISABLE();
        sei();
        sleep_cpu();
        cli();
        GIMSK = 0x00;
        sleep_disable();


        /*
        // Enable the watchdog and sleep for 8 seconds
        wdt_enable(WDTO_8S);
        WDTCSR |= (1 << WDIE);
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sei();
        sleep_cpu();
        sleep_disable();

        */
        
        // Wait for cap to recharge
        _delay_ms(5);
    }

    return 0;
}

/* turn on reg */
ISR(EXT_INT0_vect)
{
    // Voltage low, enable the reg
    REG_ENABLE();
}

/**
 *  * Watchdog interrupt
 *   */
ISR(WATCHDOG_vect)
{
    wdt_disable();
}
