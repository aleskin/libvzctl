/*
 *  Copyright (c) 1999-2015 Parallels IP Holdings GmbH
 *
 * This file is part of OpenVZ libraries. OpenVZ is free software; you can
 * redistribute it and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/> or write to Free Software Foundation,
 * 51 Franklin Street, Fifth Floor Boston, MA 02110, USA.
 *
 * Our contact details: Parallels IP Holdings GmbH, Vordergasse 59, 8200
 * Schaffhausen, Switzerland.
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include "libvzctl.h"
#include "list.h"
#include "cpt.h"
#include "cgroup.h"
#include "env.h"
#include "util.h"
#include "exec.h"
#include "vzerror.h"
#include "logger.h"

static int do_dump(struct vzctl_env_handle *h, int cmd,
		struct vzctl_cpt_param *param)
{
	char path[PATH_MAX];
	char buf[PATH_MAX];
	char script[PATH_MAX];
	char *arg[2];
	char *env[6];
	int ret, i = 0;
	pid_t pid;

	ret = cg_env_get_init_pid(EID(h), &pid);
	if (ret)
		return ret;

	get_dumpfile(h, param, path, sizeof(path));
	logger(2, 0, "Store dump at %s", path);

	snprintf(buf, sizeof(buf), "VE_DUMP_DIR=%s", path);
	env[i++] = strdup(buf);
	snprintf(buf, sizeof(buf), "VE_ROOT=%s", h->env_param->fs->ve_root);
	env[i++] = strdup(buf);
	snprintf(buf, sizeof(buf), "VE_PID=%d", pid);
	env[i++] = strdup(buf);

	cg_get_path(EID(h), CG_FREEZER, "", path, sizeof(path));
	snprintf(buf, sizeof(buf), "VE_FREEZE_CG=%s", path);
	env[i++] = strdup(buf);

	if (cmd == VZCTL_CMD_DUMP) {
		snprintf(buf, sizeof(buf), "CRIU_EXTRA_ARGS=--leave-running");
		env[i++] = strdup(buf);
	}
	env[i] = NULL;

	arg[0] = get_script_path("vz-cpt", script, sizeof(script));
	arg[1] = NULL;

	ret = vzctl2_wrap_exec_script(arg, env, 0);
	free_ar_str(env);
	
	return ret ? VZCTL_E_CHKPNT : 0;
}

static int dump(struct vzctl_env_handle *h, int cmd,
		struct vzctl_cpt_param *param)
{
	return do_dump(h, cmd, param);
}

static int chkpnt(struct vzctl_env_handle *h, int cmd,
		struct vzctl_cpt_param *param)
{
	int ret;
	char buf[PATH_MAX];

	ret = do_dump(h, cmd, param);
	if (ret)
		return ret;

	get_init_pid_path(EID(h), buf);
	unlink(buf);

	return 0;
}

static int restore(struct vzctl_env_handle *h, struct vzctl_cpt_param *param,
	struct start_param *data)
{
	char path[STR_SIZE];
	char script[PATH_MAX];
	char buf[PATH_MAX];
	char *arg[2];
	char *env[10];
	struct vzctl_veth_dev *veth;
	int ret, i = 0;
	char *pbuf, *ep;

	get_dumpfile(h, param, path, sizeof(path));
	logger(3, 0, "Open the dump file %s", path);
	snprintf(buf, sizeof(buf), "VE_DUMP_DIR=%s", path);
	env[i++] = strdup(buf);

	get_init_pid_path(h->ctid, path);
	snprintf(buf, sizeof(buf), "VE_PIDFILE=%s", path);
	env[i++] = strdup(buf);

	snprintf(buf, sizeof(buf), "VE_ROOT=%s", h->env_param->fs->ve_root);
	env[i++] = strdup(buf);
	snprintf(buf, sizeof(buf), "VZCTL_PID=%d", getpid());
	env[i++] = strdup(buf);
	if (data != NULL) {
		snprintf(buf, sizeof(buf), "STATUSFD=%d", data->status_p[1]);
		env[i++] = strdup(buf);
		snprintf(buf, sizeof(buf), "WAITFD=%d", data->wait_p[0]);
		env[i++] = strdup(buf);
	}
	get_netns_path(h, path, sizeof(path));
	snprintf(buf, sizeof(buf), "VE_NETNS_FILE=%s", path);
	env[i++] = strdup(buf);

	if (is_vz_kernel()) {
		snprintf(buf, sizeof(buf), "VEID=%s", h->ctid);
		env[i++] = strdup(buf);
	}

	pbuf = buf;
	ep = buf + sizeof(buf);
	pbuf += snprintf(buf, sizeof(buf), "VE_VETH_DEVS=");
	list_for_each(veth, &h->env_param->veth->dev_list, list) {
		pbuf += snprintf(pbuf, ep - pbuf,
				"%s=%s\n", veth->dev_name_ve, veth->dev_name);
		if (pbuf > ep) {
			env[i] = NULL;
			free_ar_str(env);
			return vzctl_err(VZCTL_E_INVAL, 0, "restore_FN: buffer overflow");
		}
	}
	env[i++] = strdup(buf);
	env[i] = NULL;

	arg[0] = get_script_path("vz-rst", script, sizeof(script));
	arg[1] = NULL;

	ret = vzctl2_wrap_exec_script(arg, env, 0);
	if (ret)
		ret = VZCTL_E_RESTORE;
	free_ar_str(env);

	return ret;
}

int criu_cmd(struct vzctl_env_handle *h, int cmd,
		struct vzctl_cpt_param *param, struct start_param *data)
{
	switch (cmd) {
	/* cpt */
	case VZCTL_CMD_CHKPNT:
		return chkpnt(h, cmd, param);
	case VZCTL_CMD_DUMP:
		logger(0, 0, "\tdump");
		return dump(h, cmd, param);
	/* rst */
	case VZCTL_CMD_RESTORE:
		return restore(h, param, data);
	default:
		return vzctl_err(VZCTL_E_INVAL, 0,
			"Unsupported criu command %d", cmd);
	}
}