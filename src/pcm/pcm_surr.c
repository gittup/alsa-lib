/*
 *  PCM - Surround plugin
 *  Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
 *
 *  This plugin offers 4.0 and 5.1 surround devices with these routing:
 *
 *   1st channel - front left speaker
 *   2nd channel - front rear speaker
 *   3rd channel - rear left speaker
 *   4th channel - rear right speaker
 *   5th channel - center speaker
 *   6th channel - LFE channel (woofer)
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <byteswap.h>
#include <limits.h>
#include <ctype.h>
#include <sys/shm.h>
#include "../control/control_local.h"
#include "pcm_local.h"
#include "pcm_plugin.h"

#ifndef DATADIR
#define DATADIR "/usr/share"
#endif
#define ALSA_SURROUND_FILE DATADIR "/alsa/surround.conf"

#define SURR_CAP_CAPTURE	(1<<0)
#define SURR_CAP_4CH		(1<<1)
#define SURR_CAP_6CH		(1<<2)

typedef struct _snd_pcm_surround snd_pcm_surround_t;

struct _snd_pcm_surround {
	int card;		/* card number */
	int device;		/* device number */
	unsigned int channels;	/* count of channels (4 or 6) */
	int pcms;		/* count of PCM channels */
	int use_fd;		/* use this FD for the direct access */
	snd_pcm_t *pcm[3];	/* up to three PCM stereo streams */
	int use_route: 1;	/* route is used */
	int route[6];		/* channel route */
	int linked[3];		/* streams are linked */
	snd_ctl_t *ctl;		/* CTL handle */
	unsigned int caps;	/* capabilities */
	snd_sctl_t *store;	/* control store container */
};

static int snd_pcm_surround_free(snd_pcm_surround_t *surr);

static int snd_pcm_surround_close(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_surround_free(surr);
}

static int snd_pcm_surround_nonblock(snd_pcm_t *pcm, int nonblock)
{
	int i, err = 0, err1;
	snd_pcm_surround_t *surr = pcm->private_data;
	for (i = 0; i < surr->pcms; i++) {
		err1 = snd_pcm_nonblock(surr->pcm[i], nonblock);
		if (err1 && !err)
			err = err1;
	}
	return err;
}

static int snd_pcm_surround_async(snd_pcm_t *pcm, int sig, pid_t pid)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_async(surr->pcm[0], sig, pid);
}

static int snd_pcm_surround_info(snd_pcm_t *pcm, snd_pcm_info_t * info)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	memset(info, 0, sizeof(*info));
	info->stream = snd_enum_to_int(pcm->stream);
	info->card = surr->card;
	strncpy(info->id, "Surround", sizeof(info->id));
	strncpy(info->name, "Surround", sizeof(info->name));
	strncpy(info->subname, "Surround", sizeof(info->subname));
	info->subdevices_count = 1;
	return 0;
}

static int snd_pcm_surround_channel_info(snd_pcm_t *pcm, snd_pcm_channel_info_t * info)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	int err, old_channel, use_pcm;

	old_channel = info->channel;
	if (old_channel < 0 || old_channel > 5)
		return -EINVAL;
	info->channel = surr->route[old_channel];
	use_pcm = 0;
	if (surr->pcms > 1) {
		use_pcm = info->channel / 2;
		info->channel %= 2;
	}
	if (surr->pcm[use_pcm] == NULL) {
		info->channel = old_channel;
		return -EINVAL;
	}
	err = snd_pcm_channel_info(surr->pcm[use_pcm], info);
	info->channel = old_channel;
	return err;
}

static int snd_pcm_surround_status(snd_pcm_t *pcm, snd_pcm_status_t * status)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_status(surr->pcm[0], status);
}

static snd_pcm_state_t snd_pcm_surround_state(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_state(surr->pcm[0]);
}

static int snd_pcm_surround_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_delay(surr->pcm[0], delayp);
}

static int snd_pcm_surround_prepare(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_prepare(surr->pcm[0]);
}

static int snd_pcm_surround_reset(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_reset(surr->pcm[0]);
}

static int snd_pcm_surround_start(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_start(surr->pcm[0]);
}

static int snd_pcm_surround_drop(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_drop(surr->pcm[0]);
}

static int snd_pcm_surround_drain(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_drain(surr->pcm[0]);
}

static int snd_pcm_surround_pause(snd_pcm_t *pcm, int enable)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_pause(surr->pcm[0], enable);
}

static snd_pcm_sframes_t snd_pcm_surround_rewind(snd_pcm_t *pcm, snd_pcm_uframes_t frames)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_rewind(surr->pcm[0], frames);
}

static snd_pcm_sframes_t snd_pcm_surround_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	if (surr->pcms == 1 && !surr->use_route)
		return snd_pcm_writei(surr->pcm[0], buffer, size);
	if (pcm->running_areas == NULL) {
		int err;
		if ((err = snd_pcm_mmap(pcm)) < 0)
			return err;
	}
	return snd_pcm_mmap_writei(pcm, buffer, size);
}

static snd_pcm_sframes_t snd_pcm_surround_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	int i;
	snd_pcm_sframes_t res = -1, res1;
	snd_pcm_surround_t *surr = pcm->private_data;
	if (surr->use_route) {
		if (pcm->running_areas == NULL) {
			int err;
			if ((err = snd_pcm_mmap(pcm)) < 0)
				return err;
		}
		return snd_pcm_mmap_writen(pcm, bufs, size);
	}
	for (i = 0; i < surr->pcms; i++, bufs += 2) {
		res1 = snd_pcm_writen(pcm, bufs, size);
		if (res1 < 0)
			return res1;
		if (res < 0)
			res = res1;
		else if (res != res1)
			return -EPIPE;
	}
	return res;
}

static snd_pcm_sframes_t snd_pcm_surround_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	if (surr->pcms == 1 && !surr->use_route)
		return snd_pcm_readi(surr->pcm[0], buffer, size);
	if (pcm->running_areas == NULL) {
		int err;
		if ((err = snd_pcm_mmap(pcm)) < 0)
			return err;
	}
	return snd_pcm_mmap_readi(pcm, buffer, size);
}

static snd_pcm_sframes_t snd_pcm_surround_readn(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	int i;
	snd_pcm_sframes_t res = -1, res1;
	snd_pcm_surround_t *surr = pcm->private_data;
	if (surr->use_route) {
		if (pcm->running_areas == NULL) {
			int err;
			if ((err = snd_pcm_mmap(pcm)) < 0)
				return err;
		}
		return snd_pcm_mmap_readn(pcm, bufs, size);
	}
	for (i = 0; i < surr->pcms; i++) {
		res1 = snd_pcm_writen(pcm, bufs, size);
		if (res1 < 0)
			return res1;
		if (res < 0) {
			res = size = res1;
		} else if (res != res1)
			return -EPIPE;
	}
	return res;
}

static snd_pcm_sframes_t snd_pcm_surround_mmap_commit(snd_pcm_t *pcm, snd_pcm_uframes_t offset, snd_pcm_uframes_t size)
{
	int i;
	snd_pcm_sframes_t res = -1, res1;
	snd_pcm_surround_t *surr = pcm->private_data;
	for (i = 0; i < surr->pcms; i++) {
		res1 = snd_pcm_mmap_commit(surr->pcm[i], offset, size);
		if (res1 < 0)
			return res1;
		if (res < 0) {
			res = size = res1;
		} else if (res != res1)
			return -EPIPE;
	}
	return res;
}

static snd_pcm_sframes_t snd_pcm_surround_avail_update(snd_pcm_t *pcm)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	return snd_pcm_avail_update(surr->pcm[0]);
}

static int snd_pcm_surround_interval_channels(snd_pcm_surround_t *surr,
					      snd_pcm_hw_params_t *params,
					      int refine)
{
	snd_interval_t *interval;
	interval = &params->intervals[SND_PCM_HW_PARAM_CHANNELS-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
	if (interval->empty)
		return -EINVAL;
	if (interval->openmin) {
		if (!refine) {
			interval->empty = 1;
			return -EINVAL;
		}
		interval->min = surr->channels;
		interval->openmin = 0;
	}
	if (interval->openmax) {
		if (!refine) {
			interval->empty = 1;
			return -EINVAL;
		}
		interval->max = surr->channels;
		interval->openmax = 0;
	}
	if (refine && interval->min <= surr->channels && interval->max >= surr->channels)
		interval->min = interval->max = surr->channels;
	if (interval->min != interval->max || interval->min != surr->channels) {
		interval->empty = 1;
		return -EINVAL;
	}
	if (surr->pcms != 1)
		interval->min = interval->max = 2;
	return 0;
}

static void snd_pcm_surround_interval_channels_fixup(snd_pcm_surround_t *surr,
						     snd_pcm_hw_params_t *params)
{
	snd_interval_t *interval;
	interval = &params->intervals[SND_PCM_HW_PARAM_CHANNELS-SNDRV_PCM_HW_PARAM_FIRST_INTERVAL];
	interval->min = interval->max = surr->channels;
}

static int snd_pcm_surround_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	snd_pcm_access_mask_t *access_mask;
	snd_pcm_access_mask_t *access_mask1;
	int i, err;

	err = snd_pcm_surround_interval_channels(surr, params, 1);
	if (err < 0)
		return err;
	if (surr->pcms == 1 && !surr->use_route)
		return snd_pcm_hw_refine(surr->pcm[0], params);
	access_mask = (snd_pcm_access_mask_t *)snd_pcm_hw_param_get_mask(params, SND_PCM_HW_PARAM_ACCESS);
	snd_pcm_access_mask_alloca(&access_mask1);
	snd_pcm_access_mask_copy(access_mask1, access_mask);
	snd_pcm_access_mask_reset(access_mask, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (snd_pcm_access_mask_test(access_mask1, SND_PCM_ACCESS_RW_INTERLEAVED))
		snd_pcm_access_mask_set(access_mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
	for (i = 0; i < surr->pcms; i++) {
		err = snd_pcm_hw_refine(surr->pcm[i], params);
		if (err < 0)
			goto __error;
	}
	err = 0;
      __error:
	snd_pcm_access_mask_none(access_mask);
	if (snd_pcm_access_mask_test(access_mask1, SND_PCM_ACCESS_RW_INTERLEAVED))
		snd_pcm_access_mask_set(access_mask, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (snd_pcm_access_mask_test(access_mask1, SND_PCM_ACCESS_RW_NONINTERLEAVED))
		snd_pcm_access_mask_set(access_mask, SND_PCM_ACCESS_RW_NONINTERLEAVED);
	if (snd_pcm_access_mask_test(access_mask1, SND_PCM_ACCESS_MMAP_COMPLEX))
		snd_pcm_access_mask_set(access_mask, SND_PCM_ACCESS_MMAP_COMPLEX);
	snd_pcm_surround_interval_channels_fixup(surr, params);
	return err;
}

static int snd_pcm_surround_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t * params)
{
	snd_pcm_surround_t *surr = pcm->private_data;
	snd_pcm_access_mask_t *access_mask;
	snd_pcm_access_mask_t *access_mask1;
	int i, err;
	
	err = snd_pcm_surround_interval_channels(surr, params, 0);
	if (err < 0)
		return err;
	if (surr->pcms == 1 && !surr->use_route)
		return snd_pcm_hw_params(surr->pcm[0], params);
	access_mask = (snd_pcm_access_mask_t *)snd_pcm_hw_param_get_mask(params, SND_PCM_HW_PARAM_ACCESS);
	snd_pcm_access_mask_alloca(&access_mask1);
	snd_pcm_access_mask_copy(access_mask1, access_mask);
	snd_pcm_access_mask_reset(access_mask, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (snd_pcm_access_mask_test(access_mask1, SND_PCM_ACCESS_RW_INTERLEAVED))
		snd_pcm_access_mask_set(access_mask, SND_PCM_ACCESS_MMAP_INTERLEAVED);
	for (i = 0; i < surr->pcms; i++) {
		err = snd_pcm_hw_params(surr->pcm[i], params);
		if (err < 0) {
			snd_pcm_access_mask_copy(access_mask, access_mask1);
			snd_pcm_surround_interval_channels_fixup(surr, params);
			return err;
		}
	}
	snd_pcm_access_mask_copy(access_mask, access_mask1);
	snd_pcm_surround_interval_channels_fixup(surr, params);
	surr->linked[0] = 0;
	for (i = 1; i < surr->pcms; i++) {
		err = snd_pcm_link(surr->pcm[0], surr->pcm[i]);
		if ((surr->linked[i] = (err >= 0)) == 0)
			return err;
	}
	return 0;
}

static int snd_pcm_surround_hw_free(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	int i, err, res = 0;
	snd_pcm_surround_t *surr = pcm->private_data;
	for (i = 0; i < surr->pcms; i++) {
		err = snd_pcm_hw_free(surr->pcm[i]);
		if (err < 0)
			res = err;
		if (!surr->linked[i])
			continue;
		surr->linked[i] = 0;
		err = snd_pcm_unlink(surr->pcm[i]);
		if (err < 0)
			res = err;
	}
	return res;
}

static int snd_pcm_surround_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t * params)
{
	int i, err;
	snd_pcm_surround_t *surr = pcm->private_data;
	for (i = 0; i < surr->pcms; i++) {
		err = snd_pcm_sw_params(surr->pcm[i], params);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_pcm_surround_mmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	// snd_pcm_surround_t *surr = pcm->private_data;
	return 0;
}

static int snd_pcm_surround_munmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	// snd_pcm_surround_t *surr = pcm->private_data;
	return 0;
}

static void snd_pcm_surround_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_output_printf(out, "Surround PCM\n");
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
}

snd_pcm_ops_t snd_pcm_surround_ops = {
	close: snd_pcm_surround_close,
	info: snd_pcm_surround_info,
	hw_refine: snd_pcm_surround_hw_refine,
	hw_params: snd_pcm_surround_hw_params,
	hw_free: snd_pcm_surround_hw_free,
	sw_params: snd_pcm_surround_sw_params,
	channel_info: snd_pcm_surround_channel_info,
	dump: snd_pcm_surround_dump,
	nonblock: snd_pcm_surround_nonblock,
	async: snd_pcm_surround_async,
	mmap: snd_pcm_surround_mmap,
	munmap: snd_pcm_surround_munmap,
};

snd_pcm_fast_ops_t snd_pcm_surround_fast_ops = {
	status: snd_pcm_surround_status,
	state: snd_pcm_surround_state,
	delay: snd_pcm_surround_delay,
	prepare: snd_pcm_surround_prepare,
	reset: snd_pcm_surround_reset,
	start: snd_pcm_surround_start,
	drop: snd_pcm_surround_drop,
	drain: snd_pcm_surround_drain,
	pause: snd_pcm_surround_pause,
	rewind: snd_pcm_surround_rewind,
	writei: snd_pcm_surround_writei,
	writen: snd_pcm_surround_writen,
	readi: snd_pcm_surround_readi,
	readn: snd_pcm_surround_readn,
	avail_update: snd_pcm_surround_avail_update,
	mmap_commit: snd_pcm_surround_mmap_commit,
};

static int snd_pcm_surround_free(snd_pcm_surround_t *surr)
{
	int i;

	assert(surr);
	for (i = 2; i >= 0; i--) {
		if (surr->pcm[i] == NULL)
			continue;
		snd_pcm_close(surr->pcm[i]);
		surr->pcm[i] = NULL;
	}
	if (surr->store)
		snd_sctl_free(surr->ctl, surr->store);
	if (surr->ctl)
		snd_ctl_close(surr->ctl);
	free(surr);
	return 0;
}

static int snd_pcm_surround_one_stream(snd_pcm_surround_t *surr,
				       snd_pcm_surround_type_t type ATTRIBUTE_UNUSED,
				       int card,
				       int dev, int subdev,
				       snd_pcm_stream_t stream, int mode)
{
	int err;

	if ((err = snd_pcm_hw_open(&surr->pcm[0], "Surround", card, dev,
				   subdev, stream, mode)) < 0)
		return err;
	surr->pcms++;
	return 0;
}

static int snd_pcm_surround_three_streams(snd_pcm_surround_t *surr,
					  snd_pcm_surround_type_t type,
					  int card,
					  int dev0, int subdev0,
					  int dev1, int subdev1,
					  int dev2, int subdev2,
					  snd_pcm_stream_t stream, int mode)
{
	int err;

	if ((err = snd_pcm_hw_open(&surr->pcm[0], "Surround L/R", card, dev0,
				   subdev0, stream, mode)) < 0)
		return err;
	surr->pcms++;
	if ((err = snd_pcm_hw_open(&surr->pcm[1], "Surround Rear L/R", card, dev1,
				   subdev1, stream, mode)) < 0)
		return err;
	surr->pcms++;
	if (type == SND_PCM_SURROUND_51) {
		if ((err = snd_pcm_hw_open(&surr->pcm[2], "Surround Center/LFE", card, dev2,
					   subdev2, stream, mode)) < 0)
			return err;
		surr->pcms++;
	}
	return 0;
}

static int build_config(snd_config_t **r_conf)
{
	int err;
	snd_input_t *in;
	snd_config_t *conf, *file;
	const char *filename = ALSA_SURROUND_FILE;

	assert(r_conf);
	*r_conf = NULL;
	if ((err = snd_config_update()) < 0)
		return err;
	if ((err = snd_config_search(snd_config, "surround_file", &file)) >= 0) {
		if ((err = snd_config_get_string(file, &filename)) < 0) {
			SNDERR("cards_file definition must be string");
			filename = ALSA_SURROUND_FILE;
		}
	}
	if ((err = snd_input_stdio_open(&in, filename, "r")) < 0) {
		SNDERR("unable to open configuration file '%s'", filename);
		return err;
	}
	if ((err = snd_config_top(&conf)) < 0) {
		SNDERR("config_top");
		snd_input_close(in);
		return err;
	}
	if ((err = snd_config_load(conf, in)) < 0) {
		SNDERR("config load error");
		snd_config_delete(conf);
		snd_input_close(in);
		return err;
	}
	snd_input_close(in);
	*r_conf = conf;
	return 0;
}

int load_surround_config(snd_ctl_t *ctl, snd_pcm_surround_t *surr,
			 snd_pcm_surround_type_t stype,
			 snd_card_type_t ctype,
			 snd_pcm_stream_t stream,
			 int mode)
{
	int err, res = -EINVAL;
	snd_config_t *conf = NULL, *surrconf;
	snd_config_iterator_t i, next;

	if ((err = build_config(&conf)) < 0)
		return err;
	if ((err = snd_config_search(conf, "surround_plugin", &surrconf)) < 0) {
		SNDERR("unable to find card definitions");
		snd_config_delete(conf);
		return err;
	}
	if (snd_config_get_type(surrconf) != SND_CONFIG_TYPE_COMPOUND) {
		SNDERR("compound type expected");
		snd_config_delete(conf);
		return err;
	}
	snd_config_for_each(i, next, surrconf) {
		snd_config_t *n = snd_config_iterator_entry(i), *n1;
		const char *id = snd_config_get_id(n);
		snd_card_type_t mytype = (snd_card_type_t)-1;
		int opened = 0;
		if (isdigit(*id)) {
			mytype = (snd_card_type_t)atoi(id);
		} else {
			if (snd_card_type_string_to_enum(id, &mytype) < 0) {
				SNDERR("snd_card_type_string_to_enum %s", id);
				continue;
			}
		}
		if (mytype != ctype)
			continue;
		if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
			SNDERR("compound type expected");
			goto __error;
		}
		if (snd_config_search(n, "device", &n1) >= 0) {
			unsigned long i;
			if ((err = snd_config_get_integer(n1, &i)) < 0) {
				SNDERR("Invalid type for field device");
				goto __error;
			}
			if (surr->device != (int)i)
				continue;
		}
		if (snd_config_search(n, "channels_four", &n1) >= 0) {
			const char *str;
			if ((err = snd_config_get_string(n1, &str)) < 0) {
				SNDERR("Invalid value for %s", id);
				goto __error;
			} else if (!strcasecmp(str, "true")) {
				surr->caps |= SURR_CAP_4CH;
			} else if (isdigit(*str) && atoi(str) != 0)
				surr->caps |= SURR_CAP_4CH;
		}
		if (snd_config_search(n, "channels_six", &n1) >= 0) {
			const char *str;
			if ((err = snd_config_get_string(n1, &str)) < 0) {
				SNDERR("Invalid value for %s", id);
				goto __error;
			} else if (!strcasecmp(str, "true")) {
				surr->caps |= SURR_CAP_6CH;
			} else if (isdigit(*str) && atoi(str) != 0)
				surr->caps |= SURR_CAP_6CH;
		}
		if (snd_config_search(n, "use_fd", &n1) >= 0) {
			unsigned long i;
			if ((err = snd_config_get_integer(n1, &i)) < 0) {
				SNDERR("Invalid type for %s", id);
				goto __error;
			} else if (i <= 2)
				surr->use_fd = i;
			else {
				SNDERR("Invalid range for use_fd (0-2): %li", i);
				goto __error;
			}
		}
		if (snd_config_search(n, "route_four", &n1) >= 0) {
			snd_config_iterator_t i, next;
			snd_config_t *n2;
			if (stype == SND_PCM_SURROUND_40) {
				if (snd_config_get_type(n1) != SND_CONFIG_TYPE_COMPOUND) {
					SNDERR("compound type expected");
					goto __error;
				}
				if (snd_config_search(n1, "channel", &n2) >= 0) {
					snd_config_for_each(i, next, n2) {
						snd_config_t *n = snd_config_iterator_entry(i);
						int idx = atoi(snd_config_get_id(n));
						unsigned long i;
						int err = snd_config_get_integer(n, &i);
						if (err < 0) {
							SNDERR("Invalid field channel.%s", snd_config_get_id(n));
							goto __error;
						}
						if (idx < 0 || idx >= 4) {
							SNDERR("Index is out of range (0-3): %i", idx);
							goto __error;
						}
						if (idx != (int)i)
							surr->use_route = 1;
						surr->route[idx] = i;
					}
				}
			}
		}
		if (snd_config_search(n, "route_six", &n1) >= 0) {
			snd_config_iterator_t i, next;
			snd_config_t *n2;
			if (stype == SND_PCM_SURROUND_51) {
				if (snd_config_get_type(n1) != SND_CONFIG_TYPE_COMPOUND) {
					SNDERR("compound type expected");
					goto __error;
				}
				if (snd_config_search(n1, "channel", &n2) >= 0) {
					snd_config_for_each(i, next, n2) {
						snd_config_t *n = snd_config_iterator_entry(i);
						int idx = atoi(snd_config_get_id(n));
						unsigned long i;
						int err = snd_config_get_integer(n, &i);
						if (err < 0) {
							SNDERR("Invalid field channel.%s", snd_config_get_id(n));
							goto __error;
						}
						if (idx < 0 || idx >= 6) {
							SNDERR("Index is out of range (0-5): %i", idx);
							goto __error;
						}
						if (idx != (int)i)
							surr->use_route = 1;
						surr->route[idx] = i;
					}
				}
			}
		}
		if (snd_config_search(n, "open_single", &n1) >= 0) {
			snd_config_iterator_t i, next;
			int device = 0, subdevice = -1;
			if (snd_config_get_type(n1) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("compound type expected");
				goto __error;
			}
			snd_config_for_each(i, next, n1) {
				snd_config_t *n = snd_config_iterator_entry(i);
				const char *id = snd_config_get_id(n);
				unsigned long i;
				if (!strcmp(id, "device")) {
					if ((err = snd_config_get_integer(n, &i)) < 0) {
						SNDERR("Invalid type for %s", id);
						goto __error;
					}
					device = i;
				} else if (!strcmp(id, "subdevice")) {
					if ((err = snd_config_get_integer(n, &i)) < 0) {
						SNDERR("Invalid type for %s", id);
						goto __error;
					}
					subdevice = i;
				} else {
					SNDERR("Invalid field %s", id);
					goto __error;
				}
			}
			if (stream == SND_PCM_STREAM_CAPTURE && !(surr->caps & SURR_CAP_CAPTURE)) {
				err = -ENODEV;
				goto __error;
			}
			switch (stype) {
			case SND_PCM_SURROUND_40:
				if (!(surr->caps & SURR_CAP_4CH)) {
					err = -ENODEV;
					goto __error;
				}
				break;
			case SND_PCM_SURROUND_51:
				if (!(surr->caps & SURR_CAP_6CH)) {
					err = -ENODEV;
					goto __error;
				}
				break;
			}
			if ((err = snd_pcm_surround_one_stream(surr, stype, surr->card, device, subdevice, stream, mode)) < 0) {
				SNDERR("surround single stream open error %i,%i,%i: %s", surr->card, device, subdevice, snd_strerror(err));
				goto __error;
			}
			opened = 1;
		} else if (snd_config_search(n, "open_multi", &n1) >= 0) {
			snd_config_iterator_t i, next;
			int device[3] = { 0, 0, 0 }, subdevice[3] = { -1, -1, -1 };
			if (snd_config_get_type(n1) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("compound type expected");
				goto __error;
			}
			snd_config_for_each(i, next, n1) {
				snd_config_t *n = snd_config_iterator_entry(i);
				const char *id = snd_config_get_id(n);
				snd_config_iterator_t i, next;
				if (!strcmp(id, "device") || !strcmp(id, "subdevice")) {
					if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
						SNDERR("compound type expected");
						goto __error;
					}
					snd_config_for_each(i, next, n) {
						snd_config_t *n = snd_config_iterator_entry(i);
						const char *id = snd_config_get_id(n);
						int idx = atoi(snd_config_get_id(n));
						unsigned long i;
						if (idx < 0 || idx > 2) {
							SNDERR("Invalid index %s", snd_config_get_id(n));
							goto __error;
						}
						if ((err = snd_config_get_integer(n, &i)) < 0) {
							SNDERR("Invalid type for %s", id);
							goto __error;
						}
						if (!strcmp(id, "device"))
							device[idx] = i;
						else
							subdevice[idx] = i;
					}
				} else {
					SNDERR("Invalid field %s", id);
					goto __error;
				}
			}
			if (stream == SND_PCM_STREAM_CAPTURE && !(surr->caps & SURR_CAP_CAPTURE)) {
				err = -ENODEV;
				goto __error;
			}
			switch (stype) {
			case SND_PCM_SURROUND_40:
				if (!(surr->caps & SURR_CAP_4CH)) {
					err = -ENODEV;
					goto __error;
				}
				break;
			case SND_PCM_SURROUND_51:
				if (!(surr->caps & SURR_CAP_6CH)) {
					err = -ENODEV;
					goto __error;
				}
				break;
			}
			if ((err = snd_pcm_surround_three_streams(surr, stype, surr->card, device[0], subdevice[0], device[1], subdevice[1], device[2], subdevice[2], stream, mode)) < 0) {
				SNDERR("surround single stream open error %i,%i,%i,%i,%i,%i,%i: %s", surr->card, device[0], subdevice[0], device[1], subdevice[1], device[2], subdevice[2], snd_strerror(err));
				goto __error;
			}
			opened = 1;
		}
		if (opened == 0) {
			err = -ENODEV;
			goto __error;
		}
		if (snd_config_search(n, "open_control", &n1) >= 0) {
			snd_sctl_replace_t replace[4];
			char values[3][10] = { "123", "123", "123" };
			snd_pcm_info_t *info;
			int ridx = 0;
			
			snd_pcm_info_alloca(&info);
			if ((err = snd_pcm_info(surr->pcm[0], info)) < 0) {
				SNDERR("snd_pcm_info failed", snd_strerror(err));
				goto __error;
			}
			sprintf(values[0], "%i", snd_pcm_info_get_subdevice(info));
			replace[ridx].key = "index";
			replace[ridx].old_value = "subdevice0";
			replace[ridx].new_value = values[0];
			ridx++;
			
			if (surr->pcm[1]) {
				if ((err = snd_pcm_info(surr->pcm[1], info)) < 0) {
					SNDERR("snd_pcm_info failed", snd_strerror(err));
					goto __error;
				}
				sprintf(values[1], "%i", snd_pcm_info_get_subdevice(info));
				replace[ridx].key = "index";
				replace[ridx].old_value = "subdevice1";
				replace[ridx].new_value = values[1];
				ridx++;
			}
			if (surr->pcm[2]) {
				if ((err = snd_pcm_info(surr->pcm[2], info)) < 0) {
					SNDERR("snd_pcm_info failed", snd_strerror(err));
					goto __error;
				}
				sprintf(values[2], "%i", snd_pcm_info_get_subdevice(info));
				replace[ridx].key = "index";
				replace[ridx].old_value = "subdevice2";
				replace[ridx].new_value = values[2];
				ridx++;
			}
			replace[ridx].key = NULL;
			replace[ridx].old_value = NULL;
			replace[ridx].new_value = NULL;
			if ((err = snd_sctl_build(ctl, &surr->store, n1, replace)) < 0) {
				SNDERR("snd_sctl_build : %s\n", snd_strerror(err));
				goto __error;
			}
		}
		return 0;
	}
	res = -ENOENT;
	SNDERR("configuration for card %i not found", (int)ctype);
      __error:
	snd_config_delete(conf);
	return res;
}

int snd_pcm_surround_open(snd_pcm_t **pcmp, const char *name, int card, int dev,
			  snd_pcm_surround_type_t type,
			  snd_pcm_stream_t stream, int mode)
{
	snd_pcm_t *pcm = NULL;
	snd_pcm_surround_t *surr;
	snd_ctl_t *ctl = NULL;
	snd_ctl_card_info_t *info;
	snd_card_type_t ctype;
	int err, idx;

	assert(pcmp);
	surr = calloc(1, sizeof(snd_pcm_surround_t));
	if (!surr)
		return -ENOMEM;
	switch (type) {
	case SND_PCM_SURROUND_40:
		surr->channels = 4;
		break;
	case SND_PCM_SURROUND_51:
		surr->channels = 6;
		break;
	default:
		snd_pcm_surround_free(surr);
		return -EINVAL;
	}
	if ((err = snd_ctl_hw_open(&ctl, "Surround", card, 0)) < 0) {
		snd_pcm_surround_free(surr);
		return err;
	}
	surr->ctl = ctl;
	surr->card = card;
	surr->device = dev;
	surr->caps = SURR_CAP_4CH;
	for (idx = 0; idx < 6; idx++)
		surr->route[idx] = idx;
	snd_ctl_card_info_alloca(&info);
	if ((err = snd_ctl_card_info(ctl, info)) < 0)
		goto __error;
	ctype = snd_ctl_card_info_get_type(info);
	if ((err = load_surround_config(ctl, surr, type, ctype, stream, mode)) < 0)
		goto __error;
	if (surr->store == NULL) {
		snd_ctl_close(ctl);
		surr->ctl = NULL;
	}
	ctl = NULL;
	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		err = -ENOMEM;
		goto __error;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_SURROUND;
	pcm->stream = stream;
	pcm->mode = mode;
	pcm->ops = &snd_pcm_surround_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_surround_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private_data = surr;
	pcm->poll_fd = surr->pcm[surr->use_fd]->poll_fd;
	pcm->hw_ptr = surr->pcm[surr->use_fd]->hw_ptr;
	pcm->appl_ptr = surr->pcm[surr->use_fd]->appl_ptr;
	
	*pcmp = pcm;

	return 0;

      __error:
	snd_pcm_surround_free(surr);
	return err;
}

int _snd_pcm_surround_open(snd_pcm_t **pcmp, const char *name, snd_config_t *conf,
			   snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	long card = -1, device = 0;
	const char *str;
	int err;
	snd_pcm_surround_type_t type = SND_PCM_SURROUND_40;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "type") == 0)
			continue;
		if (strcmp(id, "card") == 0) {
			err = snd_config_get_integer(n, &card);
			if (err < 0) {
				err = snd_config_get_string(n, &str);
				if (err < 0) {
					SNDERR("Invalid type for %s", id);
					return -EINVAL;
				}
				card = snd_card_get_index(str);
				if (card < 0) {
					SNDERR("Invalid value for %s", id);
					return card;
				}
			}
			continue;
		}
		if (strcmp(id, "device") == 0) {
			err = snd_config_get_integer(n, &device);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return err;
			}
			continue;
		}
#if 0
		if (strcmp(id, "subdevice") == 0) {
			err = snd_config_get_integer(n, &subdevice);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return err;
			}
			continue;
		}
#endif
		if (strcmp(id, "stype") == 0) {
			err = snd_config_get_string(n, &str);
			if (strcmp(str, "40") == 0 || strcmp(str, "4.0") == 0)
				type = SND_PCM_SURROUND_40;
			else if (strcmp(str, "51") == 0 || strcmp(str, "5.1") == 0)
				type = SND_PCM_SURROUND_51;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (card < 0) {
		SNDERR("card is not defined");
		return -EINVAL;
	}
	return snd_pcm_surround_open(pcmp, name, card, device, type, stream, mode);
}