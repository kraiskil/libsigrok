/*
 * This file is part of the libsigrok project.
 * It implements data parsing via Digilent's Waveforms
 * SDK, particularly the AnalogDiscovery2 device.
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

#include <config.h>
#include "protocol.h"

#include <assert.h>
#include <stdlib.h>
/* Digilent includes - some confusion with C variants? dwf.h complains
 * 'bool' is unknown */
#include <stdbool.h>
#include "digilent/waveforms/dwf.h"



/* Local helper functions, documented at definitions */
static void decode_trigger(struct sr_trigger*, int*, int*, int*, int*);


/* This is the main callback to get the data from the device.
 * Per sigrok convention, it should be defined in a separate protocol.c file,
 * but because of globals in Digilent's dwh.h, we can #include that header in
 * only one compilation unit.
 */
SR_PRIV int digilent_analogdiscovery2_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	char payload[1024*1024]; // TODO: can this be on stack? packet and logic can, so this probably also. CHECK!
	HDWF hdwf;
	int cAvail, cCorrupt, cLost;

	(void)fd;
	(void)revents;

	sdi = (const struct sr_dev_inst*) cb_data;
	if (sdi==NULL)
		return TRUE;
	devc = (struct dev_context*) (sdi->priv);
	if (devc==NULL)
		return TRUE;
	hdwf = devc->hdwf;

	FDwfDigitalInStatusRecord( hdwf, &cAvail, &cLost, &cCorrupt);
	sr_spew("cAvail = %d\n", cAvail);
	if( cCorrupt || cLost ) {
		sr_err("_recevice_data() - samples lost=%d, samples corrupt=%d", cLost, cCorrupt);
		return TRUE; // TODO: false? Find out what the sematics for this function's return 
		             //       value is. Some return TRUE/FALSE others G_SOURCE_REMOVE/CONTINUE
		             //       How do we signal upstream that the device has run out of samples?
		             //       or is that something we need to do at device initialization time
		             //       and expect it to not request more samples than we are capable of
		             //       sending? Ugh.
	}
	DwfState sts;
	FDwfDigitalInStatus( hdwf, TRUE /*read, don't write*/, &sts);
	// Example code says this is the condition for "waiting for triggger"
	if(   cAvail == 0 
	   || (   sts == DwfStateConfig
	       || sts == DwfStateArmed
	       || sts == DwfStatePrefill
	      )
          ) {
		sr_spew("Waiting for trigger\n");
		return TRUE; // Or G_SOURCE_CONTINUE would be more appropriate?
	}

	FDwfDigitalInStatusData( hdwf, payload, cAvail*2);

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = cAvail*2; // TODO: bits, bytes or samples?? Assume bytes for now.
	logic.unitsize = 2;      // TODO: as above
	logic.data = payload;
	// TODO: 'demo' device prunes the data by masking disabled channels to 0. 
	//       yet they are sent. Why? Should we do it too?
	//logic_fixup_feed(devc, &logic);
	sr_session_send(sdi, &packet);
	
	return TRUE;
}

/* Start the analogdiscovery2 */
SR_PRIV void digilent_analogdiscovery2_start(
	struct dev_context *devc,
	struct sr_trigger *trigger)
{
	int low_mask,high_mask,rise_mask,fall_mask;
	HDWF hdwf = (HDWF)devc->hdwf;

	decode_trigger(trigger, &low_mask, &high_mask, &rise_mask, &fall_mask);

	/* Set up the device. This is now a hard-coded setup to begin with. */
	/* Start recording samples after trigger */
	FDwfDigitalInAcquisitionModeSet( hdwf, acqmodeRecord );
	/* Sample rate to 100kHz.
	 * This sets sample rate to sysclock/divisor, i.e. 100MHz/target
	 * TODO: there is a function to query this sysclock - is it changable?
	 */
	assert(devc->cur_samplerate != 0 && "Sample rate must be non-zero");
	FDwfDigitalInDividerSet( hdwf, 100000000/devc->cur_samplerate );
	/* Sample format: 
	 * "The function above is used to set the sample format, the number of bits 
	 * starting from least significant bit. Valid options are 8, 16, and 32."
	 * A bit unclear, but probably means one bit per channel, packed into 2 bytes.
	 * We don't seem to need to enabled/disable any channels
	 */
	FDwfDigitalInSampleFormatSet( hdwf, 16 );
	/* Set number of samples to acquire after trigger. */
	FDwfDigitalInTriggerPositionSet( hdwf, 100000 );
	/* Trigger on the digital inputs (as opposed to analog) */
	FDwfDigitalInTriggerSourceSet( hdwf, trigsrcDetectorDigitalIn );
	/* Set triggers:  enable bitmasks are combined with 'low AND high AND (falling OR rising)', 
	 * one bit per channel. All triggered channel with met conditions are required to trigger the
	 * acquistion.*/
	FDwfDigitalInTriggerSet( hdwf, low_mask, high_mask, rise_mask, fall_mask );

	/* Start aquisition on the device */
	FDwfDigitalInConfigure( hdwf, true, true );

}
/* Read an input sigrok 'struct sr_trigger', and create the bitmasks that
 * can be used with Digilent's API.
 * TODO: We don't handle multiple trigger stages. I think. It sounds like
 * something the Digilent HW doesn't support.
 */
static void decode_trigger(
	struct sr_trigger *trigger,
	int *low_mask,
	int *high_mask,
	int *rise_mask,
	int *fall_mask)
{
	*low_mask=0;
	*high_mask=0;
	*rise_mask=0;
	*fall_mask=0;

	/* A trigger is a GSList of trigger stages, each of which
	 * is a GSList of trigger matches.
	 * Here we merge all stages into one, and warn the user that it is not supported */
	if( trigger != NULL ) {
		GSList *slist = trigger->stages;
		struct sr_trigger_stage *stage =
			(struct sr_trigger_stage*) slist->data;
		sr_spew("trigger with name %s found\n", trigger->name);
		while( slist != NULL ) {
			GSList *mlist = stage->matches;
			sr_spew(" stage no 0x%x\n", stage->stage);
			while( mlist != NULL ) {
				struct sr_trigger_match *match =
					(struct sr_trigger_match*) mlist->data;
				int chan_no = atoi(match->channel->name);
				assert(chan_no >= 0 && chan_no < 16);
				sr_spew("  match channel: %s\n", match->channel->name);
				/* Damn these naming conventions!
				 * Each stage has a list of matches, each match consisting
				 * of a channel and the type of trigger ("match"), i.e high, low, rising... */
				sr_spew("  match match:   %d\n", match->match);
				switch(match->match) {
				case SR_TRIGGER_ZERO:   *low_mask  |= 1<<chan_no; break;
				case SR_TRIGGER_ONE:    *high_mask |= 1<<chan_no; break;
				case SR_TRIGGER_RISING: *rise_mask |= 1<<chan_no; break;
				case SR_TRIGGER_EDGE:   *rise_mask |= 1<<chan_no; /* Fall through!*/
				case SR_TRIGGER_FALLING:*fall_mask |= 1<<chan_no; break;
				default:
					sr_err("Unhandled trigger: 0x%x\n", match->match);
				}
				mlist = g_slist_next(mlist);
			}
			slist = g_slist_next(slist);
			if( slist != NULL )
				sr_warn("Staged triggers are not supported with this device!\n");
		}
	}
	else
		sr_spew("trigger is NULL\n");
}

SR_PRIV GSList* digilent_analogdiscovery2_scan(
	struct sr_dev_driver *di,
	GSList *options)
{
	int numDevs;
	struct drv_context *drvc;
	GSList *devices;
	struct sr_dev_inst *sdi;

	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* TODO: test this on a EExplorer, and see if it would work */
	FDwfEnum(enumfilterAll, &numDevs);

	for( int dev=0; dev<numDevs; dev++)
	{ 
		char enum_string[32]; // they are all of size '32' in DWF. 
		/* TODO: are devices we don't have permissions to listed?
		 *       i.e. do we need to filter them here?
		 */
		
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		/* TODO: some other drives set status here to _INACTIVE,
		         check which is correct */
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("Digilent");

		FDwfEnumDeviceName(dev, enum_string);
		sdi->model = g_strdup(enum_string);

		/* Seems there are no device versions as such, but the since
		 * the waveforms library has firmwares for the devices,
		 * that is sort of a de-facto device version */
		FDwfGetVersion(enum_string);
		sdi->version = g_strdup(enum_string);

		FDwfEnumSN(dev, enum_string);
		sdi->serial_num = g_strdup(enum_string);

		/* And from filling in the sr_dev_inst 
		 * to filling in the dev_context. 
		 * What is the semantic difference here?
		 */
		char channel_name[16];
		struct dev_context *devc;
		struct sr_channel *ch;
		struct sr_channel_group *cg;

		devc = g_malloc0(sizeof(struct dev_context));
		sdi->priv = devc;
		devc->enum_idx = dev;
		devc->is_opened = false;
		/* TODO: what should the default be, and how to sync with 
		 * api.c's samplerates[] ? */
		devc->cur_samplerate = SR_HZ(100);

		/* Put all Logic channels into one channel group. 
		 * Because the 'demo' device does this */
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");
		for (int j=0; j<NUM_LOGIC_CHAN; j++) {
			snprintf(channel_name, 16, "%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		/* TODO: add analog and pattern generator channels */

		devices = g_slist_append(devices, sdi);
	}

	/* TODO: This is some sigrok internal stuff, I didn't find any documentation
	 * but seems this is how other drivers do it.*/
	return std_scan_complete(di, devices);
}

SR_PRIV int digilent_analogdiscovery2_close(struct sr_dev_inst *sdi)
{

	if(sdi) {
		struct dev_context *devc = (struct dev_context *)(sdi->priv);
		if(devc) {
			if(devc->is_opened) {
				FDwfDeviceClose((HDWF)(devc->hdwf));
				devc->is_opened=false;
			}
			// should devc be freed here?
		}
		// should sdi be freed here?
	}
	return SR_OK;
}
SR_PRIV int digilent_analogdiscovery2_open(struct sr_dev_inst *sdi)
{

	if( sdi == NULL )
		return SR_ERR_ARG;

	struct dev_context *devc = (struct dev_context *)(sdi->priv);
	if( devc->is_opened ) {
		/* TODO: is this correct? Or just return an SR_OK here? */
		sr_err("Device %d already open", devc->enum_idx);
		return SR_ERR_BUG;
	}

	if(FDwfDeviceOpen(devc->enum_idx, &(devc->hdwf))) {
		devc->is_opened = true;
		return SR_OK;
	}

	sr_err("Error opening device number %d", devc->enum_idx);
	devc->is_opened = false;
	return SR_ERR_BUG;
}
