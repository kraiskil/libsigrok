/*
 * This file is part of the libsigrok project.
 * It implmenets the adaptation interface to sigrok
 * for Digilent's AnalogDiscovery2 device, using
 * Digilent's Waveforms SDK.
 *
 * NB: CamelCase functions are from Adept, underscoer_style_names
 *     from sigrok.
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

#include "config.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
/* Sigrok includes */
#include "protocol.h"

/* Parameters to look for in scan() for (I think?).
 * Nothing but the connection in this case:
 * our device is fixed */
static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

/* Basic driver functionality. */
static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
	// enable outputs eventually
	// SR_CONF_SIGNAL_GENERATOR,
	// SR_CONF_PATTERN_MODE ?
};

/* TODO: revisit these. Just a placeholder for now */
static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

/* TODO: Placeholder */
static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(10),
	SR_HZ(50),
	SR_HZ(100),
	SR_HZ(200)
};
#define NUM_SAMPLERATES (sizeof(samplerates)/sizeof(samplerates[0]))

static const int32_t available_digital_triggers[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};


/* This returns the currently applied configuration on the device */
static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	/* TODO: do we need checks for NULL here? */
	struct dev_context *devc = sdi->priv;

	(void)sdi;
	(void)data;
	(void)cg;
	if (!sdi)
		return SR_ERR_ARG;
	//struct dev_context *devc = (struct dev_context *)(sdi->priv);

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		/* TODO: allow other than 100kHz samplerates */
		*data = g_variant_new_uint64(SR_HZ(devc->cur_samplerate));
		break;
	case SR_CONF_TRIGGER_MATCH:
		sr_spew("config_get(): TRIGGER_MATCH\n");
		ret = SR_OK;
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;
	struct dev_context *devc;

	(void)data;
	(void)cg;
	sr_spew("AnalogDiscovery2: config_set(key=%d)\n", key);

	devc = sdi->priv;
	ret = SR_OK;
	switch (key) {

	/* TODO */
	case SR_CONF_TRIGGER_MATCH:
		sr_spew("config_set(): TRIGGER_MATCH\n");
		ret = SR_OK;
		break;
	case SR_CONF_SAMPLERATE:
		sr_spew("config_set(): SAMPLERATE\n");
		/* Check that idx is a valid index into the array of supported samplerates. */
		int idx = std_u64_idx(data, samplerates, NUM_SAMPLERATES);
		sr_spew("     idx = %d\n", idx);
		if (idx < 0)
			return SR_ERR_ARG;
		devc->cur_samplerate = samplerates[idx];
		sr_spew("set the sample rate to %ld Hz\n", devc->cur_samplerate);
		break;
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

/* "List all possible values for a configuration key", i.e. this is used
 * to query the device's capabilities.
 * 'data' gets allocated here, and filled with the result. */
static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	/* Documentation says to use SR_ERR_ARG,
	 * but that is defined as "argument error", whereas
	 * SR_ERR_NA is "not applicable", which sounds less of an error.
	 * Also, most other codes seem to use this. */
	ret = SR_ERR_NA; 

	switch (key) {
	case SR_CONF_SCAN_OPTIONS: /* Intentional fall-through */
	case SR_CONF_DEVICE_OPTIONS:
		/* Handled by Sigrok shared boilerplate code.
		 * TODO: for some reason, nothing gets done if cg and sdi are both
		 *       non-null. handle that here. */
		if( cg == NULL || sdi == NULL )
			ret = STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		else
			ret = SR_ERR_NA;
		break;
		
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_MATCH:
		sr_spew("config_list(): TRIGGER_MATCH\n");
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(available_digital_triggers));
		ret = SR_OK;
		break;
	default:
		sr_warn("AnalogDiscovery2: config_list(UNKNOWN: %d) - unimplemented\n", key);
		return SR_ERR_NA;
	}

	return ret;
}


static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_trigger *trigger;
	sr_spew("%s(), entry\n", __func__);
	/* Probably a redundant check? */
	if(sdi==NULL)
		return SR_ERR_ARG;

	struct dev_context *devc = (struct dev_context *)(sdi->priv);
	if(devc==NULL)
		return SR_ERR_DEV_CLOSED;

	if(devc->is_opened==false)
		return SR_ERR_DEV_CLOSED;



	trigger = sr_session_trigger_get(sdi->session);
	/* Add the data-receiving callback to be polled for */
	sr_session_source_add(
		sdi->session,
		-1, /* Create a timer source (as opposed to polling a file descriptor */
		 0, /* Maximum number of events to check for - 0 means no limit? */
		 100, /* Max time to wait for (ms) before callback is called? 
		       * Does this just mean the timer period when not polling a file? */  
		digilent_analogdiscovery2_receive_data, (struct sr_dev_inst *)sdi);

	/* Every other backend had this, we want it too!
	 * Documented as: 
	 * Standard API helper for sending an SR_DF_HEADER packet.
	 *
	 * This function can be used to simplify most drivers'
	 * dev_acquisition_start() API callback.
	 */
	std_session_send_df_header(sdi);

	digilent_analogdiscovery2_start(devc, trigger);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_spew("%s(), entry\n", __func__);

	/* Removes the callbacks to digilent_analogdiscovery2_receive_data(), I think */
	sr_session_source_remove(sdi->session, -1);
	/* uh? */
	std_session_send_df_end(sdi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver digilent_analogdiscovery2_driver_info = {
	.name = "digilent-analogdiscovery2",
	.longname = "Digilent AnalogDiscovery2",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = digilent_analogdiscovery2_scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = digilent_analogdiscovery2_open,
	.dev_close = digilent_analogdiscovery2_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(digilent_analogdiscovery2_driver_info);
