/*
 * This file is part of the libsigrok project.
 *
 * Copyright 2018 Kalle Raiskila <kraiskil@iki.fi>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBSIGROK_HARDWARE_DIGILENT_ANALOGDISCOVERY2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_DIGILENT_ANALOGDISCOVERY2_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "digilent-analogdiscovery2"

static const int NUM_LOGIC_CHAN=16; 
static const int NUM_ANALOG_CHAN=2; 
static const int NUM_WAVEGEN_CHAN=2; 


struct dev_context {
	int enum_idx; /* Waveforms SDK internal enumeration index. */
	gboolean is_opened;
	int hdwf;   /* Waveforms SDK handle to opened device.
	             * Actually of type HDWF, but we cannot include dwf.h here
	             * as it is contains definitions of global variables :(
	             * TODO: struct dev_context was put here by sigrok-new, 
	             * figure out if it is really needed here at all.
	             */
	uint64_t cur_samplerate; /* samplerate-in-use. In Hz */
};

SR_PRIV int digilent_analogdiscovery2_receive_data(int fd, int revents, void *cb_data);

SR_PRIV void digilent_analogdiscovery2_start(
	struct dev_context *,
	struct sr_trigger *);

SR_PRIV GSList* digilent_analogdiscovery2_scan(
	struct sr_dev_driver *di,
	GSList *options);
SR_PRIV int digilent_analogdiscovery2_open(struct sr_dev_inst *sdi);
SR_PRIV int digilent_analogdiscovery2_close(struct sr_dev_inst *sdi);
#endif
