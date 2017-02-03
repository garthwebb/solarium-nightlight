#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

#define NUM_BITS 24
#define NUM_PROGRAMS 8

#define SWITCH_OFF()   (PIND & (1 << PIND4))
#define SWITCH_SENSE() (PIND & (1 << PIND5))
#define SWITCH_ON()    (PIND & (1 << PIND6))

#define RED_VAL(led)       (data[led*3+1])
#define GREEN_VAL(led)     (data[led*3+2])
#define BLUE_VAL(led)      (data[led*3])
#define SET_LED(led,r,g,b) data[led*3]=b; data[led*3+1]=r; data[led*3+2]=g;
#define SET_IDX(idx,r,g,b) data[idx]=b; data[idx+1]=r; data[idx+2]=g;

// Define functions
//======================

void io_init(void);         // Initializes IO
void interrupt_init(void);  // Initialize the interrupts

void delay_ms(uint16_t x);  // General purpose delay
void delay_us(int x);

void write_data(void);
void clear_lights(void);
void cycle (uint16_t *vals, uint16_t step, uint16_t ceiling);
void rgb2hsv (uint16_t r, uint16_t g, uint16_t b, float *h, float *s, float *v);
void hsv2rgb (float h, float s, float v, uint16_t *r, uint16_t *g, uint16_t *b);
uint16_t max3 (uint16_t a, uint16_t b, uint16_t c);
uint16_t min3 (uint16_t a, uint16_t b, uint16_t c);

// Programs
void sun_show_prog(int init, float level);
void xmas_ball_prog (int init, float level);
void spaceship_prog (int init, float level);
void color_cycle_prog(int init, float level);

void led_test_prog (int init);

//======================

/*
    Default LED positions; (bottom) and <top>

    (4)   (7)
    <5>   <6>
    
    (2)   (1)
    <3>   <0>
*/

/*
   Ordering for sun_order

   (1)   (0)
   <7>   <6>
         
   (3)   (2)
   <5>   <4>
*/

/* LEDs are ordered Blue, Red Green, e.g.:

	data[3*x]   = blue_value;
	data[3*x+1] = red_value;
	data[3*x+2] = green_value

   Where 'x' is one of the 8 RGB LEDs, 0 - 7.

*/

int adc_order[8] = {0*3, 3*3, 5*3, 6*3, 1*3, 2*3, 4*3, 7*3};

// These are the starting indexes of the LEDs in the order
// we want to light for the sun show
int sun_order[8] = {7*3, 4*3, 1*3, 2*3, 0*3, 3*3, 6*3, 5*3};

// Hold the 8 sets of RGB data
uint16_t data[NUM_BITS];

// Data that changes during interrupts
volatile int cur_program = 0;

// Flags to let the program know that data changed
volatile int prog_change = 1;

int last_state; // 0 - off, 1 - light sense, 2 - on

int init_prog = 0;
int waiting_on_adc = 0;
int sense_on = 0;
int adc_num = 0;

int main (void) {


	// Setup IO pins and defaults
	io_init();

	// Blank out the lights
	clear_lights();

	// Enable interrupts
	interrupt_init();

	// Run forever 
    while (1) {
    	// No matter what the state change is, clear the lights
		if (prog_change) {
			// Reset the state change flag
			prog_change = 0;

			// Clear the lights
			clear_lights();

			// Let programs know to initialize
			init_prog = 1;

			// Eliminate bounce in the switch
			delay_ms(500);
			
			// Blink to let the user know whether we're on the bright or dim setting
			if (cur_program > 3) {
				data[0] = 0xFFF;
				write_data();
				delay_ms(400);
				data[0] = 0x000;
				write_data();
			} else {
				data[1] = 0xFFF;
				write_data();
				delay_ms(400);
				data[1] = 0x000;
				write_data();
			}
		}
		
		// Reenable interrupts on the push button
		PCMSK2 |= (1 << PCINT23);

		// If we're off, light an LED for now
		if (SWITCH_SENSE() || SWITCH_ON()) {
			// If we were just off, set the lights to all off
			if (last_state == 0) {
				clear_lights();
			}

			// Nothing special when switching to full on
			if (SWITCH_ON())
				last_state = 2;

			// On switch sense, make sure to poll the ADC register
			if (SWITCH_SENSE()) {
				// If we weren't in sense mode before, start the ADC conversions
				if (last_state != 1) {
					clear_lights();
					write_data();

					// Clear ADIF by writing a 1 (this sets the value to 0)
					ADCSRA |= (1 << ADIF);
					ADMUX = 0;				// Channel selection
					ADCSRA |= (1 << ADSC);	// Start new conversion
				}
				last_state = 1;

				// See if there is an ADC conversion done
				if (ADCSRA & (1<<ADIF)) {
					// Clear ADIF by writing a 1 (this sets the value to 0)
					ADCSRA |= (1 << ADIF);
					
					// Read the current value
					adc_num = ADC;

					ADMUX = 0;				// Channel selection
					ADCSRA |= (1 << ADSC);	// Start new conversion
				}

				// If its too bright, don't show anything
				if (adc_num > 260) {
					clear_lights();
					write_data();
					continue;
				}
			}

			switch (cur_program) {
			case 0 :
				sun_show_prog(init_prog, 1.0);
				break;
			case 1 :
				spaceship_prog(init_prog, 1.0);
				break;
			case 2 :
				xmas_ball_prog(init_prog, 1.0);
				break;
			case 3 :
				color_cycle_prog(init_prog, 1.0);
				break;
			case 4 :
				sun_show_prog(init_prog, 0.5);
				break;
			case 5 :
				spaceship_prog(init_prog, 0.5);
				break;
			case 6 :
				xmas_ball_prog(init_prog, 0.5);
				break;
			case 7 :
				color_cycle_prog(init_prog, 0.5);
				break;
			}

			write_data();
		} else {
			last_state = 0;
		}

		init_prog = 0;
	}
}

void io_init (void) {

	// Set PD0-PD3 to outputs and PD4-PD7 to inputs on port D
	DDRD = (1 << PD0) | (1 << PD1) | (1 << PD2) | (1 << PD3);
	// Set the inital value of port D to be zero
	PORTD = 0;

	TCCR2B = (1<<CS21); //Set Prescaler to 8. CS21=1

	// Enable ADC and set 128 prescale
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

void interrupt_init (void) {

	// Enable interrupts for the pushbutton and slide switch
	// PCICR = Pin Change Interrupt Control Register
	// PCIE2 interrupts PCINT[16:23]
	PCICR |= (1 << PCIE2);

	// Select PCINT23 as an interrupt pin
	PCMSK2 |= (1 << PCINT23);

	// Enable Global Interrupts
	sei();
}

ISR(PCINT2_vect) {
	// Disable interrupts to avoid bounce.  Reenable later
	PCMSK2 &= ~(1 << PCINT23);

	// Advance to the next program
	if (PIND & (1 << PIND7)) {
		prog_change = 1;
		cur_program = (cur_program + 1) % NUM_PROGRAMS;
	}
}

void clear_lights (void) {
	int x;

	for (x = 0; x <= NUM_BITS; x++) {
		data[x] = 0;
	}
	write_data();
}

/*

 === Spaceship Prog ===

*/

#define HUE 0
#define SAT 1
#define VAL 2

#define SS_VAL_MAX 1.0
#define SS_DELAY_MAX 10

int spaceship_cycles[2][4] = {
	{5*3, 6*3, 0*3, 3*3},
	{1*3, 7*3, 4*3, 2*3},
};

int ss_delay = SS_DELAY_MAX;

// Where the current lead light is
int top_cycle=0;
int bot_cycle=2;
int top_cycle_incr=0;
int bot_cycle_incr=0;

float ss_color[2][4][3] = {
	{{1.0, 1.0, 0}, {1.0, 1.0, 0}, {1.0, 1.0, 0}, {1.0, 1.0, 0}},
	{{0.5, 1.0, 0}, {0.5, 1.0, 0}, {0.5, 1.0, 0}, {0.5, 1.0, 0}},
};

float ss_hue = SS_VAL_MAX;
float ss_hue_bot = 0.5;

float ss_val = 1.0;
float ss_val_bot = 0;
float ss_val_step = 0.0004;

// Number of steps to get to full speed
int num_steps = 50;

// Initial step size
float ss_step = 0.004;

void spaceship_prog (int init, float level) {
	if (init) {
		clear_lights();

		// If we decrease the val, increase the delay
		ss_val = SS_VAL_MAX * level;
		ss_delay = SS_DELAY_MAX / level;
	}

	// TOP CYCLE
	if (ss_color[0][top_cycle][VAL] >= ss_val) {
		// Make sure trailing lights value is zero
		ss_color[0][(top_cycle+3)%4][VAL] = 0.0;
		// Move to the next light.  Don't increment here because we need to write
		// the zero to the trailing light
		top_cycle_incr = 1;
	} else {
		ss_color[0][top_cycle][VAL] += 0.004;
		if (ss_color[0][top_cycle][VAL] > 0xFFF)
			ss_color[0][top_cycle][VAL] = 0xFFF;

		if (ss_color[0][(top_cycle+3)%4][VAL]-0.004 > 0) {
			ss_color[0][(top_cycle+3)%4][VAL] -= 0.004;
		} else {
			ss_color[0][(top_cycle+3)%4][VAL] = 0.0;
		}
	}

	hsv2rgb(ss_hue,
			ss_color[0][top_cycle][SAT],
			ss_color[0][top_cycle][VAL],
			&data[spaceship_cycles[0][top_cycle]],
			&data[spaceship_cycles[0][top_cycle]+1],
			&data[spaceship_cycles[0][top_cycle]+2]);

	hsv2rgb(ss_hue,
			ss_color[0][(top_cycle+3)%4][SAT],
			ss_color[0][(top_cycle+3)%4][VAL],
			&data[spaceship_cycles[0][(top_cycle+3)%4]],
			&data[spaceship_cycles[0][(top_cycle+3)%4]+1],
			&data[spaceship_cycles[0][(top_cycle+3)%4]+2]);

	// BOTTOM CYCLE
	if (ss_color[1][bot_cycle][VAL] >= ss_val_bot) {
		// Make sure trailing lights value is zero
		ss_color[1][(bot_cycle+3)%4][VAL] = 0.0;
		// Move to the next light.  Don't increment here because we need to write
		// the zero to the trailing light
		bot_cycle_incr = 1;
	} else {
		ss_color[1][bot_cycle][VAL] += 0.004;
		if (ss_color[0][top_cycle][VAL] > 0xFFF)
			ss_color[0][top_cycle][VAL] = 0xFFF;

		if (ss_color[1][(bot_cycle+3)%4][VAL]-0.004 > 0) {
			ss_color[1][(bot_cycle+3)%4][VAL] -= 0.004;
		} else {
			ss_color[1][(bot_cycle+3)%4][VAL] = 0.0;
		}
	}

	hsv2rgb(ss_hue_bot,
			ss_color[1][bot_cycle][SAT],
			ss_color[1][bot_cycle][VAL],
			&data[spaceship_cycles[1][bot_cycle]],
			&data[spaceship_cycles[1][bot_cycle]+1],
			&data[spaceship_cycles[1][bot_cycle]+2]);


	hsv2rgb(ss_hue_bot,
			ss_color[1][(bot_cycle+3)%4][SAT],
			ss_color[1][(bot_cycle+3)%4][VAL],
			&data[spaceship_cycles[1][(bot_cycle+3)%4]],
			&data[spaceship_cycles[1][(bot_cycle+3)%4]+1],
			&data[spaceship_cycles[1][(bot_cycle+3)%4]+2]);

	if (ss_hue + 0.0004 > 1.0) {
		ss_hue = 0.0;
	} else {
		ss_hue += 0.0004;
	}

	ss_hue_bot = ss_hue + 0.5;
	if (ss_hue_bot > 1)
		ss_hue_bot -= 1;

	if (ss_val + ss_val_step > 1.0) {
		ss_val = 1.0;
		ss_val_step *= -1;
	} else if (ss_val + ss_val_step < 0) {
		ss_val = 0.0;
		ss_val_step *= -1;
	} else {
		ss_val += ss_val_step;
	}

	ss_val_bot = 1-ss_val;

	// Move to the next top light
	if (top_cycle_incr) {
		// Move to the next light
		top_cycle = (top_cycle+1)%4;
		top_cycle_incr = 0;
	}
	// Move to the next bottom light
	if (bot_cycle_incr) {
		// Move to the next light
		bot_cycle = (bot_cycle+1)%4;
		bot_cycle_incr = 0;
	}

	write_data();
	delay_ms(ss_delay);
}

/* 

 === XMAS BALL PROG ===

*/

#define XBALL_LIGHT_LIMIT 0xFFF
#define XBALL_DELAY_LIMIT 5

int xball_delay = XBALL_DELAY_LIMIT;

// Current intensity of the light
uint16_t xball_light_level[3] = {0x000, 0x000, 0x000};

// The color to cycle through (r=0, g=1, b=2)
int xball_light_color = 0;

// How much to raise the current level per cycle
uint16_t xball_light_step  = 0x001;

// The maximum light level
uint16_t xball_light_max   = XBALL_LIGHT_LIMIT;

// The set of lights to illuminate
int xball_light_set = 0;

// Whether we're in the white phase of the program
int xball_phase = 0;

// Current level of the white phase
uint16_t xball_white_level = 0x000;

// This pattern lights LEDs 1,3,4,6 together and 0,2,5,7 together 
int xmas_ball_sets[2][4] = {
	{1*3, 3*3, 4*3, 6*3},
	{0*3, 2*3, 5*3, 7*3},
};

void xmas_ball_prog (int init, float level) {
	if (init) {
		clear_lights();
		xball_light_max = XBALL_LIGHT_LIMIT * level;
		xball_delay = XBALL_DELAY_LIMIT/level;
	}
	int x;

	// Phase 1; warm up the color
	if (xball_phase == 0) {
		for (x=0; x <= 3; x++) {
			SET_IDX(xmas_ball_sets[xball_light_set][x],
					xball_light_level[0],
					xball_light_level[1],
					xball_light_level[2]);
		}

		xball_light_level[xball_light_color] += xball_light_step;

		if (xball_light_level[xball_light_color] > xball_light_max) {
			xball_phase = 1;
		}
	}
	// Phase 2; warm up the white
	else if (xball_phase == 1) {
		for (x=0; x <= 3; x++) {
			SET_IDX(xmas_ball_sets[(xball_light_set+1)%2][x],
					xball_white_level,
					xball_white_level,
					xball_white_level);
		}

		xball_white_level += xball_light_step;

		if (xball_white_level > xball_light_max) {
			xball_phase = 2;
		}
	} else {
		for (x=0; x <= 3; x++) {
			SET_IDX(xmas_ball_sets[xball_light_set][x],
					xball_light_level[0],
					xball_light_level[1],
					xball_light_level[2]);
			SET_IDX(xmas_ball_sets[(xball_light_set+1)%2][x],
					xball_white_level,
					xball_white_level,
					xball_white_level);
		}

		xball_light_level[xball_light_color] -= xball_light_step*4;
		xball_white_level -= xball_light_step*4;
		
		if (xball_light_level[xball_light_color] < 0.0)
			xball_light_level[xball_light_color] = 0.0;
		if (xball_white_level < 0.0)
			xball_white_level = 0;

		if (xball_light_level[xball_light_color] == 0 && xball_white_level == 0) {
			for (int x=0; x <= 3; x++) {
				SET_IDX(xmas_ball_sets[xball_light_set][x], 0x000, 0x000, 0x000);
			}
			xball_light_level[0] = xball_light_level[1] = xball_light_level[2] = 0;
			xball_light_set = (xball_light_set + 1) % 2;
			xball_light_color = (xball_light_color + 1) % 3;

			xball_white_level = 0;
			xball_phase = 0;
		}	
	}

	delay_ms(xball_delay);
	write_data();
}

/* 

 === Sun Show Prog ===

*/

//9
#define DAY_SEGMENTS 10
//5000
#define DAY_FRAMES 5000
#define HOUR_INTERVAL (float) DAY_FRAMES/DAY_SEGMENTS

// Starting from the bottom, 2 LED "rings" of light
int sun_rings[4][2] = {
	{4*3, 1*3},
	{7*3, 2*3},
	{5*3, 0*3},
	{3*3, 6*3},
};

// For sun_show3_prog
// Hours: 0, 3, 6, 9, 12, 15, 18, 21
// Four bands each with these hour ranges
// Each hour includes an HSV value for the color of that band at that hour
// Format is:
// {{band 1, hour 0 HSV}, {band 1, hour 3 HSV}, {...},  ...}},
// {{band 2, hour 0 HSV}, ... }},
// ...

/* Blank slate
float sun_bands[4][8][3] = {
	{{/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00},
	 {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}},
	{{/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00},
	 {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}},
	{{/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00},
	 {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}},
	{{/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00},
	 {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}, {/360.0, 1.00, 0.00}},
};
*/

// Green is about 60/360 -> 180/360.  Avoid this range

float sun_bands[4][10][3] = {
	{{294/360.0, 0.980, 0.200}, {250/360.0, 0.980, 0.100}, {250/360.0, 0.980, 0.150}, {357/360.0, 1.000, 0.540}, {358/360.0, 1.000, 1.000},
	 { 11/360.0, 0.800, 1.000}, { 21/360.0, 0.450, 1.000}, { 24/360.0, 0.140, 1.000}, {256/360.0, 0.190, 0.300}, {268/360.0, 0.530, 0.150}},
	{{258/360.0, 0.900, 0.170}, {250/360.0, 0.980, 0.100}, {250/360.0, 0.980, 0.150}, { 20/360.0, 0.960, 0.240}, { 29/360.0, 0.830, 1.000},
	 {54/360.0,  0.980, 1.000}, { 24/360.0, 0.110, 1.000}, {256/360.0, 0.140, 1.000}, {245/360.0, 0.710, 0.460}, {283/360.0, 0.940, 0.200}},
	{{ 12/360.0, 1.000, 0.170}, {250/360.0, 0.980, 0.100}, {250/360.0, 0.980, 0.150}, {310/360.0, 1.000, 0.160}, {246/360.0, 0.480, 0.570},
	 {250/360.0, 0.400, 0.710}, {255/360.0, 0.120, 1.000}, {288/360.0, 0.040, 1.000}, { 48/360.0, 1.000, 1.000}, { 10/360.0, 1.000, 0.720}},
	{{333/360.0, 0.990, 0.110}, {250/360.0, 0.980, 0.100}, {250/360.0, 0.980, 0.150}, {269/360.0, 1.000, 0.390}, {232/360.0, 1.000, 0.410},
	 {239/360.0, 0.620, 0.490}, {241/360.0, 0.150, 1.000}, {310/360.0, 0.070, 1.000}, { 20/360.0, 0.970, 1.000}, { 15/360.0, 1.000, 0.440}},
};

// To not deal with floating point numbers, assign 1000 frames between each defined hour (3, 6, etc)
// so there's 8000 (see DAY_FRAMES) updates per 'day'; 0-999 is 12am - 3am, 1000-1999 is 3am - 6am, etc.
int day_counter = 0;

void sun_show_prog (int init, float level) {
	if (init) {
		clear_lights();
	}

	// Get the starting hour by dividing by the hour interval and converting it
	// to an integer, e.g. 4000/1000 = hour 4 == 9am
	int start_hour  = day_counter/((float) HOUR_INTERVAL);

	// This is what day_counter would be at the top of the last hour
	int start_count = HOUR_INTERVAL*start_hour;

	// This is the next hour
	int end_hour    = (start_hour+1)%DAY_SEGMENTS;

	// How far into the hour have we progressed
	float progress = (day_counter-start_count)/((float) HOUR_INTERVAL);

	// For each beginning and end HSV and each band, calculate the current HSV
	float h1, h2, s1, s2, v1, v2;
	float h, s, v;
	uint16_t r, g, b;

	for (int band = 0; band <= 3; band++) {
		h1 = sun_bands[band][start_hour][0];
		h2 = sun_bands[band][end_hour][0];
		s1 = sun_bands[band][start_hour][1];
		s2 = sun_bands[band][end_hour][1];
		v1 = sun_bands[band][start_hour][2];
		v2 = sun_bands[band][end_hour][2];

		//$h1+(($h2-$h1)*$p)
		
		// If we'd have to sweep through GREEN for H1 to get to H2, go the opposite
		// way around the color wheel.  60/360 is where green starts (~ 0.1666...)
		// and ends at about 180/360 (~ 0.5)
		if ((h2 < h1) && (h2 < 0.16) && (h1 > 0.5)) {
			h = h1+( ( (h2+1)-h1 )*progress );
			if (h > 1.0)
				h -= 1.0;
		// Likewise, if our sweep starts low, below the green belt and goes high
		// to above green, take the other way around the color circle
		} else if ((h1 < h2) && (h2 > 0.5) && (h1 < 0.16)) {
			h = h1+( ( h2-(h1+1) )*progress );
			if (h < 0.0)
				h += 1.0;
		} else {
			h = h1+((h2-h1)*progress);
		}
		s = s1+((s2-s1)*progress);
		v = v1+((v2-v1)*progress);

		hsv2rgb(h, s, v, &r, &g, &b);

		// Each ring has two LEDs.  Set the RGB for each
		data[sun_rings[band][0]+0] = b;
		data[sun_rings[band][1]+0] = b;
		data[sun_rings[band][0]+1] = r;
		data[sun_rings[band][1]+1] = r;
		data[sun_rings[band][0]+2] = g;
		data[sun_rings[band][1]+2] = g;
	}

	write_data();

	// Increase the counter and wrap around at DAY_FRAMES frames.
	day_counter = (day_counter+1)%DAY_FRAMES;

	delay_ms(20);
}

#define COLOR_CYCLE_STEP 1
#define COLOR_CYCLE_MAX_VAL 0xFFF
#define COLOR_CYCLE_VAL_MAX 1.0

float hue = 0.00;
float sat = 1.00;
float val = 1.00;

float hue_step = 0.001;

void color_cycle_prog (int init, float level) {
	if (init) {
		clear_lights();
		val = COLOR_CYCLE_VAL_MAX * level;
	}

	int x;
	uint16_t r, g, b;

	hsv2rgb(hue, sat, val, &r, &g, &b);

	for (x=0; x <= 7; x++) {
		SET_LED(x, r, g, b);
	}
	
	hue += hue_step;
	if (hue > 1.0)
		hue -= 1.0;
	write_data();
	delay_ms(50);
}

void led_test_prog (int init) {
	if (init) {
		clear_lights();
		data[0] = 0x0FF;
	}

	int first = data[0];
	for (int x = 0; x < NUM_BITS-1; x++) {
		data[x] = data[x+1];
	}
	data[NUM_BITS-1] = first;

	write_data();
	delay_ms(1000);
}

void cycle (uint16_t *vals, uint16_t step, uint16_t ceiling) {
	// If value 1 and 2 are > 0,
	// -- increase 2 by step, decrease 1 by step
	// If value 2 and 3 are > 0,
	// -- increase 3 by step, decrease 2 by step
	// If value 3 and 1 are > 0,
	// -- increase 1 by step, decrease 3 by step
	// If only one value > 0
	// -- Increase it by step
	// If a value reaches ceiling
	// -- Cap value at ceiling, raise next value by step
	// If no value is > 0 set value 1 to step

	if (vals[0] && vals[1]) {
		vals[0] -= step;
		vals[1] += step;
	} else if (vals[1] && vals[2]) {
		vals[1] -= step;
		vals[2] += step;	
	} else if (vals[2] && vals[0]) {
		vals[2] -= step;
		vals[0] += step;
	} else if (vals[0]) {
		vals[0] += step;
	} else if (vals[1]) {
		vals[1] += step;
	} else {
		vals[2] += step;
	}

	// If value is way over max, assume we flipped over from zero
	if (vals[0] > 0xFFFF) {
		vals[1] = 0;
	} else if (vals[1] > 0xFFFF) {
		vals[2] = 0;
	} else if (vals[2] > 0xFFFF) {
		vals[0] = 0;
	}

	// If we're over the ceiling cap and set the next in line to step
	if (vals[0] > ceiling) {
		vals[0] = ceiling;
		vals[1] = step;
	} else if (vals[1] > ceiling) {
		vals[1] = ceiling;
		vals[2] = step;
	} else if (vals[2] > ceiling) {
		vals[2] = ceiling;
		vals[0] = step;
	}
}

void rgb2hsv (uint16_t r, uint16_t g, uint16_t b, float *h, float *s, float *v) {
	uint16_t max = max3(r, g, b);
	uint16_t min = min3(r, g, b);

	*v = (float)max/0x0FFF;

	float delta = (float)(max - min)/0x0FFF;
	if (max == 0)
		*s = 0;
	else
		*s = delta/((float)max/0x0FFF);

	if (max == min) {
		*h = 0;
	} else {
		if (max == r) {
			*h = ((float)(g-b)/0x0FFF)/delta;
			if (g < b)
				*h += 6;
		} else if (max == g) {
			*h = ((float)(b-r)/0x0FFF)/delta + 2;
		} else {
			*h = ((float)(r-g)/0x0FFF)/delta + 4;
		}
		*h /= 6;
	}
}

uint16_t max3 (uint16_t a, uint16_t b, uint16_t c) {
	uint16_t max = a;

	if (max < b)
		max = b;
	if (max < c)
		max = c;
	return max;
}

uint16_t min3 (uint16_t a, uint16_t b, uint16_t c) {
	uint16_t min = a;

	if (min > b)
		min = b;
	if (min > c)
		min = c;
	return min;
}

void hsv2rgb (float h, float s, float v, uint16_t *r, uint16_t *g, uint16_t *b) {
	float fr = 0;
	float fg = 0;
	float fb = 0;

	int i = h * 6;
	float f = h * 6 - i;
	float p = v * (1 - s);
	float q = v * (1 - f * s);
	float t = v * (1 - (1 - f) * s);

	switch (i%6) {
		case 0: fr = v; fg = t; fb = p; break;
		case 1: fr = q; fg = v; fb = p; break;
		case 2: fr = p; fg = v; fb = t; break;
		case 3: fr = p; fg = q; fb = v; break;
		case 4: fr = t; fg = p; fb = v; break;
		case 5: fr = v; fg = p; fb = q; break;
	}

	*r = fr*0x0FFF;
	*g = fg*0x0FFF;
	*b = fb*0x0FFF;
}

int read_analog(void) {
	ADMUX = 0;						// Channel selection
	ADCSRA |= (1 << ADSC);			// Start conversion
	while (!(ADCSRA & (1<<ADIF)));	// Loop until conversion is complete
	ADCSRA |= (1 << ADIF);			// Clear ADIF by writing a 1 (this sets the value to 0)

	return(ADC);
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
			// Pulse the clock to get a rise then fall
			PORTD++;
			PORTD--;
		}
	}

	// Pulse the XLAT & BLANK line to latch in the data and reset the GSCLK
	PORTD = 0x04|0x08;
	PORTD = 0x00;
}
