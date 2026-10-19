
// Based on https://github.com/gorhill/lz4-wasm by Raymond Hill
// armorpaint/base/assets/licenses/license_lz4-wasm.md
#include "iron_lz4.h"

#include "iron_alloc.h"
#include "iron_system.h"
#include <stdint.h>
#include <stdlib.h>

static i32_array_t *_lz4_hash_table = NULL;

uint32_t lz4_encode_bound(uint32_t size) {
	uint32_t i = size / 255;
	return size > 0x7e000000 ? 0 : size + (i | 0) + 16;
}

buffer_t *lz4_encode(buffer_t *b) {
	u8_array_t *ibuf = (u8_array_t *)b;
	uint32_t    ilen = ibuf->length;
	if (ilen >= 0x7e000000) {
		iron_log("LZ4 range error");
		return NULL;
	}

	uint32_t last_match_pos   = ilen - 12;
	uint32_t last_literal_pos = ilen - 5;

	if (_lz4_hash_table == NULL) {
		_lz4_hash_table = i32_array_create(65536);
	}
	for (uint32_t i = 0; i < _lz4_hash_table->length; ++i) {
		_lz4_hash_table->buffer[i] = -65536;
	}

	uint32_t    olen       = lz4_encode_bound(ilen);
	u8_array_t *obuf       = u8_array_create(olen);
	uint32_t    ipos       = 0;
	uint32_t    opos       = 0;
	uint32_t    anchor_pos = 0;

	// Sequence-finding loop
	while (true) {
		uint32_t ref_pos  = 0;
		uint32_t moffset  = 0;
		uint32_t sequence = ((uint32_t)ibuf->buffer[ipos] << 8) | ((uint32_t)ibuf->buffer[ipos + 1] << 16) | ((uint32_t)ibuf->buffer[ipos + 2] << 24);

		// Match-finding loop
		while (ipos <= last_match_pos) {
			sequence                      = (sequence >> 8) | ((uint32_t)ibuf->buffer[ipos + 3] << 24);
			uint32_t hash                 = ((sequence * 0x9e37) & 0xffff) + ((sequence * 0x79b1) >> 16) & 0xffff;
			ref_pos                       = _lz4_hash_table->buffer[hash];
			_lz4_hash_table->buffer[hash] = ipos;
			moffset                       = ipos - ref_pos;
			if (moffset < 65536 && ibuf->buffer[ref_pos + 0] == ((sequence) & 0xff) && ibuf->buffer[ref_pos + 1] == ((sequence >> 8) & 0xff) &&
			    ibuf->buffer[ref_pos + 2] == ((sequence >> 16) & 0xff) && ibuf->buffer[ref_pos + 3] == ((sequence >> 24) & 0xff)) {
				break;
			}
			ipos += 1;
		}

		// No match found
		if (ipos > last_match_pos) {
			break;
		}

		// Match found
		uint32_t llen = ipos - anchor_pos;
		uint32_t mlen = ipos;
		ipos += 4;
		ref_pos += 4;
		while (ipos < last_literal_pos && ibuf->buffer[ipos] == ibuf->buffer[ref_pos]) {
			ipos += 1;
			ref_pos += 1;
		}
		mlen           = ipos - mlen;
		uint32_t token = mlen < 19 ? mlen - 4 : 15;

		// Write token, length of literals if needed
		if (llen >= 15) {
			obuf->buffer[opos++] = 0xf0 | token;
			uint32_t l           = llen - 15;
			while (l >= 255) {
				obuf->buffer[opos++] = 255;
				l -= 255;
			}
			obuf->buffer[opos++] = l;
		}
		else {
			obuf->buffer[opos++] = (llen << 4) | token;
		}

		// Write literals
		while (llen-- > 0) {
			obuf->buffer[opos++] = ibuf->buffer[anchor_pos++];
		}

		if (mlen == 0) {
			break;
		}

		// Write offset of match
		obuf->buffer[opos + 0] = moffset;
		obuf->buffer[opos + 1] = moffset >> 8;
		opos += 2;

		// Write length of match if needed
		if (mlen >= 19) {
			uint32_t l = mlen - 19;
			while (l >= 255) {
				obuf->buffer[opos++] = 255;
				l -= 255;
			}
			obuf->buffer[opos++] = l;
		}

		anchor_pos = ipos;
	}

	// Last sequence is literals only
	uint32_t llen = ilen - anchor_pos;
	if (llen >= 15) {
		obuf->buffer[opos++] = 0xf0;
		uint32_t l           = llen - 15;
		while (l >= 255) {
			obuf->buffer[opos++] = 255;
			l -= 255;
		}
		obuf->buffer[opos++] = l;
	}
	else {
		obuf->buffer[opos++] = llen << 4;
	}
	while (llen-- > 0) {
		obuf->buffer[opos++] = ibuf->buffer[anchor_pos++];
	}

	buffer_t *result = buffer_slice(obuf, 0, opos);
	array_free(obuf);
	free(obuf);
	return result;
}

static bool lz4_read(buffer_t *b, uint32_t olen, uint8_t *out) {
	if (b == NULL || b->buffer == NULL || b->length == 0) return false;
	uint32_t ipos = 0, opos = 0;
	while (ipos < b->length) {
		uint32_t token = b->buffer[ipos++];
		uint32_t count = token >> 4;
		if (count == 15) {
			uint32_t extra;
			do {
				if (ipos == b->length) return false;
				extra = b->buffer[ipos++];
				if (count > olen || extra > olen - count) return false;
				count += extra;
			} while (extra == 255);
		}
		if (count > b->length - ipos || count > olen - opos) return false;
		if (out != NULL) memcpy(out + opos, b->buffer + ipos, count);
		ipos += count;
		opos += count;
		if (ipos == b->length) return opos == olen;
		if (b->length - ipos < 2) return false;
		uint32_t offset = b->buffer[ipos] | (b->buffer[ipos + 1] << 8);
		ipos += 2;
		if (offset == 0 || offset > opos) return false;
		count = (token & 15) + 4;
		if (count == 19) {
			uint32_t extra;
			do {
				if (ipos == b->length) return false;
				extra = b->buffer[ipos++];
				if (count > olen - opos || extra > olen - opos - count) return false;
				count += extra;
			} while (extra == 255);
		}
		if (count > olen - opos)
			return false;
		if (out != NULL)
			for (uint32_t i = 0; i < count; ++i)
				out[opos + i] = out[opos + i - offset];
		opos += count;
	}
	return opos == olen;
}

bool lz4_validate(buffer_t *b, uint32_t olen) {
	return lz4_read(b, olen, NULL);
}

buffer_t *lz4_decode(buffer_t *b, uint32_t olen) {
	if (!lz4_validate(b, olen))
		return NULL;
	u8_array_t *out = u8_array_create(olen);
	if (out == NULL || out->buffer == NULL) {
		free(out);
		return NULL;
	}
	lz4_read(b, olen, out->buffer);
	return out;
}
