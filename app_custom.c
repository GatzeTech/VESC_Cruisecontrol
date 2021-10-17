/*
	Copyright 2019 Benjamin Vedder	benjamin@vedder.se

	This file is part of the VESC firmware.

	The VESC firmware is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    The VESC firmware is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
    */

#include "app.h"
#include "ch.h"
#include "hal.h"

// Some useful includes
#include "mc_interface.h"
#include "utils.h"
#include "encoder.h"
#include "terminal.h"
#include "comm_can.h"
#include "hw.h"
#include "commands.h"
#include "timeout.h"

#include <math.h>
#include <string.h>
#include <stdio.h>

// Threads
static THD_FUNCTION(my_thread, arg);
static THD_WORKING_AREA(my_thread_wa, 2048);

// Private functions
//static void pwm_callback(void);
//static void terminal_test(int argc, const char **argv);

// Private variables
static volatile bool stop_now = true;
static volatile bool is_running = false;

//**********Custom**********
//State's
#define STATE_IDLE          0
#define STATE_THROTTLE_1    1
#define STATE_THROTTLE_2    2
#define STATE_THROTTLE_3    3
#define STATE_THROTTLE_4    4
#define STATE_THROTTLE_5    5

#define TIMER_THROTTLE_RETURN    20
#define BORDER_THROTTLE 0.30

#define LPF_FACTOR 0.2 //1=no filtering 0 is a a lot of filtering

static volatile app_configuration config;
static volatile float ms_without_power = 0.0;
static volatile float read_voltage = 0.0;
static volatile float voltage_filtered;
static volatile float voltage_filtered_old;
static volatile float pwr_old;
static float pwr;
static float pwr_temp;
static float pwr_highest;

unsigned int timer_throttle;
unsigned int counter;

unsigned int state;
//**********Custom**********



// Called when the custom application is started. Start our
// threads here and set up callbacks.
void app_custom_start(void) {
	//mc_interface_set_pwm_callback(pwm_callback);

	stop_now = false;
	chThdCreateStatic(my_thread_wa, sizeof(my_thread_wa),
			NORMALPRIO, my_thread, NULL);

	/*// Terminal commands for the VESC Tool terminal can be registered.
	terminal_register_command_callback(
			"custom_cmd",
			"Print the number d",
			"[d]",
			terminal_test);*/
}

// Called when the custom application is stopped. Stop our threads
// and release callbacks.
void app_custom_stop(void) {
	//mc_interface_set_pwm_callback(0);
	//terminal_unregister_callback(terminal_test);

	stop_now = true;
	while (is_running) {
		chThdSleepMilliseconds(1);
	}
}

void app_custom_configure(app_configuration *conf) {
	config = *conf;
	ms_without_power = 0.0;
}

static THD_FUNCTION(my_thread, arg) {
	(void)arg;

	chRegSetThreadName("App Custom");

	is_running = true;

	// Example of using the experiment plot
//	chThdSleepMilliseconds(8000);
//	commands_init_plot("Sample", "Voltage");
//	commands_plot_add_graph("Temp Fet");
//	commands_plot_add_graph("Input Voltage");
//	float samp = 0.0;
//
//	for(;;) {
//		commands_plot_set_graph(0);
//		commands_send_plot_points(samp, mc_interface_temp_fet_filtered());
//		commands_plot_set_graph(1);
//		commands_send_plot_points(samp, GET_INPUT_VOLTAGE());
//		samp++;
//		chThdSleepMilliseconds(10);
//	}

	for(;;) {
		// Check if it is time to stop.
		if (stop_now) {
			is_running = false;
			return;
		}

		timeout_reset(); // Reset timeout if everything is OK.

		// Run your logic here. A lot of functionality is available in mc_interface.h.
		
//**********Custom**********		
			// Read the external ADC pin and convert the value to a voltage.
		pwr = (float)ADC_Value[ADC_IND_EXT];
		pwr /= 4095;
		pwr *= V_REG;
		read_voltage = pwr;

		if (config.app_adc_conf.use_filter) {
		  //Using own alpha filter for input
		  voltage_filtered = LPF_FACTOR * read_voltage + (1.0-LPF_FACTOR)*voltage_filtered_old;
		  voltage_filtered_old = voltage_filtered;
      pwr = voltage_filtered;
		}

    //alway same mapping
		// Linear mapping between the start and end voltage
		pwr = utils_map(pwr, config.app_adc_conf.voltage_start, config.app_adc_conf.voltage_end, 0.0, 1.0);

		// Truncate the read voltage
		utils_truncate_number(&pwr, 0.0, 1.0);

		// Apply deadband
		utils_deadband(&pwr, config.app_adc_conf.hyst, 1.0);

    pwr_temp = pwr;
		//State's to check if holding throttle
		switch(state) {
			case STATE_IDLE:
				if (pwr_temp > 0){
					state=STATE_THROTTLE_1;
				}
        pwr = 0;
			break;
			case STATE_THROTTLE_1:
        pwr = pwr_temp;	
	
        if (pwr_temp == 0) {
          state = STATE_THROTTLE_2;
          timer_throttle = 0;
        }
			break;
			case STATE_THROTTLE_2:			
        timer_throttle += 1;
        if (timer_throttle > TIMER_THROTTLE_RETURN) {
          state = STATE_IDLE;
        }

        if (pwr_temp > 0){
          state = STATE_THROTTLE_3;
          pwr_highest = 0;
        }
      break;
			case STATE_THROTTLE_3:	
        if (pwr_temp > pwr_highest) {
          pwr_highest = pwr_temp;
        }
        
        if (pwr_highest < BORDER_THROTTLE) {
          pwr = pwr_temp;
        } else if (pwr_highest >= BORDER_THROTTLE) {
          pwr = pwr_highest;
        }

        if (pwr_temp == 0) {
          if (pwr_highest >= BORDER_THROTTLE) {
            state = STATE_THROTTLE_4;
          } else if (pwr_highest < BORDER_THROTTLE) {
            state = STATE_IDLE;
          }  
        }
      break;
      case STATE_THROTTLE_4:	
        pwr = pwr_highest;

        if (pwr_temp > 0){
					state = STATE_THROTTLE_5;
				}

			break;
      case STATE_THROTTLE_5:	
        if (pwr_temp == 0){
					state=STATE_IDLE;
				}
			break;
			//default:	
		}

    mc_interface_set_duty(pwr);

    		//printf for debugging
		/*if (counter == 1) {
			counter = 0;

			commands_printf("state=%d",state);
      commands_printf("pwr_temp=%d",(int)(pwr_temp*1000));
      commands_printf("pwr=%d",(int)(pwr*1000));
      commands_printf("pwr_highest=%d",(int)(pwr_highest*1000));
		}
		counter++;*/
//**********Custom**********




	
		
		
		
		
		
		

		chThdSleepMilliseconds(10);
	}
}

//static void pwm_callback(void) {
	// Called for every control iteration in interrupt context.
//}

/*// Callback function for the terminal command with arguments.
static void terminal_test(int argc, const char **argv) {
	if (argc == 2) {
		int d = -1;
		sscanf(argv[1], "%d", &d);

		commands_printf("You have entered %d", d);

		// For example, read the ADC inputs on the COMM header.
		commands_printf("ADC1: %.2f V ADC2: %.2f V",
				(double)ADC_VOLTS(ADC_IND_EXT), (double)ADC_VOLTS(ADC_IND_EXT2));
	} else {
		commands_printf("This command requires one argument.\n");
	}
}*/
