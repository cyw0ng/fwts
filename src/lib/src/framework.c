/*
 * Copyright (C) 2010 Canonical
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */


#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <getopt.h>

#include "framework.h"

#define RESULTS_LOG	"results.log"

#define LOGFILE(name1, name2)	\
	(name1 != NULL) ? name1 : name2

#define FRAMEWORK_FLAGS_STDOUT_SUMMARY		0x00000001
#define FRAMEWORK_FLAGS_FRAMEWORK_DEBUG		0x00000002
#define FRAMEWORK_FLAGS_SHOW_PROGRESS		0x00000004

#define FRAMEWORK_DEFAULT_FLAGS			0

enum {
	BIOS_TEST_TOOLKIT_PASSED_TEXT,
	BIOS_TEST_TOOLKIT_FAILED_TEXT,
	BIOS_TEST_TOOLKIT_ERROR_TEXT,
	BIOS_TEST_TOOLKIT_FRAMEWORK_DEBUG
};

typedef struct fwts_framework_list {
	const char *name;
	const fwts_framework_ops *ops;
	struct fwts_framework_list *next;
	int   priority;
} fwts_framework_list;

static fwts_framework_list *fwts_framework_list_head = NULL;

typedef struct {
	int env_id;
	char *env_name;
	char *env_default;
	char *env_value;
} fwts_framework_setting;

#define ID_NAME(id)	id, # id

static fwts_framework_setting fwts_framework_settings[] = {
	{ ID_NAME(BIOS_TEST_TOOLKIT_PASSED_TEXT),      "PASSED", NULL },
	{ ID_NAME(BIOS_TEST_TOOLKIT_FAILED_TEXT),      "FAILED", NULL },
	{ ID_NAME(BIOS_TEST_TOOLKIT_ERROR_TEXT),       "ERROR",  NULL },
	{ ID_NAME(BIOS_TEST_TOOLKIT_FRAMEWORK_DEBUG),  "off",    NULL },
};

static void fwts_framework_debug(fwts_framework* framework, char *fmt, ...);


#ifdef FRAMEWORK_DEBUG
static void fwts_framework_dump_items(fwts_framework_list *head)
{
	printf("DUMP:\n");
	while (head) {
		printf("%p %s %d\n",head, head->name, head->priority);
		head = head->next;
	}
}
#endif

void fwts_framework_add(char *name, const fwts_framework_ops *ops, const int priority)
{
	fwts_framework_list *newitem;
	fwts_framework_list **list;

	if ((newitem = malloc(sizeof(fwts_framework_list))) == NULL) {
		fprintf(stderr, "FATAL: Could not allocate memory initialising fwts_framework_\n");		
		exit(EXIT_FAILURE);
	}
	newitem->name = name;
	newitem->ops  = ops;
	newitem->next = NULL;
	newitem->priority = priority;

	for (list = &fwts_framework_list_head; *list != NULL; list = &(*list)->next) {
		if ((*list)->priority >= newitem->priority) {
			newitem->next = (*list);
			break;
		}
	}
	*list = newitem;

	/*fwts_framework_dump_items(fwts_framework_list_head); */
}

static void fwts_framework_show_tests(void)
{
	fwts_framework_list *item;

	printf("Available tests:\n");

	for (item = fwts_framework_list_head; item != NULL; item = item->next)
		printf(" %-13.13s %s\n", item->name, item->ops->headline());
}
	

static void fwts_framework_underline(fwts_framework *fw, const int ch)
{
	fwts_log_underline(fw->results, ch);
}

static char *fwts_framework_get_env(const int env_id)
{
	int i;

	for (i=0;i<sizeof(fwts_framework_settings)/sizeof(fwts_framework_setting);i++) {
		if (fwts_framework_settings[i].env_id == env_id) {
			if (fwts_framework_settings[i].env_value)
				return fwts_framework_settings[i].env_value;
			else {
				char *value = getenv(fwts_framework_settings[i].env_name);
				if (value == NULL) {
					value = fwts_framework_settings[i].env_default;
				}
				fwts_framework_settings[i].env_value = malloc(strlen(value)+1);
				if (fwts_framework_settings[i].env_value) {
					strcpy(fwts_framework_settings[i].env_value, value);
					return fwts_framework_settings[i].env_value;
				} else {
					return "";
				}
			}
		}
	}
	return "";
}

static void fwts_framework_debug(fwts_framework* fw, char *fmt, ...)
{
	va_list ap;
	char buffer[1024];	
	static int debug = -1;

	if (debug == -1)
		debug = (!strcmp(fwts_framework_get_env(BIOS_TEST_TOOLKIT_FRAMEWORK_DEBUG),"on")) |
			(fw->flags & FRAMEWORK_FLAGS_FRAMEWORK_DEBUG);
	if (debug == 0)
		return;

	va_start(ap, fmt);

        vsnprintf(buffer, sizeof(buffer), fmt, ap);
	
	fwts_log_printf(fw->debug, LOG_DEBUG, "%s", buffer);

        va_end(ap);
}

static int fwts_framework_test_summary(fwts_framework *fw)
{
	fwts_framework_underline(fw,'=');
	fwts_log_summary(fw->results, "%d passed, %d failed, %d aborted", fw->tests_passed, fw->tests_failed, fw->tests_aborted);
	fwts_framework_underline(fw,'=');

	if (fw->flags & FRAMEWORK_FLAGS_STDOUT_SUMMARY) {
		if ((fw->tests_aborted > 0) || (fw->tests_failed > 0))
			printf("FAILED\n");
		else 
			printf("PASSED\n");
	}

	fwts_log_newline(fw->results);

	fw->total_tests_aborted += fw->tests_aborted;
	fw->total_tests_failed  += fw->tests_failed;
	fw->total_tests_passed  += fw->tests_passed;

	return 0;
}

static int fwts_framework_total_summary(fwts_framework *fw)
{
	fwts_log_set_owner(fw->results, "fwts_framework_");
	fwts_log_summary(fw->results, "All tests: %d passed, %d failed, %d aborted", fw->total_tests_passed, fw->total_tests_failed, fw->total_tests_aborted);

	return 0;
}

static int fwts_framework_run_test(fwts_framework *fw, const char *name, const fwts_framework_ops *ops)
{		
	int num = 0;
	fwts_framework_tests *test;

	fwts_framework_debug(fw, "fwts_framework_run_test() entered\n");

	fwts_log_set_owner(fw->results, name);

	if (ops->headline) {
		fwts_log_info(fw->results, "%s", ops->headline());
		fwts_framework_underline(fw,'-');
	}

	fwts_framework_debug(fw, "fwts_framework_run_test() calling ops->init()\n");

	fw->tests_aborted = 0;
	fw->tests_failed = 0;
	fw->tests_passed = 0;

	if (ops->init) {
		if (ops->init(fw->results, fw)) {
			/* Init failed, so abort */
			fwts_log_error(fw->results, "Aborted test, initialisation failed");
			fwts_framework_debug(fw, "fwts_framework_run_test() init failed, aborting!");
			for (test = ops->tests; *test != NULL; test++) {
				fw->tests_aborted++;
			}
			fwts_framework_test_summary(fw);
			return 0;
		}
	}

	for (test = ops->tests; *test != NULL; test++)
		num++;

	for (test = ops->tests, fw->current_test = 1; *test != NULL; test++, fw->current_test++) {
		if (fw->flags & FRAMEWORK_FLAGS_SHOW_PROGRESS) {
			fprintf(stderr, "%-20.20s: Test %d of %d started\n", name, fw->current_test, num);		
			fflush(stderr);
		}

		fwts_framework_debug(fw, "exectuting test %d\n", fw->current_test);

		(*test)(fw->results, fw);

		if (fw->flags & FRAMEWORK_FLAGS_SHOW_PROGRESS) {
			fprintf(stderr, "%-20.20s: Test %d of %d completed (%d passed, %d failed, %d aborted)\n", 
				name, fw->current_test, num,
				fw->tests_passed, fw->tests_failed, fw->tests_aborted);
		}
	}

	fwts_framework_debug(fw, "fwts_framework_run_test() calling ops->deinit()\n");
	if (ops->deinit)
		ops->deinit(fw->results, fw);
	fwts_framework_debug(fw, "fwts_framework_run_test() complete\n");

	fwts_framework_test_summary(fw);

	return 0;
}

static void fwts_framework_run_registered_tests(fwts_framework *fw)
{
	fwts_framework_list *item;

	fwts_framework_debug(fw, "fwts_framework_run_registered_tests()\n");
	for (item = fwts_framework_list_head; item != NULL; item = item->next) {
		fwts_framework_debug(fw, "fwts_framework_run_registered_tests() - test %s\n",item->name);
		fwts_framework_run_test(fw, item->name, item->ops);
	}
	fwts_framework_debug(fw, "fwts_framework_run_registered_tests() done\n");
}

static int fwts_framework_run_registered_test(fwts_framework *fw, const char *name)
{
	fwts_framework_list *item;
	fwts_framework_debug(fw, "fwts_framework_run_registered_tests() - run test %s\n",name);
	for (item = fwts_framework_list_head; item != NULL; item = item->next) {
		if (strcmp(name, item->name) == 0) {
			fwts_framework_debug(fw, "fwts_framework_run_registered_tests() - test %s\n",item->name);
			fwts_framework_run_test(fw, item->name, item->ops);
			return 0;
		}
	}
	fw->results = fwts_log_open(name, LOGFILE(fw->results_logname, RESULTS_LOG), "a+");
	fwts_log_printf(fw->results, LOG_ERROR, "Test %s does not exist!", name);
	fwts_log_close(fw->results);

	return 1;
}

static void fwts_framework_close(fwts_framework *fw)
{
	int failed = (fw->total_tests_aborted > 0 || fw->total_tests_failed);

	if (fw && (fw->magic == FRAMEWORK_MAGIC)) {
		free(fw);
	}
	
	exit(failed ? EXIT_FAILURE : EXIT_SUCCESS);
}

void fwts_framework_passed(fwts_framework *fw, const char *fmt, ...)
{
	va_list ap;
	char buffer[1024];

	va_start(ap, fmt);

	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	fwts_framework_debug(fw, "test %d passed: %s\n", fw->current_test, buffer);
	fw->tests_passed++;
	fwts_log_printf(fw->results, LOG_RESULT, "%s: test %d, %s", 
		fwts_framework_get_env(BIOS_TEST_TOOLKIT_PASSED_TEXT), fw->current_test, buffer);

	va_end(ap);
}

void fwts_framework_failed(fwts_framework *fw, const char *fmt, ...)
{
	va_list ap;
	char buffer[1024];

	va_start(ap, fmt);

	vsnprintf(buffer, sizeof(buffer), fmt, ap);
	fwts_framework_debug(fw, "test %d failed: %s\n", fw->current_test, buffer);
	fw->tests_failed++;
	fwts_log_printf(fw->results, LOG_RESULT, "%s: test %d, %s", 
		fwts_framework_get_env(BIOS_TEST_TOOLKIT_FAILED_TEXT), fw->current_test, buffer);

	va_end(ap);
}

static void fwts_framework_syntax(char **argv)
{
	printf("Usage %s: [OPTION] [TEST]\n", argv[0]);
	printf("Arguments:\n");
	printf("--dmidecode\t\tSpecify path to dmidecode\n");
	printf("--iasl\t\t\tSpecify path to iasl\n");
	printf("--fwts_framework_-debug\tEnable run-time fwts_framework debug\n");
	printf("--help\t\t\tGet help\n");
	printf("--stdout-summary\tOutput SUCCESS or FAILED to stdout at end of tests\n");
	printf("--results-no-separators\tNo horizontal separators in results log\n");
	printf("--results-output=file\tOutput results to a named file. Filename can also be stdout or stderr\n");
	printf("--debug-output=file\tOutput debug to a named file. Filename can also be stdout or stderr\n");	
	printf("--dsdt=file\t\tSpecify DSDT file rather than reading it from the ACPI table\n");
	printf("--klog=file\t\tSpecify kernel log file rather than reading it from the kernel\n");
	printf("--log-fields\t\tShow available log filtering fields\n");
	printf("--log-filter=expr\tDefine filters to dump out specific log fields\n");
	printf("\t\te.g. --log-filter=RES,SUM  - dump out results and summary\n");
	printf("\t\t     --log-filter=ALL,~INF - dump out all fields except info fields\n");
	printf("--log-format=fields\tDefine output log format\n");
	printf("\t\te.g. --log-format=%%date %%time [%%field] (%%owner): %%text\n");
	printf("\t\t     fields are: %%date - date, %%time - time, %%field log filter field\n");
	printf("\t\t                 %%owner - name of test program, %%text - log text\n");
	printf("--show-progress\t\tOutput test progress report to stderr\n");
	printf("--show-tests\t\tShow available tests\n");
}


int fwts_framework_args(int argc, char **argv)
{
	struct option long_options[] = {
		{ "stdout-summary", 0, 0, 0 },		
		{ "fwts_framework_-debug", 0, 0, 0 },
		{ "help", 0, 0, 0 },
		{ "results-output", 1, 0, 0 },
		{ "results-no-separators", 0, 0, 0 },
		{ "debug-output", 1, 0, 0 },
		{ "log-filter", 1, 0, 0 },
		{ "log-fields", 0, 0, 0 },	
		{ "log-format", 1, 0, 0 },
		{ "iasl", 1, 0, 0 },
		{ "show-progress", 0, 0, 0 },
		{ "show-tests", 0, 0, 0 },
		{ "dsdt", 1, 0, 0, },
		{ "klog", 1, 0, 0, },
		{ "dmidecode", 1, 0, 0, },
		{ "s3-multiple", 1, 0, 0, },
		{ 0, 0, 0, 0 }
	};

	fwts_framework *fw;

	fw = (fwts_framework *)calloc(1, sizeof(fwts_framework));
	if (fw == NULL) {
		return 1;
	}

	fw->magic = FRAMEWORK_MAGIC;
	fw->flags = FRAMEWORK_DEFAULT_FLAGS;

	for (;;) {
		int c;
		int option_index;

		if ((c = getopt_long(argc, argv, "", long_options, &option_index)) == -1)
			break;
	
		switch (c) {
		case 0:
			switch (option_index) {
			case 0: /* --stdout-summary */
				fw->flags |= FRAMEWORK_FLAGS_STDOUT_SUMMARY;
				break;	
			case 1: /* --fwts_framework_-debug */
				fw->flags |= FRAMEWORK_FLAGS_FRAMEWORK_DEBUG;
				break;		
			case 2: /* --help */
				fwts_framework_syntax(argv);
				exit(EXIT_SUCCESS);
			case 3: /* --results-output */
				fw->results_logname = strdup(optarg);
				break;
			case 4: /* --results-no-separators */
				fwts_log_filter_unset_field(LOG_SEPARATOR);
				break;
			case 5: /* --debug-output */
				fw->debug_logname = strdup(optarg);
				fw->flags |= FRAMEWORK_FLAGS_FRAMEWORK_DEBUG;
				break;
			case 6: /* --log-filter */
				fwts_log_filter_unset_field(~0);
				fwts_log_set_field_filter(optarg);
				break;
			case 7: /* --log-fields */
				fwts_log_print_fields();
				exit(EXIT_SUCCESS);
				break;
			case 8: /* --log-format */
				fwts_log_set_format(optarg);
				break;	
			case 9: /* --iasl */
				fw->iasl = strdup(optarg);
				break;
			case 10: /* --show-progress */
				fw->flags |= FRAMEWORK_FLAGS_SHOW_PROGRESS;
				break;
			case 11: /* --show-tests */
				fwts_framework_show_tests();
				exit(EXIT_SUCCESS);
				break;
			case 12: /* --dsdt */
				fw->dsdt = strdup(optarg);
				break;
			case 13: /* --klog */
				fw->klog = strdup(optarg);
				break;
			case 14: /* --dmidecode */
				fw->dmidecode = strdup(optarg);
				break;
			case 15: /* --s3-multiple */
				fw->s3_multiple = atoi(optarg);
				break;
			}
		case '?':
			break;
		}
	}	

	fw->debug = fwts_log_open("fwts_framework_", LOGFILE(fw->debug_logname, "stderr"), "a+");
	fw->results = fwts_log_open("fwts_framework_", LOGFILE(fw->results_logname, RESULTS_LOG), "a+");

	if (optind < argc) 
		for (; optind < argc; optind++) {
			if (fwts_framework_run_registered_test(fw, argv[optind])) {
				fprintf(stderr, "No such test '%s'\n",argv[optind]);
				fwts_framework_show_tests();
				exit(EXIT_FAILURE);
			}
		}
	else 
		fwts_framework_run_registered_tests(fw);

	fwts_framework_total_summary(fw);

	fwts_log_close(fw->results);
	fwts_log_close(fw->debug);

	fwts_framework_close(fw);

	return 0;
}

