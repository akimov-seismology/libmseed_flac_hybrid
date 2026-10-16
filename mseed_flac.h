/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#ifndef MSEED_FLAC_H
#define MSEED_FLAC_H
#include <stdint.h>
#include <libmseed.h>

/* Pack one record. Returns samples consumed (>0), or -1. Input is one stream.
 * metadata supplies sid/starttime/samprate/flags/extra; output size is reclen.
 * Call again with advanced starttime and samples for remaining input. */
int msflac_pack_record(const MS3Record *metadata, const int32_t *samples,
                      int count, char *output, uint32_t reclen);
/* Parse and decode one compact FLAC21 record, checking record/frame CRCs.
 * Returns 0 on success or -1. Free returned record with msr3_free(). */
int msflac_unpack_record(const char *record, uint32_t reclen, MS3Record **decoded);
#endif
