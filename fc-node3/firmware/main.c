/**
 * UKHASnet scraper node
 *
 * Jon Sowman 2015
 */

#include <stdio.h>
#include <string.h>

#include <avr/io.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "RFM69.h"
#include "RFM69Config.h"

#include "ds18b20.h"

#define EN_DDR DDRA
#define EN_PIN 3

// Temperature sensor pins
#define DS18B20_VDD_DDR DDRA
#define DS18B20_VDD_PORT PORTA
#define DS18B20_VDD_PIN 7

// Node configuration options
#define NODE_ID     "JH0"
#define HOPS        "2"
#define WAKE_FREQ    1

/** Enable reg by Hi-Z'ing the pin and enable pull up */
#define REG_ENABLE() do { EN_DDR &= ~_BV(EN_PIN); } while(0)

/** Disable the reg by driving low */
#define REG_DISABLE() do { EN_DDR |= _BV(EN_PIN); } while(0)

// Starting seqid
static char seqid = 'a';

// How many times have we woken up?
static uint8_t wakes = WAKE_FREQ;

static char packetbuf[64];
static char* p;

// Get the voltage on the battery terminals in mV
uint16_t get_batt_voltage(void);
void get_temperature(int8_t *dec, int8_t *frac);

int main()
{
    int8_t temp_dec, temp_frac;

    /* disable watchdog */
    wdt_disable();

    /* EN pin should be 0 */
    PORTA &= ~_BV(EN_PIN);

    /* EN on */
    REG_ENABLE();

    /* disable pullups */
    MCUCR |= _BV(PUD);

    // Power down temp sensor
    DS18B20_VDD_DDR |= _BV(DS18B20_VDD_PIN);
    DS18B20_VDD_PORT &= ~_BV(DS18B20_VDD_PIN);

    /* Enable and configure RFM69 */
    while(!rf69_init());
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
            // Add voltage
            sprintf(p, "V%u", get_batt_voltage());
            p += strlen(p);
            // Add temperature
            get_temperature(&temp_dec, &temp_frac);
            sprintf(p, "T%d.%d", temp_dec, temp_frac);
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
            _delay_ms(10);

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
        
        // Wait until voltage detector says we're OK
        /*while((PINB & _BV(2)) == 0);*/

        // then wait a little longer to make sure the cap is charged
        _delay_ms(50);
    }

    return 0;
}

/**
 * Return the temperature from the onboard DS18B20
 * @returns A temperature in degrees C
 */
void get_temperature(int8_t *dec, int8_t *frac)
{
    double d;

    // Turn on sensor power
    DS18B20_VDD_PORT |= _BV(DS18B20_VDD_PIN);
    _delay_ms(10);

    // Convert
    d = ds18b20_gettemp();

    // And power it off again 
    DS18B20_VDD_PORT &= ~_BV(DS18B20_VDD_PIN);

    // Return
    *dec = (int8_t)d;
    *frac = (int8_t)(d*10 - *dec*10);
    if(*frac < 0) *frac = -*frac;
}

/**
 * Return the battery voltage (PA0/ADC0) in mV
 * @returns The voltage in millivolts
 */
uint16_t get_batt_voltage(void)
{
    uint32_t r;

    // Power up ADC
    PRR &= ~_BV(PRADC);

    // Channel 0 selected as default
    // VCC is default reference
    // Use a /8 prescaler to get 125kHz ADC clock from 1MHz core
    ADCSRA |= _BV(ADPS1) | _BV(ADPS0);

    // Enable ADC and start conversion
    ADCSRA |= _BV(ADEN) | _BV(ADSC);

    // Wait until done
    while(!(ADCSRA & _BV(ADIF)));

    // Get result
    r = (uint32_t)ADC;

    // Kill ADC
    ADCSRA &= ~_BV(ADEN);
    PRR |= _BV(PRADC);

    return (uint16_t)((r*3300)/1024);
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
