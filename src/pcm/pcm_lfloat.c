/*
 *  PCM - Linear Integer <-> Linear Float conversion
 *  Copyright (c) 2001 by Jaroslav Kysela <perex@suse.cz>
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
#include "pcm_local.h"
#include "pcm_plugin.h"

typedef float float_t;
typedef double double_t;

#ifndef PIC
/* entry for static linking */
const char *_snd_module_pcm_float = "";
#endif

typedef struct {
	/* This field need to be the first */
	snd_pcm_plugin_t plug;
	unsigned int int32_idx;
	unsigned int float32_idx;
	snd_pcm_format_t sformat;
	void (*func)(const snd_pcm_channel_area_t *dst_areas, snd_pcm_uframes_t dst_offset,
		     const snd_pcm_channel_area_t *src_areas, snd_pcm_uframes_t src_offset,
		     unsigned int channels, snd_pcm_uframes_t frames,
		     unsigned int get32idx, unsigned int put32floatidx);
} snd_pcm_lfloat_t;

int snd_pcm_lfloat_get_s32_index(snd_pcm_format_t format)
{
	int width, endian;

	switch (format) {
	case SND_PCM_FORMAT_FLOAT_LE:
	case SND_PCM_FORMAT_FLOAT_BE:
		width = 32;
		break;
	case SND_PCM_FORMAT_FLOAT64_LE:
	case SND_PCM_FORMAT_FLOAT64_BE:
		width = 64;
		break;
	default:
		return -EINVAL;
	}
#ifdef SND_LITTLE_ENDIAN
	endian = snd_pcm_format_big_endian(format);
#else
	endian = snd_pcm_format_little_endian(format);
#endif
	return ((width / 32)-1) * 2 + endian;
}

int snd_pcm_lfloat_put_s32_index(snd_pcm_format_t format)
{
	return snd_pcm_lfloat_get_s32_index(format);
}

void snd_pcm_lfloat_convert_integer_float(const snd_pcm_channel_area_t *dst_areas, snd_pcm_uframes_t dst_offset,
					  const snd_pcm_channel_area_t *src_areas, snd_pcm_uframes_t src_offset,
					  unsigned int channels, snd_pcm_uframes_t frames,
					  unsigned int get32idx, unsigned int put32floatidx)
{
#define GET32_LABELS
#define PUT32F_LABELS
#include "plugin_ops.h"
#undef PUT32F_LABELS
#undef GET32_LABELS
	void *get32 = get32_labels[get32idx];
	void *put32float = put32float_labels[put32floatidx];
	unsigned int channel;
	for (channel = 0; channel < channels; ++channel) {
		const char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		int32_t sample = 0;
		float tmp_float;
		double tmp_double;
		const snd_pcm_channel_area_t *src_area = &src_areas[channel];
		const snd_pcm_channel_area_t *dst_area = &dst_areas[channel];
		src = snd_pcm_channel_area_addr(src_area, src_offset);
		dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
		src_step = snd_pcm_channel_area_step(src_area);
		dst_step = snd_pcm_channel_area_step(dst_area);
		frames1 = frames;
		while (frames1-- > 0) {
			goto *get32;
#define GET32_END sample_loaded
#include "plugin_ops.h"
#undef GET32_END
		sample_loaded:
			goto *put32float;
#define PUT32F_END sample_put
#include "plugin_ops.h"
#undef PUT32F_END
		sample_put:
			src += src_step;
			dst += dst_step;
		}
	}
}

void snd_pcm_lfloat_convert_float_integer(const snd_pcm_channel_area_t *dst_areas, snd_pcm_uframes_t dst_offset,
					  const snd_pcm_channel_area_t *src_areas, snd_pcm_uframes_t src_offset,
					  unsigned int channels, snd_pcm_uframes_t frames,
					  unsigned int put32idx, unsigned int get32floatidx)
{
#define PUT32_LABELS
#define GET32F_LABELS
#include "plugin_ops.h"
#undef GET32F_LABELS
#undef PUT32_LABELS
	void *put32 = put32_labels[put32idx];
	void *get32float = get32float_labels[get32floatidx];
	unsigned int channel;
	for (channel = 0; channel < channels; ++channel) {
		const char *src;
		char *dst;
		int src_step, dst_step;
		snd_pcm_uframes_t frames1;
		int32_t sample;
		int64_t sample64;
		float tmp_float;
		double tmp_double;
		const snd_pcm_channel_area_t *src_area = &src_areas[channel];
		const snd_pcm_channel_area_t *dst_area = &dst_areas[channel];
		src = snd_pcm_channel_area_addr(src_area, src_offset);
		dst = snd_pcm_channel_area_addr(dst_area, dst_offset);
		src_step = snd_pcm_channel_area_step(src_area);
		dst_step = snd_pcm_channel_area_step(dst_area);
		frames1 = frames;
		while (frames1-- > 0) {
			goto *get32float;
#define GET32F_END sample_loaded
#include "plugin_ops.h"
#undef GET32F_END
		sample_loaded:
			goto *put32;
#define PUT32_END sample_put
#include "plugin_ops.h"
#undef PUT32_END
		sample_put:
			src += src_step;
			dst += dst_step;
		}
	}
}

static int snd_pcm_lfloat_hw_refine_cprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	int err;
	snd_pcm_access_mask_t access_mask = { SND_PCM_ACCBIT_SHM };
	snd_pcm_format_mask_t lformat_mask = { SND_PCM_FMTBIT_LINEAR };
	snd_pcm_format_mask_t fformat_mask = { SND_PCM_FMTBIT_FLOAT };
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_ACCESS,
					 &access_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_param_set_mask(params, SND_PCM_HW_PARAM_FORMAT,
					 snd_pcm_format_linear(lfloat->sformat) ?
					 &fformat_mask : &lformat_mask);
	if (err < 0)
		return err;
	err = _snd_pcm_hw_params_set_subformat(params, SND_PCM_SUBFORMAT_STD);
	if (err < 0)
		return err;
	params->info &= ~(SND_PCM_INFO_MMAP | SND_PCM_INFO_MMAP_VALID);
	return 0;
}

static int snd_pcm_lfloat_hw_refine_sprepare(snd_pcm_t *pcm, snd_pcm_hw_params_t *sparams)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	snd_pcm_access_mask_t saccess_mask = { SND_PCM_ACCBIT_MMAP };
	_snd_pcm_hw_params_any(sparams);
	_snd_pcm_hw_param_set_mask(sparams, SND_PCM_HW_PARAM_ACCESS,
				   &saccess_mask);
	_snd_pcm_hw_params_set_format(sparams, lfloat->sformat);
	_snd_pcm_hw_params_set_subformat(sparams, SND_PCM_SUBFORMAT_STD);
	return 0;
}

static int snd_pcm_lfloat_hw_refine_schange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(sparams, links, params);
	if (err < 0)
		return err;
	return 0;
}
	
static int snd_pcm_lfloat_hw_refine_cchange(snd_pcm_t *pcm ATTRIBUTE_UNUSED, snd_pcm_hw_params_t *params,
					    snd_pcm_hw_params_t *sparams)
{
	int err;
	unsigned int links = (SND_PCM_HW_PARBIT_CHANNELS |
			      SND_PCM_HW_PARBIT_RATE |
			      SND_PCM_HW_PARBIT_PERIOD_SIZE |
			      SND_PCM_HW_PARBIT_BUFFER_SIZE |
			      SND_PCM_HW_PARBIT_PERIODS |
			      SND_PCM_HW_PARBIT_PERIOD_TIME |
			      SND_PCM_HW_PARBIT_BUFFER_TIME |
			      SND_PCM_HW_PARBIT_TICK_TIME);
	err = _snd_pcm_hw_params_refine(params, links, sparams);
	if (err < 0)
		return err;
	return 0;
}

static int snd_pcm_lfloat_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	return snd_pcm_hw_refine_slave(pcm, params,
				       snd_pcm_lfloat_hw_refine_cprepare,
				       snd_pcm_lfloat_hw_refine_cchange,
				       snd_pcm_lfloat_hw_refine_sprepare,
				       snd_pcm_lfloat_hw_refine_schange,
				       snd_pcm_plugin_hw_refine_slave);
}

static int snd_pcm_lfloat_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	snd_pcm_t *slave = lfloat->plug.slave;
	snd_pcm_format_t src_format, dst_format;
	int err = snd_pcm_hw_params_slave(pcm, params,
					  snd_pcm_lfloat_hw_refine_cchange,
					  snd_pcm_lfloat_hw_refine_sprepare,
					  snd_pcm_lfloat_hw_refine_schange,
					  snd_pcm_plugin_hw_params_slave);
	if (err < 0)
		return err;
	if (pcm->stream == SND_PCM_STREAM_PLAYBACK) {
		src_format = snd_pcm_hw_params_get_format(params);
		dst_format = slave->format;
	} else {
		src_format = slave->format;
		dst_format = snd_pcm_hw_params_get_format(params);
	}
	if (snd_pcm_format_linear(src_format)) {
		lfloat->int32_idx = snd_pcm_linear_get_index(src_format, SND_PCM_FORMAT_S32);
		lfloat->float32_idx = snd_pcm_lfloat_put_s32_index(dst_format);
		lfloat->func = snd_pcm_lfloat_convert_integer_float;
	} else {
		lfloat->int32_idx = snd_pcm_linear_put_index(SND_PCM_FORMAT_S32, dst_format);
		lfloat->float32_idx = snd_pcm_lfloat_get_s32_index(src_format);
		lfloat->func = snd_pcm_lfloat_convert_float_integer;
	}
	return 0;
}

static snd_pcm_uframes_t
snd_pcm_lfloat_write_areas(snd_pcm_t *pcm,
			   const snd_pcm_channel_area_t *areas,
			   snd_pcm_uframes_t offset,
			   snd_pcm_uframes_t size,
			   const snd_pcm_channel_area_t *slave_areas,
			   snd_pcm_uframes_t slave_offset,
			   snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	if (size > *slave_sizep)
		size = *slave_sizep;
	lfloat->func(slave_areas, slave_offset,
		     areas, offset, 
		     pcm->channels, size,
		     lfloat->int32_idx, lfloat->float32_idx);
	*slave_sizep = size;
	return size;
}

static snd_pcm_uframes_t
snd_pcm_lfloat_read_areas(snd_pcm_t *pcm,
			  const snd_pcm_channel_area_t *areas,
			  snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t size,
			  const snd_pcm_channel_area_t *slave_areas,
			  snd_pcm_uframes_t slave_offset,
			  snd_pcm_uframes_t *slave_sizep)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	if (size > *slave_sizep)
		size = *slave_sizep;
	lfloat->func(areas, offset, 
		     slave_areas, slave_offset,
		     pcm->channels, size,
		     lfloat->int32_idx, lfloat->float32_idx);
	*slave_sizep = size;
	return size;
}

static void snd_pcm_lfloat_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_lfloat_t *lfloat = pcm->private_data;
	snd_output_printf(out, "Linear Integer <-> Linear Float conversion PCM (%s)\n", 
		snd_pcm_format_name(lfloat->sformat));
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(lfloat->plug.slave, out);
}

snd_pcm_ops_t snd_pcm_lfloat_ops = {
	close: snd_pcm_plugin_close,
	info: snd_pcm_plugin_info,
	hw_refine: snd_pcm_lfloat_hw_refine,
	hw_params: snd_pcm_lfloat_hw_params,
	hw_free: snd_pcm_plugin_hw_free,
	sw_params: snd_pcm_plugin_sw_params,
	channel_info: snd_pcm_plugin_channel_info,
	dump: snd_pcm_lfloat_dump,
	nonblock: snd_pcm_plugin_nonblock,
	async: snd_pcm_plugin_async,
	mmap: snd_pcm_plugin_mmap,
	munmap: snd_pcm_plugin_munmap,
};

int snd_pcm_lfloat_open(snd_pcm_t **pcmp, const char *name, snd_pcm_format_t sformat, snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_lfloat_t *lfloat;
	int err;
	assert(pcmp && slave);
	if (snd_pcm_format_linear(sformat) != 1 &&
	    snd_pcm_format_float(sformat) != 1)
		return -EINVAL;
	lfloat = calloc(1, sizeof(snd_pcm_lfloat_t));
	if (!lfloat) {
		return -ENOMEM;
	}
	lfloat->sformat = sformat;
	lfloat->plug.read = snd_pcm_lfloat_read_areas;
	lfloat->plug.write = snd_pcm_lfloat_write_areas;
	lfloat->plug.slave = slave;
	lfloat->plug.close_slave = close_slave;

	err = snd_pcm_new(&pcm, SND_PCM_TYPE_LINEAR_FLOAT, name, slave->stream, slave->mode);
	if (err < 0) {
		free(lfloat);
		return err;
	}
	pcm->ops = &snd_pcm_lfloat_ops;
	pcm->fast_ops = &snd_pcm_plugin_fast_ops;
	pcm->private_data = lfloat;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = &lfloat->plug.hw_ptr;
	pcm->appl_ptr = &lfloat->plug.appl_ptr;
	*pcmp = pcm;
	
	return 0;
}

int _snd_pcm_lfloat_open(snd_pcm_t **pcmp, const char *name,
			 snd_config_t *root, snd_config_t *conf, 
			 snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	int err;
	snd_pcm_t *spcm;
	snd_config_t *slave = NULL, *sconf;
	snd_pcm_format_t sformat;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (snd_pcm_conf_generic_id(id))
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("slave is not defined");
		return -EINVAL;
	}
	err = snd_pcm_slave_conf(root, slave, &sconf, 1,
				 SND_PCM_HW_PARAM_FORMAT, SCONF_MANDATORY, &sformat);
	if (err < 0)
		return err;
	if (snd_pcm_format_linear(sformat) != 1 &&
	    snd_pcm_format_float(sformat) != 1) {
		snd_config_delete(sconf);
		SNDERR("slave format is not linear integer or linear float");
		return -EINVAL;
	}
	err = snd_pcm_open_slave(&spcm, root, sconf, stream, mode);
	snd_config_delete(sconf);
	if (err < 0)
		return err;
	err = snd_pcm_lfloat_open(pcmp, name, sformat, spcm, 1);
	if (err < 0)
		snd_pcm_close(spcm);
	return err;
}
SND_DLSYM_BUILD_VERSION(_snd_pcm_lfloat_open, SND_PCM_DLSYM_VERSION);