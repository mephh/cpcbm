/*
  limits.h - code pertaining to limit-switches and performing the homing cycle
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

#ifndef LIMITS_H
#define LIMITS_H


// Initialize the limits module
void limits_init(void);

// Disables hard limits.
void limits_disable(void);

// Returns limit state as a bit-wise uint8 variable.
uint8_t limits_get_state(void);

// Perform one portion of the homing cycle based on the input settings.
void limits_go_home(uint8_t cycle_mask);

// Check for soft limit violations
void limits_soft_check(float *target);

void enable_debounce_timer(void);

#endif /* LIMITS_H */
