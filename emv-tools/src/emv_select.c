/*
 * emv-tools - a set of tools to work with EMV family of smart cards
 * Copyright (C) 2015 Dmitry Eremin-Solenikov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "openemv/scard.h"
#include "openemv/sc_helpers.h"
#include "openemv/tlv.h"
#include "openemv/emv_tags.h"
#include "openemv/dump.h"
#include "openemv/emv_commands.h"
#include "openemv/config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool print_cb(void *data, const struct tlv *tlv)
{
	emv_tag_dump(tlv, stdout);
	dump_buffer(tlv->value, tlv->len, stdout);

	return true;
}

static void dir_entry_cb(struct tlvdb *db, void *data)
{
	tlvdb_visit(db, print_cb, NULL);
	tlvdb_free(db);
}

static bool emv_parse_directory(const unsigned char *buf, size_t len,
		void (*cb)(struct tlvdb *db, void *data), void *data)
{
	while (len != 0) {
		struct tlv e;

		if (!tlv_parse_tl(&buf, &len, &e) || e.tag != 0x61 || e.len > len)
			return false;

		struct tlvdb *db = tlvdb_parse_multi(buf, e.len);

		if (!db)
			return false;

		buf += e.len;
		len -= e.len;

		cb(db, data);
	}

	return true;
}

int main(void)
{
	struct sc *sc;
	const unsigned char pse_name[] = {
		0x31, 0x50, 0x41, 0x59, 0x2e, 0x53, 0x59, 0x53, 0x2e, 0x44, 0x44, 0x46, 0x30, 0x31
	};
	const struct tlv pse_tlv = {
		.len = sizeof(pse_name),
		.value = pse_name,
	};

	sc = scard_init(NULL);
	if (!sc) {
		printf("Cannot init scard\n");
		return 1;
	}

	scard_connect(sc, openemv_config_get_int("scard.reader", 0));
	if (scard_is_error(sc)) {
		printf("%s\n", scard_error(sc));
		return 1;
	}

	struct tlvdb *pse = emv_select(sc, &pse_tlv);
	if (!pse)
		return 1;

	const struct tlv *e = tlvdb_get(pse, 0x88, NULL);
	if (!e)
		return 1;
	unsigned char sfi = e->value[0];

	int i;
	for (i = 1; ; i++) {
		unsigned short sw;
		size_t outlen;
		unsigned char *outbuf = emv_read_record(sc, sfi, i, &sw, &outlen);

		if (sw == 0x6a83)
			break;
		else if (sw != 0x9000 || !outbuf)
			return 1;

		struct tlv e;
		size_t tmplen = outlen;
		const unsigned char *tmp = outbuf;

		if (!tlv_parse_tl(&tmp, &tmplen, &e) || e.tag != 0x70 || e.len != tmplen) {
			free(outbuf);

			return 1;
		}

		if (!emv_parse_directory(tmp, tmplen, dir_entry_cb, NULL)) {
			free(outbuf);

			return 1;
		}
	}

	printf("Final\n");
	tlvdb_visit(pse, print_cb, NULL);
	tlvdb_free(pse);

	scard_disconnect(sc);
	if (scard_is_error(sc)) {
		printf("%s\n", scard_error(sc));
		return 1;
	}
	scard_shutdown(sc);

	return 0;
}
