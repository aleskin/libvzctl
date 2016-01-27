/*
 * Copyright (c) 2015 Parallels IP Holdings GmbH
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
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <string.h>

#include <libvcmmd/vcmmd.h>

#include "env.h"
#include "res.h"
#include "logger.h"
#include "vzerror.h"
#include "exec.h"
#include "util.h"

#define VCMMCTL_BIN     "/usr/sbin/vcmmdctl"

static int vcmm_error(int rc, const char *msg)
{
	char buf[STR_SIZE];

	return vzctl_err(VZCTL_E_VCMM, 0, "vcmmd: %s: %s",
			msg, vcmmd_strerror(rc, buf, sizeof(buf)));
}

#define DEFAULT_MEM_GUARANTEE_PCT	20
static struct vcmmd_ve_config *get_config(struct vcmmd_ve_config *c,
		struct vzctl_ub_param *ub, struct vzctl_mem_guarantee * guar,
		int init)
{
	vcmmd_ve_config_init(c);
	int update_guar = init;
	unsigned long memguar = DEFAULT_MEM_GUARANTEE_PCT;
	unsigned long memlimit = 0;

	if (ub != NULL) {
		if (ub->physpages != NULL) {
			memlimit = ub->physpages->l * get_pagesize();
			vcmmd_ve_config_append(c, VCMMD_VE_CONFIG_LIMIT,
					memlimit);
			update_guar = 1;
		}

		if (ub->swappages != NULL)
			vcmmd_ve_config_append(c, VCMMD_VE_CONFIG_SWAP,
					ub->swappages->l * get_pagesize());
	}

	if (guar != NULL) {
		memguar = guar->type == VZCTL_MEM_GUARANTEE_PCT ?
				guar->value : DEFAULT_MEM_GUARANTEE_PCT;
		update_guar = 1;
	}

	if (update_guar) {
		unsigned long memguarlimit = memlimit *  memguar / 100;
		logger(1, 0, "memory guaranty %lu%% %lubytes",
				memguar, memguarlimit);
		vcmmd_ve_config_append(c, VCMMD_VE_CONFIG_GUARANTEE,
				memguarlimit);
	}

	return c;
}

int is_managed_by_vcmmd(void)
{
	return access(VCMMCTL_BIN, F_OK) == 0;
}

int vcmm_unregister(struct vzctl_env_handle *h)
{
	int rc;

	if (!is_managed_by_vcmmd())
		return 0;

	logger(1, 0, "vcmmd: unregister");
	rc = vcmmd_unregister_ve(EID(h));
	if (rc && rc != VCMMD_ERROR_VE_NOT_REGISTERED)
		return vcmm_error(rc, "failed to unregister Container");

	return 0;
}

int vcmm_register(struct vzctl_env_handle *h, struct vzctl_ub_param *ub,
		struct vzctl_mem_guarantee *guar)
{
	int rc;
	struct vcmmd_ve_config c;

	if (!is_managed_by_vcmmd())
		return 0;

	get_config(&c, ub, guar, 1);

	logger(1, 0, "vcmmd: register");
	rc = vcmmd_register_ve(EID(h), VCMMD_VE_CT, &c);
	if (rc == VCMMD_ERROR_VE_NAME_ALREADY_IN_USE) {
		vcmm_unregister(h);
		rc = vcmmd_register_ve(EID(h), VCMMD_VE_CT, &c);
	}
	if (rc)
		return vcmm_error(rc, "failed to register Container");

	logger(1, 0, "vcmmd: activate");
	rc = vcmmd_activate_ve(EID(h));
	if (rc)
		return vcmm_error(rc, "failed to activate Container");

	return 0;
}

int vcmm_update(struct vzctl_env_handle *h, struct vzctl_ub_param *ub,
		struct vzctl_mem_guarantee *guar)
{
	int rc;
	struct vcmmd_ve_config c = {};

	if (ub->physpages == NULL && ub->swappages == NULL && guar == NULL)
		return 0;

	logger(1, 0, "vcmmd: update");
	rc = vcmmd_update_ve(EID(h), get_config(&c, ub, guar, 0));
	if (rc)
		return vcmm_error(rc, "failed to update Container configuration");

	return 0;
}