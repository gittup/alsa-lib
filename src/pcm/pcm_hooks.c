/*
 *  PCM - Hook functions
 *  Copyright (c) 2001 by Abramo Bagnara <abramo@alsa-project.org>
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
  
#include <dlfcn.h>
#include "pcm_local.h"

#ifndef DOC_HIDDEN
struct _snd_pcm_hook {
	snd_pcm_t *pcm;
	snd_pcm_hook_func_t func;
	void *private_data;
	struct list_head list;
};

typedef struct {
	snd_pcm_t *slave;
	int close_slave;
	struct list_head hooks[SND_PCM_HOOK_LAST + 1];
} snd_pcm_hooks_t;

static int snd_pcm_hooks_close(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	struct list_head *pos, *next;
	unsigned int k;
	int err;
	if (h->close_slave) {
		err = snd_pcm_close(h->slave);
		if (err < 0)
			return err;
	}
	list_for_each_safe(pos, next, &h->hooks[SND_PCM_HOOK_CLOSE]) {
		snd_pcm_hook_t *hook = list_entry(pos, snd_pcm_hook_t, list);
		err = hook->func(hook);
		if (err < 0)
			return err;
	}
	for (k = 0; k <= SND_PCM_HOOK_LAST; ++k) {
		struct list_head *hooks = &h->hooks[k];
		while (!list_empty(hooks)) {
			snd_pcm_hook_t *hook;
			pos = hooks->next;
			hook = list_entry(pos, snd_pcm_hook_t, list);
			snd_pcm_hook_remove(hook);
		}
	}
	free(h);
	return 0;
}

static int snd_pcm_hooks_nonblock(snd_pcm_t *pcm, int nonblock)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_nonblock(h->slave, nonblock);
}

static int snd_pcm_hooks_async(snd_pcm_t *pcm, int sig, pid_t pid)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_async(h->slave, sig, pid);
}

static int snd_pcm_hooks_info(snd_pcm_t *pcm, snd_pcm_info_t *info)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_info(h->slave, info);
}

static int snd_pcm_hooks_channel_info(snd_pcm_t *pcm, snd_pcm_channel_info_t * info)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_channel_info(h->slave, info);
}

static int snd_pcm_hooks_status(snd_pcm_t *pcm, snd_pcm_status_t * status)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_status(h->slave, status);
}

static snd_pcm_state_t snd_pcm_hooks_state(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_state(h->slave);
}

static int snd_pcm_hooks_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_delay(h->slave, delayp);
}

static int snd_pcm_hooks_prepare(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_prepare(h->slave);
}

static int snd_pcm_hooks_reset(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_reset(h->slave);
}

static int snd_pcm_hooks_start(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_start(h->slave);
}

static int snd_pcm_hooks_drop(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_drop(h->slave);
}

static int snd_pcm_hooks_drain(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_drain(h->slave);
}

static int snd_pcm_hooks_pause(snd_pcm_t *pcm, int enable)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_pause(h->slave, enable);
}

static snd_pcm_sframes_t snd_pcm_hooks_rewind(snd_pcm_t *pcm, snd_pcm_uframes_t frames)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_rewind(h->slave, frames);
}

static snd_pcm_sframes_t snd_pcm_hooks_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_writei(h->slave, buffer, size);
}

static snd_pcm_sframes_t snd_pcm_hooks_writen(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_writen(h->slave, bufs, size);
}

static snd_pcm_sframes_t snd_pcm_hooks_readi(snd_pcm_t *pcm, void *buffer, snd_pcm_uframes_t size)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_readi(h->slave, buffer, size);
}

static snd_pcm_sframes_t snd_pcm_hooks_readn(snd_pcm_t *pcm, void **bufs, snd_pcm_uframes_t size)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_readn(h->slave, bufs, size);
}

static snd_pcm_sframes_t snd_pcm_hooks_mmap_commit(snd_pcm_t *pcm,
						   snd_pcm_uframes_t offset,
						   snd_pcm_uframes_t size)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_mmap_commit(h->slave, offset, size);
}

static snd_pcm_sframes_t snd_pcm_hooks_avail_update(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_avail_update(h->slave);
}

static int snd_pcm_hooks_hw_refine(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_hw_refine(h->slave, params);
}

static int snd_pcm_hooks_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	struct list_head *pos, *next;
	int err = snd_pcm_hw_params(h->slave, params);
	if (err < 0)
		return err;
	list_for_each_safe(pos, next, &h->hooks[SND_PCM_HOOK_HW_PARAMS]) {
		snd_pcm_hook_t *hook = list_entry(pos, snd_pcm_hook_t, list);
		err = hook->func(hook);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_pcm_hooks_hw_free(snd_pcm_t *pcm)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	struct list_head *pos, *next;
	int err = snd_pcm_hw_free(h->slave);
	if (err < 0)
		return err;
	list_for_each_safe(pos, next, &h->hooks[SND_PCM_HOOK_HW_FREE]) {
		snd_pcm_hook_t *hook = list_entry(pos, snd_pcm_hook_t, list);
		err = hook->func(hook);
		if (err < 0)
			return err;
	}
	return 0;
}

static int snd_pcm_hooks_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t * params)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	return snd_pcm_sw_params(h->slave, params);
}

static int snd_pcm_hooks_mmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	return 0;
}

static int snd_pcm_hooks_munmap(snd_pcm_t *pcm ATTRIBUTE_UNUSED)
{
	return 0;
}

static void snd_pcm_hooks_dump(snd_pcm_t *pcm, snd_output_t *out)
{
	snd_pcm_hooks_t *h = pcm->private_data;
	snd_output_printf(out, "Hooks PCM\n");
	if (pcm->setup) {
		snd_output_printf(out, "Its setup is:\n");
		snd_pcm_dump_setup(pcm, out);
	}
	snd_output_printf(out, "Slave: ");
	snd_pcm_dump(h->slave, out);
}

snd_pcm_ops_t snd_pcm_hooks_ops = {
	close: snd_pcm_hooks_close,
	info: snd_pcm_hooks_info,
	hw_refine: snd_pcm_hooks_hw_refine,
	hw_params: snd_pcm_hooks_hw_params,
	hw_free: snd_pcm_hooks_hw_free,
	sw_params: snd_pcm_hooks_sw_params,
	channel_info: snd_pcm_hooks_channel_info,
	dump: snd_pcm_hooks_dump,
	nonblock: snd_pcm_hooks_nonblock,
	async: snd_pcm_hooks_async,
	mmap: snd_pcm_hooks_mmap,
	munmap: snd_pcm_hooks_munmap,
};

snd_pcm_fast_ops_t snd_pcm_hooks_fast_ops = {
	status: snd_pcm_hooks_status,
	state: snd_pcm_hooks_state,
	delay: snd_pcm_hooks_delay,
	prepare: snd_pcm_hooks_prepare,
	reset: snd_pcm_hooks_reset,
	start: snd_pcm_hooks_start,
	drop: snd_pcm_hooks_drop,
	drain: snd_pcm_hooks_drain,
	pause: snd_pcm_hooks_pause,
	rewind: snd_pcm_hooks_rewind,
	writei: snd_pcm_hooks_writei,
	writen: snd_pcm_hooks_writen,
	readi: snd_pcm_hooks_readi,
	readn: snd_pcm_hooks_readn,
	avail_update: snd_pcm_hooks_avail_update,
	mmap_commit: snd_pcm_hooks_mmap_commit,
};

int snd_pcm_hooks_open(snd_pcm_t **pcmp, const char *name, snd_pcm_t *slave, int close_slave)
{
	snd_pcm_t *pcm;
	snd_pcm_hooks_t *h;
	unsigned int k;
	assert(pcmp && slave);
	h = calloc(1, sizeof(snd_pcm_hooks_t));
	if (!h)
		return -ENOMEM;
	h->slave = slave;
	h->close_slave = close_slave;
	for (k = 0; k <= SND_PCM_HOOK_LAST; ++k) {
		INIT_LIST_HEAD(&h->hooks[k]);
	}
	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		free(h);
		return -ENOMEM;
	}
	if (name)
		pcm->name = strdup(name);
	pcm->type = SND_PCM_TYPE_HOOKS;
	pcm->stream = slave->stream;
	pcm->mode = slave->mode;
	pcm->ops = &snd_pcm_hooks_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_hooks_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private_data = h;
	pcm->poll_fd = slave->poll_fd;
	pcm->hw_ptr = slave->hw_ptr;
	pcm->appl_ptr = slave->appl_ptr;
	*pcmp = pcm;

	return 0;
}

int snd_pcm_hook_add_conf(snd_pcm_t *pcm, snd_config_t *conf)
{
	int err;
	char buf[256];
	const char *str;
	const char *lib = NULL, *install = NULL;
	snd_config_t *args = NULL;
	snd_config_iterator_t i, next;
	int (*install_func)(snd_pcm_t *pcm, snd_config_t *args);
	void *h;
	if (snd_config_get_string(conf, &str) >= 0) {
		err = snd_config_search_alias(snd_config, "pcm_hook", str, &conf);
		if (err < 0) {
			SNDERR("unknown pcm_hook %s", str);
			return err;
		}
	}
	if (snd_config_get_type(conf) != SND_CONFIG_TYPE_COMPOUND) {
		SNDERR("Invalid hook definition");
		return -EINVAL;
	}
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (strcmp(id, "comment") == 0)
			continue;
		if (strcmp(id, "lib") == 0) {
			err = snd_config_get_string(n, &lib);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "install") == 0) {
			err = snd_config_get_string(n, &install);
			if (err < 0) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			continue;
		}
		if (strcmp(id, "args") == 0) {
			args = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (args && snd_config_get_string(args, &str) >= 0) {
		err = snd_config_search_alias(snd_config, "hook_args", str, &args);
		if (err < 0) {
			SNDERR("unknown hook_args %s", str);
			return err;
		}
	}
	if (!install) {
		install = buf;
		snprintf(buf, sizeof(buf), "_snd_pcm_hook_%s_install",
			 snd_config_get_id(conf));
	}
	if (!lib)
		lib = ALSA_LIB;
	h = dlopen(lib, RTLD_NOW);
	if (!h) {
		SNDERR("Cannot open shared library %s", lib);
		return -ENOENT;
	}
	install_func = dlsym(h, install);
	if (!install_func) {
		SNDERR("symbol %s is not defined inside %s", install, lib);
		dlclose(h);
		return -ENXIO;
	}
	err = install_func(pcm, args);
	if (err < 0)
		return err;
	return 0;
}

int _snd_pcm_hooks_open(snd_pcm_t **pcmp, const char *name,
			snd_config_t *conf, 
			snd_pcm_stream_t stream, int mode)
{
	snd_config_iterator_t i, next;
	int err;
	snd_pcm_t *spcm;
	snd_config_t *slave = NULL, *sconf;
	snd_config_t *hooks = NULL;
	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id = snd_config_get_id(n);
		if (snd_pcm_conf_generic_id(id))
			continue;
		if (strcmp(id, "slave") == 0) {
			slave = n;
			continue;
		}
		if (strcmp(id, "hooks") == 0) {
			if (snd_config_get_type(n) != SND_CONFIG_TYPE_COMPOUND) {
				SNDERR("Invalid type for %s", id);
				return -EINVAL;
			}
			hooks = n;
			continue;
		}
		SNDERR("Unknown field %s", id);
		return -EINVAL;
	}
	if (!slave) {
		SNDERR("slave is not defined");
		return -EINVAL;
	}
	err = snd_pcm_slave_conf(slave, &sconf, 0);
	if (err < 0)
		return err;
	err = snd_pcm_open_slave(&spcm, sconf, stream, mode);
	if (err < 0)
		return err;
	err = snd_pcm_hooks_open(pcmp, name, spcm, 1);
	if (err < 0) {
		snd_pcm_close(spcm);
		return err;
	}
	if (!hooks)
		return 0;
	snd_config_for_each(i, next, hooks) {
		snd_config_t *n = snd_config_iterator_entry(i);
		err = snd_pcm_hook_add_conf(*pcmp, n);
		if (err < 0) {
			snd_pcm_close(*pcmp);
			return err;
		}
	}
	return 0;
}

#endif

/**
 * \brief Get PCM handle for a PCM hook
 * \param hook PCM hook handle
 * \return PCM handle
 */
snd_pcm_t *snd_pcm_hook_get_pcm(snd_pcm_hook_t *hook)
{
	assert(hook);
	return hook->pcm;
}

/**
 * \brief Get callback function private data for a PCM hook
 * \param hook PCM hook handle
 * \return callback function private data
 */
void *snd_pcm_hook_get_private(snd_pcm_hook_t *hook)
{
	assert(hook);
	return hook->private_data;
}

/**
 * \brief Add a PCM hook at end of hooks chain
 * \param hookp Returned PCM hook handle
 * \param pcm PCM handle
 * \param type PCM hook type
 * \param func PCM hook callback function
 * \param private_data PCM hook private data
 * \return 0 on success otherwise a negative error code
 *
 * Warning: an hook callback function cannot remove an hook of the same type
 * different from itself
 */
int snd_pcm_hook_add(snd_pcm_hook_t **hookp, snd_pcm_t *pcm,
		     snd_pcm_hook_type_t type,
		     snd_pcm_hook_func_t func, void *private_data)
{
	snd_pcm_hook_t *h;
	snd_pcm_hooks_t *hooks;
	assert(hookp && func);
	assert(snd_pcm_type(pcm) == SND_PCM_TYPE_HOOKS);
	h = calloc(1, sizeof(*h));
	if (!h)
		return -ENOMEM;
	h->pcm = pcm;
	h->func = func;
	h->private_data = private_data;
	hooks = pcm->private_data;
	list_add_tail(&h->list, &hooks->hooks[type]);
	*hookp = h;
	return 0;
}

/**
 * \brief Remove a PCM hook
 * \param hook PCM hook handle
 * \return 0 on success otherwise a negative error code
 *
 * Warning: an hook callback cannot remove an hook of the same type
 * different from itself
 */
int snd_pcm_hook_remove(snd_pcm_hook_t *hook)
{
	assert(hook);
	list_del(&hook->list);
	free(hook);
	return 0;
}
