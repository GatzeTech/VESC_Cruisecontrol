# VESC_Cruisecontrol
VESC Custom App for integrating a cruise control in a thumb throttle or hand throttle connected to the ADC.

It works as following:

1-Give some throttle (Going from STATE_IDLE to STATE_THROTTLE_1)

2-No throttle at all (Going from STATE_THROTTLE_1 to STATE_THROTTLE_2)

3-Within TIMER_THROTTLE_RETURN (20 = 0.2 seconds) give again throttle (Going from STATE_THROTTLE_2 to STATE_THROTTLE_3) otherwise back to step 1

4-Set the desired throttle to hold, at least higher than BORDER_THROTTLE (at the moment defined as 0.30). If lower than 0.3 than it acts as a normal throttle. Higher than 0.3, keep track of the maximum set throttle (pwr_highest)

5-Throtthle = 0 again. If in step 4 the maximum set throttle is higher than 0.3 then the Cruise Control is on and this value is set as duty cycle (STATE_THROTTLE_4). If the throttle was lower than 0.3 go back to step 1 (STATE_IDLE)

6-Any throttle input after step 5 will turn off the Cruise Control and 
