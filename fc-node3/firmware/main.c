/**
 * UKHASnet film canister node
 *
 * A node that sleeps using a reservoir capacitor, so that a boost regulator
 * only runs around 0.1% of the time. Runs for ages on a single AA/AAA.
 *
 * The MCP1640 boost regulator is capable of running a cell down to 0.35V but
 * will only start up from a cell >0.8V (worse case). So once the cell voltage
 * falls below POWER_MODE_WDT_THRESH, the reg is left enabled and the device
 * sleeps on the watchdog timer in order to maximally drain the cell.
 *
 * Jon Sowman 2015-18
 * jon+github@jonsowman.com
 *
 * https://ukhas.net
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <avr/io.h>
#include <util/delay.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>

#include "RFM69.h"
#include "RFM69Config.h"

#include "ds18b20.h"

/* Node configuration options */
#define NODE_ID         "JH9"
#define HOPS            "1"
#define WAKE_FREQ       5
#define TX_POWER_DBM    10

/* Move into MODE_WDT when the battery voltage falls below (mV) */
#define POWER_MODE_WDT_THRESH  1350
#define POWER_MODE_WDT_HYST      50

/* Regulator enable pin */
#define EN_DDR  DDRA
#define EN_PORT PORTA
#define EN_PIN  3

/* Temperature sensor pins */
#define DS18B20_VDD_DDR     DDRA
#define DS18B20_VDD_PORT    PORTA
#define DS18B20_VDD_PIN     7

/* Enable reg by Hi-Z'ing the pin and enable pull up */
#define REG_ENABLE() do { EN_DDR &= ~_BV(EN_PIN); } while(0)

/* Disable the reg by driving low */
#define REG_DISABLE() do { EN_DDR |= _BV(EN_PIN); } while(0)

/**
 * Enumerate the various sleep modes this device support */
typedef enum power_mode_t {
    MODE_WDT = 0,
    MODE_BOOSTOFF,
    NUM_POWER_MODES
} power_mode_t;

/* Starting sequence ID */
static char seqid = 'a';

/* How many times have we woken up? */
static uint8_t wakes = WAKE_FREQ;

/* UKHASnet packet buffer and pointer */
static char packetbuf[64];
static char *p;
static uint16_t batt_mv;

/* Track power saving mode */
static power_mode_t power_mode = MODE_BOOSTOFF;

/* Get the voltage on the battery terminals in mV */
uint16_t get_batt_voltage(void);
float get_temperature(void);

/* Main loop */
int main(void)
{
    /* Disable watchdog */
    wdt_disable();

    /* Enable global interrupts */
    sei();

    /* Wait for cap to charge */
    _delay_ms(1000);

    /* EN pin should be 0 */
    EN_PORT &= ~_BV(EN_PIN);
    
    /* EN on */
    REG_ENABLE();

    /* Disable pullups */
    MCUCR |= _BV(PUD);

    /* Power down temp sensor */
    DS18B20_VDD_DDR |= _BV(DS18B20_VDD_PIN);
    DS18B20_VDD_PORT &= ~_BV(DS18B20_VDD_PIN);

    /* Enable and configure RFM69 */
    while(!rf69_init());
    rf69_setMode(RFM69_MODE_SLEEP);

    /* All periphs off */
    PRR |= _BV(PRTIM0) | _BV(PRUSI) | _BV(PRADC);

    /* Main loop of sleeping and transmitting */
    while(1)
    {
        /* Wakes will be roughly every 30sec depending on exact hardware 
         * and climate conditions */
        if(wakes == WAKE_FREQ)
        {
            /* Construct and send the packet. A packet looks like
             <HOPS><SEQID>VxxxxTyy.yXa,b,c[<NODEID>]
            where:
            <HOPS> is as defined at top of this file
            <SEQID> is a sequence ID, 'a' at startup, running 'b'-'z' after
            Vxxxx is the battery voltage in millivolts
            Tyy.y is the temperature in decimal degrees 
            Xa,b,c is a custom field:
                a: WAKE_FREQ
                b: TX_POWER_DBM
                c: power_mode (0=MODE_WDT, 1=MODE_BOOSTOFF)
            <NODEID> is as configured at the top of this file
            */
            /* Reset pointer to beginning of packet buffer */
            p = packetbuf;

            /* Number of hops */
            strcpy(p, HOPS);
            p += strlen(p);

            /* Add sequence ID */
            *p++ = seqid;

            /* Add voltage */
            *p++ = 'V';
            batt_mv = get_batt_voltage();
            utoa(batt_mv, p, 10);
            p += strlen(p);

            /* Add temperature */
            *p++ = 'T';
            dtostrf(get_temperature(), 1, 1, p);
            p += strlen(p);

            /* Add wake freq, tx power and power save mode */
            *p++ = 'X';
            utoa(WAKE_FREQ, p, 10);
            p += strlen(p);
            *p++ = ',';
            utoa(TX_POWER_DBM, p, 10);
            p += strlen(p);
            *p++ = ',';
            utoa(power_mode, p, 10);
            p += strlen(p);

            /* Add node ID in [] */
            *p++ = '[';
            strcpy(p, NODE_ID);
            p += strlen(p);
            *p++ = ']';

            /* Null terminate */
            *p = '\0';

            /* Send the packet */
            rf69_send((uint8_t*)packetbuf, strlen(packetbuf), TX_POWER_DBM); 

            /* Delay to allow the cap to recharge a bit extra after tx,
             * since it takes a little while after rf69_send() exits
             * for the PA to fully turn off and stop drawing current */
            _delay_ms(10);

            /* Reset the number of wakes */
            wakes = 1;

            /* Increase the sequence ID for the next time we enter here */
            if(seqid == 'z')
                seqid = 'b';
            else
                seqid++;

            /* Update the power mode */
            if( power_mode == MODE_BOOSTOFF 
                    && batt_mv < POWER_MODE_WDT_THRESH )
                /* Battery fallen below threshold, move to MODE_WDT */
                power_mode = MODE_WDT;
            else if( power_mode == MODE_WDT 
                    && batt_mv > (POWER_MODE_WDT_THRESH + POWER_MODE_WDT_HYST) )
                /* Battery is above (threshold+hysteresis), move to 
                 * MODE_BOOSTOFF. */
                power_mode = MODE_BOOSTOFF;
        } /* End of the waking loop - go back to sleep */
        else
        {
            /* Not time to wake up, go back to sleep */
            wakes++;
        }

        /* What we do now depends on the power save mode */
        if( power_mode == MODE_BOOSTOFF )
        {
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

            /* Then wait a little longer to make sure the cap is charged */
            _delay_ms(50);
        } else {
            /* Enable the watchdog and sleep for 8 seconds */
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_enable();
            /* 8x8 = 64 seconds which is roughly one 'wake' */
            for(uint8_t sleeps = 0; sleeps < 8; sleeps++)
            {
                wdt_enable(WDTO_8S);
                WDTCSR |= (1 << WDIE);
                sleep_cpu();
            }
            sleep_disable();
        }
    }

    return 0;
} /* Main application loop -- never leave here */

/**
 * Return the temperature from the onboard DS18B20 to precision 0.1degC
 * @returns the temperature in degrees C.
 */
float get_temperature(void)
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
    return d;
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

/* Turn on reg */
ISR(EXT_INT0_vect)
{
    // Voltage low, enable the reg
    REG_ENABLE();
}

/* Watchdog interrupt */
ISR(WATCHDOG_vect)
{
    wdt_disable();
}
