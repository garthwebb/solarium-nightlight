#include <avr/io.h>
#include <avr/interrupt.h>

// 12 bit mask, use if paranoid about larger than 12-bit values.
#define BIT_MASK 0x0FFF
#define DARK_THRESH 153
#define NUM_BITS 24

//Define functions
//======================

void ioinit(void);      //Initializes IO
void delay_ms(uint16_t x); //General purpose delay
void delay_us(int x);
void write_data(void);
void blink_status(void);

//======================

// Hold the 8 sets of RGB data
uint16_t data[NUM_BITS];

int main (void) {
	int x, num;

    ioinit(); //Setup IO pins and defaults

	for (x = 0; x <= NUM_BITS; x++) {
		data[x] = 0;
	}
	write_data();

	// Run forever 
    while (1) {

		// Write to all red LEDs, half intensity
		for (x = 0; x <= NUM_BITS; x += 3) {
			data[x] = 0x003F;
			write_data();
			delay_ms(100);
		}

		// Write to all green LEDs, half intensity
		for (x = 1; x <= NUM_BITS; x += 3) {
			data[x] = 0x003F;
			write_data();
			delay_ms(100);
		}

		// Write to all blue LEDs, half intensity
		for (x = 2; x <= NUM_BITS; x += 3) {
			data[x] = 0x003F;
			write_data();
			delay_ms(100);
		}
		
		// Write to all red LEDs, full intensity
		for (x = 0; x <= NUM_BITS; x += 3) {
			data[x] = 0x0FFF;
			write_data();
			delay_ms(100);
		}

		// Write to all green LEDs, full intensity
		for (x = 1; x <= NUM_BITS; x += 3) {
			data[x] = 0x0FFF;
			write_data();
			delay_ms(100);
		}

		// Write to all blue LEDs, full intensity
		for (x = 2; x <= NUM_BITS; x += 3) {
			data[x] = 0x0FFF;
			write_data();
			delay_ms(100);
		}
		
		for (x = 0; x <= NUM_BITS; x++) {
			data[x] = 0;
		}
		write_data();
	}
}

void ioinit (void) {

	// Set all but pin PD3 of port D to be outputs
	DDRD = 0xFF;
	// Set the inital value of port D to be zero
	PORTD = 0;

	TCCR2B = (1<<CS21); //Set Prescaler to 8. CS21=1

	// Enable ADC and set 128 prescale
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

int read_analog(void) {
	ADMUX = 0;						// Channel selection
	ADCSRA |= (1 << ADSC);			// Start conversion
	while (!(ADCSRA & (1<<ADIF)));	// Loop until conversion is complete
	ADCSRA |= (1 << ADIF);			// Clear ADIF by writing a 1 (this sets the value to 0)
 
   return(ADC);
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

	for (x=NUM_BITS; x >= 0; x--) {
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
