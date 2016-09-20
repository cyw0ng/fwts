/*
 * Copyright (C) 2014-2016 Jeremy Kerr <jk@ozlabs.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <ctype.h>

#include <libfdt.h>

#include "fwts.h"

static const char op_powernv[] = "ibm,powernv";

static const char *firestone_models[] = {
	"8335-GTA",
	"8335-GCA",
	"8335-GTX",
};

static const char *garrison_models[] = {
	"8335-GTB",
};

static struct reference_platform {
	const char	*compatible;
	const char	**models;
	int		n_models;
} openpower_reference_platforms[] = {
	{"ibm,firestone", firestone_models,
		FWTS_ARRAY_LEN(firestone_models)},
	{"ibm,garrison", garrison_models,
		FWTS_ARRAY_LEN(garrison_models)},
};

static int dt_sysinfo_check_root_property(
	fwts_framework *fw,
	const char *name,
	bool print_flag)
{
	int node, len;
	const char *buf;

	if (!fw->fdt) {
		fwts_failed(fw, LOG_LEVEL_LOW, "DTMissing",
			"Device Tree is missing, aborting");
		return FWTS_ABORTED;
	}

	node = fdt_path_offset(fw->fdt, "/");
	if (node < 0) {
		fwts_failed(fw, LOG_LEVEL_LOW, "DTRootNodeMissing",
				"root device tree node is missing");
		return FWTS_ERROR;
	}

	buf = fdt_getprop(fw->fdt, node, name, &len);
	if (buf == NULL) {
		fwts_failed(fw, LOG_LEVEL_LOW,
			"DTSysinfoPropertyMissing",
			"can't read property %s: %s",
			name, fdt_strerror(len));
		return FWTS_ERROR;
	}

	if (print_flag) {
		if (check_property_printable(fw, name, buf, len))
			return FWTS_ERROR; /* failures logged prior */
	}

	fwts_passed(fw, "sysinfo property %s is valid", name);

	return FWTS_OK;
}

static int dt_sysinfo_check_model(fwts_framework *fw)
{
	return dt_sysinfo_check_root_property(fw, "model", true);
}

static int dt_sysinfo_check_system_id(fwts_framework *fw)
{
	return dt_sysinfo_check_root_property(fw, "system-id", true);
}

static bool dt_fdt_stringlist_contains_last(const char *strlist,
	int listlen,
	const char *str)
{
	int len = strlen(str);
	const char *p;

	/* checking for either str only or last in string */
	if (listlen < 2) /* need at least one byte plus nul */
		return false;

	p = memrchr(strlist, '\0', listlen-1);
	if (p) {
		p++;
	} else {
		p = strlist;
	}

	return !memcmp(str, p, len+1);
}

static bool machine_matches_reference_model(fwts_framework *fw,
	const char *compatible,
	int compat_len,
	const char *model)
{
	bool compatible_is_reference = false, model_is_reference = false;
	struct reference_platform *plat;
	int i;

	for (i = 0; i < (int)FWTS_ARRAY_LEN(openpower_reference_platforms);
			i++) {
		plat = &openpower_reference_platforms[i];
		if (dt_fdt_stringlist_contains_last(compatible,
				compat_len, plat->compatible)) {
			compatible_is_reference = true;
			break;
		}
	}

	/* Not a reference platform, nothing to check */
	if (!compatible_is_reference) {
		fwts_log_info(fw, "Informational: no reference model found,"
			" device tree \"compatible\" is \"%s\" and"
			" \"model\" is \"%s\"",
			compatible, model);
		return true;
	}

	/* Since we're on a reference platform, ensure that the model is also
	 * one of the reference model numbers */
	for (i = 0; i < plat->n_models; i++) {
		if (!strcmp(model, plat->models[i])) {
			model_is_reference = true;
			break;
		}
	}

	if (model_is_reference) {
		fwts_log_info_verbatim(fw,
			"Matched reference model, device tree "
			"\"compatible\" is \"%s\" and \"model\" is "
			"\"%s\"",
			plat->compatible, model);
	}

	return model_is_reference;
}

static int dt_sysinfo_check_ref_plat_compatible(fwts_framework *fw)
{
	int node, compat_len, model_len;

	node = fdt_path_offset(fw->fdt, "/");
	if (node < 0) {
		fwts_failed(fw, LOG_LEVEL_HIGH, "DTRootNodeMissing",
			"root device tree node is missing");
		return FWTS_ERROR;
	}
	if (fdt_node_check_compatible(fw->fdt, node, op_powernv)) {
		fwts_failed(fw, LOG_LEVEL_HIGH, "DTCompatibleMissing",
			"DeviceTree failed validation, could not find"
			" the \"compatible\" property of \"%s\" in the "
			"root of the device tree", "ibm,powernv");
		return FWTS_ERROR;
	} else {
		const char *model_buf, *compat_buf;
		char *orig_model_buf, *tmp_model_buf;

		compat_buf = fdt_getprop(fw->fdt, node,
				"compatible", &compat_len);
		model_buf = fdt_getprop(fw->fdt, node,
				"model", &model_len);

		if (!model_buf || !compat_buf) {
			fwts_failed(fw,LOG_LEVEL_HIGH,
				"DTSysInfoCheck:",
				" Cannot read the properties for OpenPOWER"
				" Reference Compatible check");
			return FWTS_ERROR;
		}

		/* need modifiable memory    */
		/* save original ptr to free */
		tmp_model_buf = orig_model_buf = strdup(model_buf);
		if (!tmp_model_buf) {
			fwts_failed(fw, LOG_LEVEL_HIGH,
				"DTSysInfoCheck:",
				" Unable to get memory for model"
				" compare for OpenPOWER"
				" Reference Compatible check");
			return FWTS_ERROR;
		}

		tmp_model_buf = hidewhitespace(tmp_model_buf);
		if (!(strcmp(model_buf, tmp_model_buf) == 0)) {
			fwts_warning(fw,
				"DTSysInfoCheck:"
				" See further advice in the log.\n");
			fwts_log_nl(fw);
			fwts_log_info_verbatim(fw,
				"DTSysInfoCheck:"
				" Check the root \"model\" property"
				" from the device tree %s \"%s\".\n",
				DT_FS_PATH,
				model_buf);
			fwts_advice(fw,
				"Check the root \"model\" property"
				" from the device tree %s, "
				"there are whitespace inconsistentencies"
				" between the \"model\" property and "
				" the trimmed value of \"%s\", report"
				" this as a possible bug."
				"  Run \"hexdump -C model\""
				" from the \"%s\" directory to view"
				" the raw contents of the property.",
				DT_FS_PATH,
				tmp_model_buf,
				DT_FS_PATH);
		}
		if (machine_matches_reference_model(fw,
				compat_buf,
				compat_len,
				tmp_model_buf)) {
			fwts_passed(fw, "OpenPOWER Reference "
				"Compatible passed");
		} else {
			fwts_failed(fw, LOG_LEVEL_HIGH,
				"DTOpenPOWERReferenceFailed",
			"Unable to find an OpenPOWER supported"
			" match");
			/* adding verbatim to show proper string */
			fwts_log_info_verbatim(fw,
			"Unable to find an OpenPOWER reference"
			" match for \"%s\"", tmp_model_buf);
			free(orig_model_buf);
			return FWTS_ERROR;
		}
		free(orig_model_buf);
	}

	return FWTS_OK;
}

static fwts_framework_minor_test dt_sysinfo_tests[] = {
	{ dt_sysinfo_check_model,
		"Check model property" },
	{ dt_sysinfo_check_system_id,
		"Check system-id property" },
	{ dt_sysinfo_check_ref_plat_compatible,
		"Check OpenPOWER Reference compatible" },
	{ NULL, NULL },
};

static fwts_framework_ops dt_sysinfo_ops = {
	.description	= "Device tree system information test",
	.minor_tests	= dt_sysinfo_tests,
};

FWTS_REGISTER_FEATURES("dt_sysinfo", &dt_sysinfo_ops, FWTS_TEST_ANYTIME,
		FWTS_FLAG_BATCH, FWTS_FW_FEATURE_DEVICETREE);
