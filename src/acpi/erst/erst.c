/*
 * Copyright (C) 2015-2017 Canonical
 *
 * Portions of this code original from the Linux-ready Firmware Developer Kit
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
#include "fwts.h"

#if defined(FWTS_HAS_ACPI)

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <string.h>

static fwts_acpi_table_info *table;

static int erst_init(fwts_framework *fw)
{
	if (fwts_acpi_find_table(fw, "ERST", 0, &table) != FWTS_OK) {
		fwts_log_error(fw, "Cannot read ACPI tables.");
		return FWTS_ERROR;
	}
	if (table == NULL || (table && table->length == 0)) {
		fwts_log_error(fw, "ACPI ERST table does not exist, skipping test");
		return FWTS_SKIP;
	}
	return FWTS_OK;
}

/*
 *  ERST Extended System Description Table
 */
static int erst_test1(fwts_framework *fw)
{
	fwts_acpi_table_erst *erst = (fwts_acpi_table_erst*)table->data;
	bool passed = true;
	uint64_t total_length;
	uint32_t i;

	fwts_log_info_verbatim(fw, "ERST Error Record Serialization Table :");
	fwts_log_info_verbatim(fw, "  Serialization Hdr. Size:  0x%8.8" PRIx32, erst->serialization_header_size);
	fwts_log_info_verbatim(fw, "  Reserved:                 0x%8.8" PRIx32, erst->reserved);
	fwts_log_info_verbatim(fw, "  Instruction Entry Count:  0x%8.8" PRIx32, erst->instruction_entry_count);

	if (erst->reserved) {
		passed = false;
		fwts_failed(fw, LOG_LEVEL_LOW,
			"ERSTReservedNonZero",
			"ERST Reserved field must be zero, got 0x%" PRIx32
			" instead", erst->reserved);
	}
	if (erst->serialization_header_size > table->length) {
		passed = false;
		fwts_failed(fw, LOG_LEVEL_HIGH,
			"ERSTHeaderSizeTooLong",
			"ERST Serialization Header size %" PRIu32 " is greater "
			"than the entire table length of %zu",
			erst->serialization_header_size,
			table->length);
		goto done;
	}
	total_length = (sizeof(fwts_acpi_serialization_instruction_entries) *
		erst->instruction_entry_count) + sizeof(fwts_acpi_table_erst);
	if (total_length > table->length) {
		passed = false;
		fwts_failed(fw, LOG_LEVEL_HIGH,
			"ERSTTooManyEntries",
			"ERST size of %" PRIu64 " based on %" PRIu32
			" Serialization Instruction Entries of %zu "
			"bytes is longer the entire table length of %zu",
			total_length, erst->instruction_entry_count,
			sizeof(fwts_acpi_serialization_instruction_entries),
			table->length);
		goto done;
	}
	if (passed)
		fwts_log_info(fw, "ERST header looks sane.");

	for (i = 0; i < erst->instruction_entry_count; i++) {
		fwts_acpi_serialization_instruction_entries *entry = &erst->entries[i];
		bool entry_passed = true;

#if defined(ERST_DEBUG_ENTRY)
		/*  Disable this for now, it causes the test to be too verbose */
		fwts_log_info_verbatim(fw, "ERST Serialization Instruction Entry %" PRIu32 ":", i);
		fwts_log_info_verbatim(fw, "  Serialization Action:     0x%8.8" PRIx8, entry->serialization_action);
		fwts_log_info_verbatim(fw, "  Instruction:              0x%8.8" PRIx8, entry->instruction);
		fwts_log_info_verbatim(fw, "  Flags:                    0x%8.8" PRIx8, entry->flags);
		fwts_log_info_verbatim(fw, "  Reserved:                 0x%8.8" PRIx8, entry->reserved);
		fwts_log_info_verbatim(fw, "  Value:                    0x%16.16" PRIx64, entry->value);
		fwts_log_info_verbatim(fw, "  Mask:                     0x%16.16" PRIx64, entry->mask);
		fwts_log_info_verbatim(fw, "  Address Space ID:         0x%2.2" PRIx8, entry->register_region.address_space_id);
		fwts_log_info_verbatim(fw, "  Register Bit Width        0x%2.2" PRIx8, entry->register_region.register_bit_width);
		fwts_log_info_verbatim(fw, "  Register Bit Offset       0x%2.2" PRIx8, entry->register_region.register_bit_offset);
		fwts_log_info_verbatim(fw, "  Access Size               0x%2.2" PRIx8, entry->register_region.access_width);
		fwts_log_info_verbatim(fw, "  Address                   0x%16.16" PRIx64, entry->register_region.address);
#endif

		switch (entry->serialization_action) {
		case 0x00 ... 0x10:
			/* Allow for reserved too */
			break;
		default:
			entry_passed = false;
			fwts_failed(fw, LOG_LEVEL_HIGH,
				"ERSTIInvalidAction",
				"ERST Serialization Action 0x%" PRIx8
				" is an invalid value, values allowed are "
				"0x00..0x10",
				entry->serialization_action);
			break;
		}
		switch (entry->instruction) {
		case 0x00 ... 0x01:
		case 0x04 ... 0x12:
			break;
		case 0x02 ... 0x03:
			/*
			 *  "For WRITE_REGISTER and WRITE_REGISTER_VALUE instructions,
			 *  this flag indicates that bits within the register that
			 *  are not being written must be preserved rather than destroyed.
			 *  For READ_REGISTER instructions, this flag is ignored."
			 */
			if (entry->flags > 1) {
				entry_passed = false;
				fwts_failed(fw, LOG_LEVEL_HIGH,
					"ERSTIInvalidFlag",
					"ERST Serialization Flag 0x%" PRIx8
					" is an invalid value, values allowed are "
					"0x00..0x01 for the WRITE_REGISTER and "
					"WRITE_REGISTER_VALUE instructions",
					entry->flags);
			}
			break;
		default:
			entry_passed = false;
			fwts_failed(fw, LOG_LEVEL_HIGH,
				"ERSTIInvalidInstruction",
				"ERST Serialization Instruction 0x%" PRIx8
				" is an invalid value, values allowed are "
				"0x00..0x12",
				entry->instruction);
			break;
		}

		/* 0 = Undefined, 1 = byte ... 4 = QWord */
		if (entry->register_region.access_width > 4) {
			entry_passed = false;
			fwts_failed(fw, LOG_LEVEL_HIGH,
				"ERSTIInvalidGasAccessSize",
				"ERST Serialization Generic Address Access Size 0x%" PRIx8
				" is invalid, should be 0x00 .. 0x04",
				entry->register_region.access_width);
		}
		if (entry_passed)
			fwts_log_info(fw, "ERST Serialization Entry 0x%" PRIx32 " looks sane.", i);

		passed &= entry_passed;
	}

done: 	if (passed)
		fwts_passed(fw, "No issues found in ERST table.");

	return FWTS_OK;
}

static fwts_framework_minor_test erst_tests[] = {
	{ erst_test1, "ERST Error Record Serialization Table test." },
	{ NULL, NULL }
};

static fwts_framework_ops erst_ops = {
	.description = "ERST Error Record Serialization Table test.",
	.init        = erst_init,
	.minor_tests = erst_tests
};

FWTS_REGISTER("erst", &erst_ops, FWTS_TEST_ANYTIME, FWTS_FLAG_BATCH | FWTS_FLAG_TEST_ACPI)

#endif
