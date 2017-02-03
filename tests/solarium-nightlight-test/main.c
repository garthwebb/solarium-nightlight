#include <avr/io.h>
#include <avr/interrupt.h>

// 12 bit mask, use if paranoid about larger than 12-bit values.
#define BIT_MASK 0x3F;
#define DARK_THRESH 153

//Define functions
//======================

void ioinit(void);      //Initializes IO
void delay_ms(uint16_t x); //General purpose delay
void delay_us(int x);
void write_data(void);
void blink_status(void);

void strobe_number(uint8_t num);

//======================

// Hold the 5 sets of RGB data
uint16_t data[16];

uint8_t is_dark = 1;
uint8_t lights_are_on = 1;

int main (void) {
	int x;

    ioinit(); //Setup IO pins and defaults

	blink_status();

	// Run forever 
    while (1) {
/*
		if (ADCSRA & (1 << ADIF)) {
			strobe_number(ADCH);
			delay_ms(500);
			if (ADCH < DARK_THRESH) {
				is_dark = 1;
			} else {
				is_dark = 0;
			}
			ADCSRA |= (1 << ADSC);
		}
*/

    	// Only show the lights if its dark
    	if (is_dark) {
    		// If lights are on, run the show
    		if (lights_are_on) {
				for (x=0; x <= 15; x++) {
					data[x] += 5;
					if (data[x] > 0x0FFF)
						data[x] = 0;
				}
				write_data();
			} else {
				// If the lights aren't on yet, init LED starting values
				for (x=0; x <= 15; x++) {
					data[x] = (0x0FFF*x)/15;
				}
				lights_are_on = 1;
			}
		} else {
			if (lights_are_on) {
				for (x=0; x <= 15; x++) {
					data[x] = 0;
				}
				write_data();
				lights_are_on = 0;
			}
		}
	}
}

void ioinit (void) {

	// Set all but pin PD3 of port D to be outputs
	DDRD = 0xFF;
	// Set the inital value of port D to be zero
	PORTD = 0;

    TCCR2B = (1<<CS21); //Set Prescaler to 8. CS21=1

/*
	ADMUX |= (1 << REFS0); // Set ADC reference to AVCC 
	ADMUX |= (1 << ADLAR); // Left adjust ADC result to allow easy 8 bit reading

	// Set ADC prescaler to 128 - 125KHz sample rate @ 16MHz 
	ADCSRA |= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
	ADCSRA |= (1 << ADEN);  // Enable ADC 
	ADCSRA |= (1 << ADIE);  // Enable ADC Interrupt
	//ADCSRA |= (1 << ADIF);

	ADCSRB = 0; // Set free running mode

	//sei();   // Enable Global Interrupts 

	ADCSRA |= (1 << ADSC);  // Start A2D Conversions
*/
}

void strobe_number(uint8_t num) {
	int x;
	
	for (x=1; x <= 0x80; x = x << 1) {
		PORTB |= 0x04; // Set 'clock' bit to one
		if (num & x) {
			PORTB |= 0x02; // Set bit two to one
		} else {
			PORTB &= 0xFD; // Set bit two to zero
		}
		PORTB &= 0xFB; // Set 'clock bit to zero
	}
}

ISR(ADC_vect) {
	blink_status;
	if (ADCH < DARK_THRESH) {
		is_dark = 1;
	} else {
		is_dark = 0;
	} 
}

void blink_status (void) {
	delay_ms(25);
	PORTD = 0x10;
	delay_ms(25);
	PORTD = 0x00;
}

//General short delays
void delay_us(int x) {
	int y, z, a;
	
	y = x/256;
	z = x - y * 256;
	
	for (a = 0; a < y; a++)
	{
		TIFR2 |= 0x01;//Clear any interrupt flags on Timer2
		
		TCNT2 = 0; //256 - 125 = 131 : Preload timer 2 for x clicks. Should be 1us per click
	
		while(!(TIFR2 & 0x01));
		
	}
	
	TIFR2 |= 0x01;//Clear any interrupt flags on Timer2
	
	TCNT2 = 256-z; //256 - 125 = 131 : Preload timer 2 for x clicks. Should be 1us per click

	while(!(TIFR2 & 0x01));
	
}

// General short delays
void delay_ms(uint16_t x) {
	for (; x > 0 ; x--) {
        delay_us(250);
        delay_us(250);
        delay_us(250);
        delay_us(250);
    }
}

void write_data (void) {
	int x;
	uint16_t mask;
	uint16_t val;

	// Start the clock at zero
	PORTD = 0;

	for (x=15; x >= 0; x--) {
		val = data[x];
		for (mask = 0x0800; mask > 0; mask = mask >> 1) {
			if (val & mask) {
				PORTD = 0x02;
			} else {
				PORTD = 0x00;
			}
			// Pulse the clock twice to get a rise then fall
			PORTD++;
			PORTD--;
		}
	}

	// Pulse the XLAT line to latch in the data
	PORTD = 0x04;
	PORTD = 0x00;

	// Pulse the clock twice to get a rise then fall
	PORTD++;
	PORTD--;
}
