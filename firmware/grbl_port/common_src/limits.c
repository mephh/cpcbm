/*
  limits.c - code pertaining to limit-switches and performing the homing cycle
  Part of grbl_port_opencm3 project, derived from the Grbl work.

  Copyright (c) 2017 Angelo Di Chello
  Copyright (c) 2012-2015 Sungeun K. Jeon
  Copyright (c) 2009-2011 Simen Svale Skogsrud
  
  Grbl_port_opencm3 is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl_port_opencm3 is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl_port_opencm3.  If not, see <http://www.gnu.org/licenses/>.
*/
  
#include "grbl.h"

#ifdef TEST_NUCLEO_EXTI_PINS
#include "test_nucleo.h"
#endif

// Homing axis search distance multiplier. Computed by this value times the cycle travel.
#ifndef HOMING_AXIS_SEARCH_SCALAR
  #define HOMING_AXIS_SEARCH_SCALAR  1.5 // Must be > 1 to ensure limit switch will be engaged.
#endif
#ifndef HOMING_AXIS_LOCATE_SCALAR
  #define HOMING_AXIS_LOCATE_SCALAR  5.0 // Must be > 1 to ensure limit switch is cleared.
#endif

void limits_init() 
{
#ifdef NUCLEO
	SET_LIMITS_RCC;

	SET_LIMITS_DDR;  // Set as input pins
	#ifdef DISABLE_LIMIT_PIN_PULL_UP
    UNSET_LIMITS_PU; // Normal low operation. Requires external pull-down.
    #else
    SET_LIMITS_PU;   // Enable internal pull-up resistors. Normal high operation.
    #endif

    if (bit_istrue(settings.flags,BITFLAG_HARD_LIMIT_ENABLE)) {
    	/*reset pending exti events */
    	exti_reset_request(LIMIT_INT_vect);
    	exti_reset_request(LIMIT_INT_vect_Z);
    	/*reset pending exti interrupts */
    	nvic_clear_pending_irq(LIMIT_INT);
    	nvic_clear_pending_irq(LIMIT_INT_Z);
    	exti_select_source(LIMIT_X_EXTI, LIMIT_X_GPIO);
    	exti_select_source(LIMIT_Y_EXTI, LIMIT_Y_GPIO);
    	exti_select_source(LIMIT_Z_EXTI, LIMIT_Z_GPIO);
    	exti_enable_request(LIMIT_INT_vect);
		exti_set_trigger(LIMIT_INT_vect, EXTI_TRIGGER_FALLING);
		nvic_enable_irq(LIMIT_INT);// Enable Limits pins Interrupt
    	exti_enable_request(LIMIT_INT_vect_Z);
		exti_set_trigger(LIMIT_INT_vect_Z, EXTI_TRIGGER_FALLING);
		nvic_enable_irq(LIMIT_INT_Z);// Enable Limits pins Interrupt
	} else {
		limits_disable(); 
	}

#ifdef TEST_NUCLEO_EXTI_PINS
    test_initialization();
#endif
	
#else
  LIMIT_DDR &= ~(LIMIT_MASK); // Set as input pins

  #ifdef DISABLE_LIMIT_PIN_PULL_UP
    LIMIT_PORT &= ~(LIMIT_MASK); // Normal low operation. Requires external pull-down.
  #else
    LIMIT_PORT |= (LIMIT_MASK);  // Enable internal pull-up resistors. Normal high operation.
  #endif

  if (bit_istrue(settings.flags,BITFLAG_HARD_LIMIT_ENABLE)) {
    LIMIT_PCMSK |= LIMIT_MASK; // Enable specific pins of the Pin Change Interrupt
    PCICR |= (1 << LIMIT_INT); // Enable Pin Change Interrupt
  } else {
    limits_disable(); 
  }
  
  #ifdef ENABLE_SOFTWARE_DEBOUNCE
  #ifndef NUCLEO
    MCUSR &= ~(1<<WDRF);
    WDTCSR |= (1<<WDCE) | (1<<WDE);
    WDTCSR = (1<<WDP0); // Set time-out at ~32msec.
  #endif
  #endif
#endif //ifdef NUCLEO_F401
}


// Disables hard limits.
void limits_disable()
{
#ifdef NUCLEO
	nvic_disable_irq(LIMIT_INT);// Disable Limits pins Interrupt
	nvic_disable_irq(LIMIT_INT_Z);// Disable Limits pins Interrupt
#else
  LIMIT_PCMSK &= ~LIMIT_MASK;  // Disable specific pins of the Pin Change Interrupt
  PCICR &= ~(1 << LIMIT_INT);  // Disable Pin Change Interrupt
#endif
}


// Returns limit state as a bit-wise uint8 variable. Each bit indicates an axis limit, where 
// triggered is 1 and not triggered is 0. Invert mask is applied. Axes are defined by their
// number in bit position, i.e. Z_AXIS is (1<<2) or bit 2, and Y_AXIS is (1<<1) or bit 1.
uint8_t limits_get_state()
{
  uint8_t limit_state = 0;
#ifdef NUCLEO
  uint8_t pin = GET_LIMIT_PIN;
#else
  uint8_t pin = (LIMIT_PIN & LIMIT_MASK);
#endif
  #ifdef INVERT_LIMIT_PIN_MASK
    pin ^= INVERT_LIMIT_PIN_MASK;
  #endif
  if (bit_isfalse(settings.flags,BITFLAG_INVERT_LIMIT_PINS)) { pin ^= LIMIT_MASK; }
  if (pin) {  
    uint8_t idx;
    for (idx=0; idx<N_AXIS; idx++) {
      if (pin & get_limit_pin_mask(idx))
      {
    	  limit_state |= (1 << idx);
#ifdef TEST_NUCLEO_EXTI_PINS
          test_led_toggle();
#endif
      }
    }
  }
  return(limit_state);
}


// This is the Limit Pin Change Interrupt, which handles the hard limit feature. A bouncing 
// limit switch can cause a lot of problems, like false readings and multiple interrupt calls.
// If a switch is triggered at all, something bad has happened and treat it as such, regardless
// if a limit switch is being disengaged. It's impossible to reliably tell the state of a 
// bouncing pin without a debouncing method. A simple software debouncing feature may be enabled 
// through the config.h file, where an extra timer delays the limit pin read by several milli-
// seconds to help with, not fix, bouncing switches.
// NOTE: Do not attach an e-stop to the limit pins, because this interrupt is disabled during
// homing cycles and will not respond correctly. Upon user request or need, there may be a
// special pinout for an e-stop, but it is generally recommended to just directly connect
// your e-stop switch to the Arduino reset pin, since it is the most correct way to do this.
#ifndef ENABLE_SOFTWARE_DEBOUNCE
#ifdef NUCLEO
void exti0_isr()
{
	exti_reset_request(LIMIT_INT_vect_Z);
	nvic_clear_pending_irq(NVIC_EXTI0_IRQ);
#ifdef TEST_NUCLEO_EXTI_PINS
    test_interrupt_signalling((uint32_t)10);
#endif

    // Ignore limit switches if already in an alarm state or in-process of executing an alarm.
    // When in the alarm state, Grbl should have been reset or will force a reset, so any pending
    // moves in the planner and serial buffers are all cleared and newly sent blocks will be
    // locked out until a homing cycle or a kill lock command. Allows the user to disable the hard
    // limit setting if their limits are constantly triggering after a reset and move their axes.
    if (sys.state != STATE_ALARM) {
      if (!(sys_rt_exec_alarm)) {
        #ifdef HARD_LIMIT_FORCE_STATE_CHECK
          // Check limit pin state.
          if (limits_get_state()) {
            mc_reset(); // Initiate system kill.
            bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_HARD_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate hard limit critical event
          }
        #else
          mc_reset(); // Initiate system kill.
          bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_HARD_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate hard limit critical event
        #endif
      }
    }
}

void exti9_5_isr()
{
	/* Clear interrupt request */
	exti_reset_request(LIMIT_INT_vect);
	nvic_clear_pending_irq(NVIC_EXTI9_5_IRQ);
#else
  ISR(LIMIT_INT_vect) // DEFAULT: Limit pin change interrupt process. {
#endif

#ifdef TEST_NUCLEO_EXTI_PINS
    test_interrupt_signalling((uint32_t)5);
#endif

    // Ignore limit switches if already in an alarm state or in-process of executing an alarm.
    // When in the alarm state, Grbl should have been reset or will force a reset, so any pending 
    // moves in the planner and serial buffers are all cleared and newly sent blocks will be 
    // locked out until a homing cycle or a kill lock command. Allows the user to disable the hard
    // limit setting if their limits are constantly triggering after a reset and move their axes.
    if (sys.state != STATE_ALARM) { 
      if (!(sys_rt_exec_alarm)) {
        #ifdef HARD_LIMIT_FORCE_STATE_CHECK
          // Check limit pin state. 
          if (limits_get_state()) {
            mc_reset(); // Initiate system kill.
            bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_HARD_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate hard limit critical event
          }
        #else
          mc_reset(); // Initiate system kill.
          bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_HARD_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate hard limit critical event
        #endif
      }
    }
  }  
//TODO: adjust software debounce isr routine for nucleo 
#else // OPTIONAL: Software debounce limit pin routine.
  // Upon limit pin change, enable watchdog timer to create a short delay.
#ifdef NUCLEO
void enable_debounce_timer(void)
{
  if(!nvic_get_irq_enabled(SW_DEBOUNCE_TIMER_IRQ))
  {
    /* Enable SW_DEBOUNCE_TIMER clock. */
    rcc_periph_clock_enable(SW_DEBOUNCE_TIMER_RCC);
    rcc_periph_reset_pulse(SW_DEBOUNCE_TIMER_RST);
    /* Continous mode. */
    timer_continuous_mode(SW_DEBOUNCE_TIMER);
    timer_set_mode(SW_DEBOUNCE_TIMER, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
    /* ARR reload enable. */
    timer_enable_preload(SW_DEBOUNCE_TIMER);
    timer_set_prescaler(SW_DEBOUNCE_TIMER, (256*PSC_MUL_FACTOR)-1);// set to 1/8 Prescaler
    timer_set_period(SW_DEBOUNCE_TIMER, 0X09FF);

    timer_set_oc_mode(SW_DEBOUNCE_TIMER, TIM_OC1, TIM_OCM_FROZEN);
    timer_set_oc_value(SW_DEBOUNCE_TIMER, TIM_OC1, 0x9F0);

    /* Enable SW_DEBOUNCE_TIMER Stepper Driver Interrupt. */
    timer_enable_irq(SW_DEBOUNCE_TIMER, TIM_DIER_CC1IE); /** Capture/compare 1 interrupt enable */
    nvic_enable_irq(SW_DEBOUNCE_TIMER_IRQ);

    timer_set_counter(SW_DEBOUNCE_TIMER,0);
    timer_enable_counter(SW_DEBOUNCE_TIMER); /* Counter enable. */
  }
}

void exti0_isr()
{
    /* Clear interrupt request */
    exti_reset_request(LIMIT_INT_vect_Z);
    nvic_clear_pending_irq(NVIC_EXTI0_IRQ);

    enable_debounce_timer();
}
void exti9_5_isr()
{
    /* Clear interrupt request */
    exti_reset_request(LIMIT_INT_vect);
    nvic_clear_pending_irq(NVIC_EXTI9_5_IRQ);

    enable_debounce_timer();
}

void SW_DEBOUNCE_TIMER_ISR()
{
  timer_disable_counter(SW_DEBOUNCE_TIMER);
  nvic_clear_pending_irq(SW_DEBOUNCE_TIMER_IRQ);
  nvic_disable_irq(SW_DEBOUNCE_TIMER_IRQ);

#else
  ISR(LIMIT_INT_vect) { if (!(WDTCSR & (1<<WDIE))) { WDTCSR |= (1<<WDIE); } }
  ISR(WDT_vect) // Watchdog timer ISR
  {
    WDTCSR &= ~(1<<WDIE); // Disable watchdog timer.
#endif
    if (sys.state != STATE_HOMING && sys.state != STATE_ALARM) {  // Ignore if already in alarm state and during homing.
      if (!(sys_rt_exec_alarm)) {
        // Check limit pin state. 
        if (limits_get_state()) {
          mc_reset(); // Initiate system kill.
          bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_HARD_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate hard limit critical event
        }
      }  
    }
  }
#endif

 
// Homes the specified cycle axes, sets the machine position, and performs a pull-off motion after
// completing. Homing is a special motion case, which involves rapid uncontrolled stops to locate
// the trigger point of the limit switches. The rapid stops are handled by a system level axis lock 
// mask, which prevents the stepper algorithm from executing step pulses. Homing motions typically 
// circumvent the processes for executing motions in normal operation.
// NOTE: Only the abort realtime command can interrupt this process.
// TODO: Move limit pin-specific calls to a general function for portability.
void limits_go_home(uint8_t cycle_mask) 
{
  if (sys.abort) { return; } // Block if system reset has been issued.

  // Initialize
  uint8_t n_cycle = (2*N_HOMING_LOCATE_CYCLE+1);
#ifdef NUCLEO
  uint16_t step_pin[N_AXIS];
#else
  uint8_t step_pin[N_AXIS];
#endif
  float target[N_AXIS];
  float max_travel = 0.0;
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {  
    // Initialize step pin masks
    step_pin[idx] = get_step_pin_mask(idx);
    #ifdef COREXY    
      if ((idx==A_MOTOR)||(idx==B_MOTOR)) { step_pin[idx] = (get_step_pin_mask(X_AXIS)|get_step_pin_mask(Y_AXIS)); } 
    #endif

    if (bit_istrue(cycle_mask,bit(idx))) { 
      // Set target based on max_travel setting. Ensure homing switches engaged with search scalar.
      // NOTE: settings.max_travel[] is stored as a negative value.
      max_travel = max(max_travel,(-HOMING_AXIS_SEARCH_SCALAR)*settings.max_travel[idx]);
    }
  }

  // Set search mode with approach at seek rate to quickly engage the specified cycle_mask limit switches.
  bool approach = true;
  float homing_rate = settings.homing_seek_rate;

  uint8_t limit_state, n_active_axis;
#ifdef NUCLEO
  uint16_t axislock;
#else
  uint8_t axislock;
#endif
  do {

    system_convert_array_steps_to_mpos(target,sys.position);

    // Initialize and declare variables needed for homing routine.
    axislock = 0;
    n_active_axis = 0;
    for (idx=0; idx<N_AXIS; idx++) {
      // Set target location for active axes and setup computation for homing rate.
      if (bit_istrue(cycle_mask,bit(idx))) {
        n_active_axis++;
        #ifdef COREXY
        if (!approach)
        {
          if (idx == X_AXIS) {
            int32_t axis_position = (bit_istrue(settings.dir_invert_mask,bit(Y_AXIS)) ? -1 : 1) * system_convert_corexy_to_y_axis_steps(sys.position);
            sys.position[A_MOTOR] = axis_position;
            sys.position[B_MOTOR] = -axis_position;
          } else if (idx == Y_AXIS) {
            int32_t axis_position = (bit_istrue(settings.dir_invert_mask,bit(X_AXIS)) ? -1 : 1) * system_convert_corexy_to_x_axis_steps(sys.position);
            sys.position[A_MOTOR] = sys.position[B_MOTOR] = axis_position;
          } else { 
            sys.position[Z_AXIS] = 0; 
          }
        }
        #else
        sys.position[idx] = 0;
        #endif
        // Set target direction based on cycle mask and homing cycle approach state.
        // NOTE: This happens to compile smaller than any other implementation tried.
        if (bit_istrue(settings.homing_dir_mask,bit(idx))) {
          if (approach) { target[idx] = -max_travel; }
          else { target[idx] = max_travel; }
        } else { 
          if (approach) { target[idx] = max_travel; }
          else { target[idx] = -max_travel; }
        }        
        // Apply axislock to the step port pins active in this cycle.
        axislock |= step_pin[idx];
      }

    }
    homing_rate *= sqrt(n_active_axis); // [sqrt(N_AXIS)] Adjust so individual axes all move at homing rate.
    sys.homing_axis_lock = axislock;

    plan_sync_position(); // Sync planner position to current machine position.
    
    // Perform homing cycle. Planner buffer should be empty, as required to initiate the homing cycle.
    #ifdef USE_LINE_NUMBERS
      plan_buffer_line(target, homing_rate, false, HOMING_CYCLE_LINE_NUMBER); // Bypass mc_line(). Directly plan homing motion.
    #else
      plan_buffer_line(target, homing_rate, false); // Bypass mc_line(). Directly plan homing motion.
    #endif
    
    st_prep_buffer(); // Prep and fill segment buffer from newly planned block.
    st_wake_up(); // Initiate motion
    do {
      if (approach) {
        // Check limit state. Lock out cycle axes when they change.
        limit_state = limits_get_state();
#ifdef ENABLE_SOFTWARE_DEBOUNCE
        if (limit_state != 0)
        {
        	enable_debounce_timer();
        	limit_state = limits_get_state();
        }
#endif
        for (idx=0; idx<N_AXIS; idx++) {
          if (axislock & step_pin[idx]) {
            if (limit_state & (1 << idx)) { 
              #ifdef COREXY
                if (idx==Z_AXIS) { axislock &= ~(step_pin[Z_AXIS]); }
                else { axislock &= ~(step_pin[A_MOTOR]|step_pin[B_MOTOR]); }
              #else
                axislock &= ~(step_pin[idx]); 
              #endif
            }
          }
        }
        sys.homing_axis_lock = axislock;
      }

      st_prep_buffer(); // Check and prep segment buffer. NOTE: Should take no longer than 200us.

      // Exit routines: No time to run protocol_execute_realtime() in this loop.
      if (sys_rt_exec_state & (EXEC_SAFETY_DOOR | EXEC_RESET | EXEC_CYCLE_STOP)) {
        // Homing failure: Limit switches are still engaged after pull-off motion
        if ( (sys_rt_exec_state & (EXEC_SAFETY_DOOR | EXEC_RESET)) ||  // Safety door or reset issued
           (!approach && (limits_get_state() & cycle_mask)) ||  // Limit switch still engaged after pull-off motion
           ( approach && (sys_rt_exec_state & EXEC_CYCLE_STOP)) ) { // Limit switch not found during approach.
          mc_reset(); // Stop motors, if they are running.
          protocol_execute_realtime();
          return;
        } else {
          // Pull-off motion complete. Disable CYCLE_STOP from executing.
          bit_false_atomic(sys_rt_exec_state,EXEC_CYCLE_STOP);
          break;
        } 
      }
#ifdef NUCLEO
    } while ((STEP_MASK_X | STEP_MASK_YZ) & axislock);
#else
    } while (STEP_MASK & axislock);
#endif
    st_reset(); // Immediately force kill steppers and reset step segment buffer.
    plan_reset(); // Reset planner buffer to zero planner current position and to clear previous motions.

    delay_ms(settings.homing_debounce_delay); // Delay to allow transient dynamics to dissipate.

    // Reverse direction and reset homing rate for locate cycle(s).
    approach = !approach;

    // After first cycle, homing enters locating phase. Shorten search to pull-off distance.
    if (approach) { 
      max_travel = settings.homing_pulloff*HOMING_AXIS_LOCATE_SCALAR; 
      homing_rate = settings.homing_feed_rate;
    } else {
      max_travel = settings.homing_pulloff;    
      homing_rate = settings.homing_seek_rate;
    }
    
  } while (n_cycle-- > 0);
      
  // The active cycle axes should now be homed and machine limits have been located. By 
  // default, Grbl defines machine space as all negative, as do most CNCs. Since limit switches
  // can be on either side of an axes, check and set axes machine zero appropriately. Also,
  // set up pull-off maneuver from axes limit switches that have been homed. This provides
  // some initial clearance off the switches and should also help prevent them from falsely
  // triggering when hard limits are enabled or when more than one axes shares a limit pin.
  int32_t set_axis_position;
  // Set machine positions for homed limit switches. Don't update non-homed axes.
  for (idx=0; idx<N_AXIS; idx++) {
    // NOTE: settings.max_travel[] is stored as a negative value.
    if (cycle_mask & bit(idx)) {
      #ifdef HOMING_FORCE_SET_ORIGIN
        set_axis_position = 0;
      #else 
        if ( bit_istrue(settings.homing_dir_mask,bit(idx)) ) {
          set_axis_position = lround((settings.max_travel[idx]+settings.homing_pulloff)*settings.steps_per_mm[idx]);
        } else {
          set_axis_position = lround(-settings.homing_pulloff*settings.steps_per_mm[idx]);
        }
      #endif
      
      #ifdef COREXY
        if (idx==X_AXIS) { 
          int32_t off_axis_position = system_convert_corexy_to_y_axis_steps(sys.position);
          sys.position[A_MOTOR] = (bit_istrue(settings.dir_invert_mask,bit(X_AXIS)) ? -1 : 1) * (set_axis_position) + (bit_istrue(settings.dir_invert_mask,bit(Y_AXIS)) ? -1 : 1) * (off_axis_position);
          sys.position[B_MOTOR] = (bit_istrue(settings.dir_invert_mask,bit(X_AXIS)) ? -1 : 1) * (set_axis_position) - (bit_istrue(settings.dir_invert_mask,bit(Y_AXIS)) ? -1 : 1) * (off_axis_position);
        } else if (idx==Y_AXIS) {
          int32_t off_axis_position = system_convert_corexy_to_x_axis_steps(sys.position);
          sys.position[A_MOTOR] = (bit_istrue(settings.dir_invert_mask,bit(X_AXIS)) ? -1 : 1) * (off_axis_position) + (bit_istrue(settings.dir_invert_mask,bit(Y_AXIS)) ? -1 : 1) * (set_axis_position);
          sys.position[B_MOTOR] = (bit_istrue(settings.dir_invert_mask,bit(X_AXIS)) ? -1 : 1) * (off_axis_position) - (bit_istrue(settings.dir_invert_mask,bit(Y_AXIS)) ? -1 : 1) * (set_axis_position);
        } else {
          sys.position[idx] = (bit_istrue(settings.dir_invert_mask,bit(Z_AXIS)) ? -1 : 1) * set_axis_position;
        }
      #else 
        sys.position[idx] = set_axis_position;
      #endif

    }
  }
  plan_sync_position(); // Sync planner position to homed machine position.
    
  // sys.state = STATE_HOMING; // Ensure system state set as homing before returning. 
}


// Performs a soft limit check. Called from mc_line() only. Assumes the machine has been homed,
// the workspace volume is in all negative space, and the system is in normal operation.
void limits_soft_check(float *target)
{
  uint8_t idx;
  for (idx=0; idx<N_AXIS; idx++) {
   
    #ifdef HOMING_FORCE_SET_ORIGIN
      // When homing forced set origin is enabled, soft limits checks need to account for directionality.
      // NOTE: max_travel is stored as negative
      if (bit_istrue(settings.homing_dir_mask,bit(idx))) {
        if (target[idx] < 0 || target[idx] > -settings.max_travel[idx]) { sys.soft_limit = true; }
      } else {
        if (target[idx] > 0 || target[idx] < settings.max_travel[idx]) { sys.soft_limit = true; }
      }
    #else  
      // NOTE: max_travel is stored as negative
      if (target[idx] > 0 || target[idx] < settings.max_travel[idx]) { sys.soft_limit = true; }
    #endif
    
    if (sys.soft_limit) {
      // Force feed hold if cycle is active. All buffered blocks are guaranteed to be within 
      // workspace volume so just come to a controlled stop so position is not lost. When complete
      // enter alarm mode.
      if (sys.state == STATE_CYCLE) {
        bit_true_atomic(sys_rt_exec_state, EXEC_FEED_HOLD);
        do {
          protocol_execute_realtime();
          if (sys.abort) { return; }
        } while ( sys.state != STATE_IDLE );
      }
    
      mc_reset(); // Issue system reset and ensure spindle and coolant are shutdown.
      bit_true_atomic(sys_rt_exec_alarm, (EXEC_ALARM_SOFT_LIMIT|EXEC_CRITICAL_EVENT)); // Indicate soft limit critical event
      protocol_execute_realtime(); // Execute to enter critical event loop and system abort
      return;
    }
  }
}
