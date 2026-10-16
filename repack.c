/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
/* Bounded per-channel buffering; no samples cross a discontinuity. */
#include "libmseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdatomic.h>

typedef struct {
  FILE *file; atomic_int failed; int length; int64_t records, samples;
  pthread_mutex_t mutex;
} Writer;
typedef struct Stream {
  char sid[LM_SIDLEN];
  MS3Record *metadata;             /* Owns a copy of extra headers, never raw input. */
  nstime_t origin;                 /* Fixed origin prevents repeated timestamp rounding. */
  int64_t received, emitted;
  int32_t *samples;
  size_t count, capacity;
  struct Stream *next;
  struct Stream *hash_next;
  struct Stream *job_next;
  int pending, job_flush, job_result;
} Stream;

/* Only the reader accesses this index. Keep the ownership/flush list in
 * insertion order so changing lookup does not reorder channel output. */
#define STREAM_BUCKETS 8192u
static unsigned stream_bucket(const char *sid) {
  uint32_t hash=2166136261u;
  for(const unsigned char *p=(const unsigned char *)sid;*p;p++)
    hash=(hash^*p)*16777619u;
  return hash&(STREAM_BUCKETS-1);
}

static void write_record(char *record, int length, void *context) {
  Writer *writer = context;
  pthread_mutex_lock(&writer->mutex);
  if (length != writer->length ||
      fwrite(record, 1, (size_t)length, writer->file) != (size_t)length)
    { perror("Writing output record"); writer->failed = 1; }
  writer->records++;
  pthread_mutex_unlock(&writer->mutex);
}

/* Emit filled records now; only flush a partial last record at a boundary/EOF. */
static int emit(Stream *stream, Writer *writer, int flush) {
  if (!stream->count) return 0;
  MS3Record record = *stream->metadata;
  record.starttime = ms_sampletime(stream->origin, stream->emitted, record.samprate);
  if (record.starttime == NSTERROR) { fprintf(stderr, "%s: Cannot calculate output start time\n", stream->sid); return -1; }
  record.datasamples = stream->samples;
  record.datasize = stream->count * sizeof(int32_t);
  record.numsamples = record.samplecnt = (int64_t)stream->count;
  int64_t packed = 0;
  int records = msr3_pack(&record, write_record, writer, &packed,
                         MSF_PACKVER2 | (flush ? MSF_FLUSHDATA : 0), 0);
  if (records < 0 || writer->failed || packed < 0 || (uint64_t)packed > stream->count ||
      (flush && (uint64_t)packed != stream->count)) {
    char time[64];
    if (!ms_nstime2timestr_n(record.starttime, time, sizeof(time), ISOMONTHDAY_Z, NANO)) strcpy(time, "unknown");
    fprintf(stderr, "Packing failed: channel=%s start=%s rate=%.17g Hz encoding=%d reclen=%d buffered=%zu flush=%d packed=%" PRId64 " records=%d extra_headers=%u bytes\n",
            stream->sid, time, msr3_sampratehz(&record), record.encoding,
            record.reclen, stream->count, flush, packed, records, record.extralength);
    return -1;
  }
  stream->count -= (size_t)packed;
  memmove(stream->samples, stream->samples + packed, stream->count * sizeof(int32_t));
  stream->emitted += packed;
  pthread_mutex_lock(&writer->mutex);
  writer->samples += packed;
  pthread_mutex_unlock(&writer->mutex);
  return 0;
}

/* At most one task owns a channel at a time. The reader waits before
 * touching that channel again; other channels may pack concurrently.
 * The queue contains channel pointers, not copies of sample buffers. */
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t changed;
  pthread_t *threads;
  size_t count, queued, limit;
  int stop;
  Stream *head, *tail;
  Writer *writer;
} Pool;

static void *worker(void *context) {
  Pool *pool=context;
  for(;;){
    pthread_mutex_lock(&pool->mutex);
    while(!pool->head && !pool->stop)pthread_cond_wait(&pool->changed,&pool->mutex);
    if(!pool->head && pool->stop){pthread_mutex_unlock(&pool->mutex);return NULL;}
    Stream *stream=pool->head;
    pool->head=stream->job_next;if(!pool->head)pool->tail=NULL;
    pool->queued--;pthread_cond_broadcast(&pool->changed);
    pthread_mutex_unlock(&pool->mutex);
    int result=pool->writer->failed?-1:emit(stream,pool->writer,stream->job_flush);
    if(result)pool->writer->failed=1;
    pthread_mutex_lock(&pool->mutex);
    stream->job_result=result;stream->pending=0;
    pthread_cond_broadcast(&pool->changed);pthread_mutex_unlock(&pool->mutex);
  }
}

static int pool_init(Pool *pool,Writer *writer,size_t threads) {
  memset(pool,0,sizeof(*pool));pool->writer=writer;pool->limit=threads*2;
  if(pthread_mutex_init(&pool->mutex,NULL))return -1;
  if(pthread_cond_init(&pool->changed,NULL)){pthread_mutex_destroy(&pool->mutex);return -1;}
  pool->threads=calloc(threads,sizeof(pthread_t));
  if(!pool->threads)goto fail;
  for(;pool->count<threads;pool->count++){
    int error=pthread_create(&pool->threads[pool->count],NULL,worker,pool);
    if(error){fprintf(stderr,"Cannot create worker: %s\n",strerror(error));goto fail;}
  }
  return 0;
fail:
  pthread_mutex_lock(&pool->mutex);pool->stop=1;
  pthread_cond_broadcast(&pool->changed);pthread_mutex_unlock(&pool->mutex);
  for(size_t i=0;i<pool->count;i++)pthread_join(pool->threads[i],NULL);
  free(pool->threads);pthread_cond_destroy(&pool->changed);pthread_mutex_destroy(&pool->mutex);
  return -1;
}

static int wait_channel(Pool *pool,Stream *stream) {
  if(!pool->threads)return 0;
  pthread_mutex_lock(&pool->mutex);
  while(stream->pending)pthread_cond_wait(&pool->changed,&pool->mutex);
  int result=stream->job_result;
  pthread_mutex_unlock(&pool->mutex);return result;
}

static int schedule(Pool *pool,Stream *stream,int flush) {
  if(!pool->threads)return emit(stream,pool->writer,flush);
  if(wait_channel(pool,stream))return -1;
  pthread_mutex_lock(&pool->mutex);
  while(pool->queued>=pool->limit && !pool->writer->failed)
    pthread_cond_wait(&pool->changed,&pool->mutex);
  if(pool->writer->failed){pthread_mutex_unlock(&pool->mutex);return -1;}
  stream->job_flush=flush;stream->pending=1;stream->job_next=NULL;
  if(pool->tail)pool->tail->job_next=stream;else pool->head=stream;
  pool->tail=stream;pool->queued++;
  pthread_cond_broadcast(&pool->changed);pthread_mutex_unlock(&pool->mutex);return 0;
}

static void pool_finish(Pool *pool) {
  if(!pool->threads)return;
  pthread_mutex_lock(&pool->mutex);pool->stop=1;
  pthread_cond_broadcast(&pool->changed);pthread_mutex_unlock(&pool->mutex);
  for(size_t i=0;i<pool->count;i++)pthread_join(pool->threads[i],NULL);
  free(pool->threads);pool->threads=NULL;
  pthread_cond_destroy(&pool->changed);pthread_mutex_destroy(&pool->mutex);
}

static int same_metadata(const MS3Record *a, const MS3Record *b) {
  return a->samprate == b->samprate && a->flags == b->flags &&
         a->pubversion == b->pubversion && a->extralength == b->extralength &&
         (!a->extralength || !memcmp(a->extra, b->extra, a->extralength));
}

static int start_segment(Stream *stream, const MS3Record *input, int encoding, int length) {
  MS3Record *metadata = msr3_duplicate(input, 0);
  if (!metadata) { fprintf(stderr, "%s: Cannot copy record metadata\n", input->sid); return -1; }
  metadata->record = NULL;
  metadata->datasamples = NULL;
  metadata->encoding = encoding;
  metadata->formatversion = 2;
  metadata->reclen = length;
  msr3_free(&stream->metadata);
  stream->metadata = metadata;
  stream->origin = input->starttime;
  stream->received = stream->emitted = 0;
  return 0;
}

static int parse_number(const char *text, long *number) {
  char *end; *number = strtol(text, &end, 10);
  return *text && !*end;
}

int main(int argc, char **argv) {
  if(argc==2 && !strcmp(argv[1],"--version")){
    puts("repack compact-crc32 FLAC21 (adaptive sample width; CRC32; multithreaded)");
    return 0;
  }
  if (argc != 5 && argc != 7) {
    fprintf(stderr, "Usage: %s input.mseed output.mseed 11|21 record_length [--threads 1..64]\n", argv[0]);
    return 2;
  }
  long enc, len, threads=1;
  if(argc==7 && (strcmp(argv[5],"--threads") || !parse_number(argv[6],&threads) || threads<1 || threads>64)){
    fputs("Use --threads followed by a count from 1 to 64\n",stderr);return 2;
  }
  if (!strcmp(argv[1], argv[2]) || !parse_number(argv[3], &enc) ||
      !parse_number(argv[4], &len) || (enc != 11 && enc != 21) ||
      len < 512 || len > 1048576 || (len & (len - 1))) {
    fputs("Use distinct files, encoding 11 or 21, and a power-of-two length >=512\n", stderr);
    return 2;
  }
  struct stat source;
  if (stat(argv[1], &source) || !S_ISREG(source.st_mode)) {
    fputs("Input must be a readable local regular miniSEED file\n", stderr); return 1;
  }
  int encoding = (int)enc, length = (int)len;
  /* Conservative upper bound of samples needed for a complete record. */
  size_t threshold = encoding == DE_FLAC_EXPERIMENTAL ? 16384 : (size_t)(length / 64) * 105;
  if (threshold > UINT16_MAX) threshold = UINT16_MAX;
  FILE *out = fopen(argv[2], "wbx");
  if (!out) { perror(argv[2]); return 1; }
  Writer writer = {.file=out,.failed=0,.length=length,.records=0,.samples=0,
                   .mutex=PTHREAD_MUTEX_INITIALIZER};
  Pool pool={0};pool.writer=&writer;
  if(threads>1 && pool_init(&pool,&writer,(size_t)threads)){
    fclose(out);remove(argv[2]);pthread_mutex_destroy(&writer.mutex);return 1;
  }
  Stream *streams = NULL;
  Stream *stream_index[STREAM_BUCKETS]={0};
  MS3Record *input = NULL;
  MS3FileParam *reader = NULL;
  int result, failed = 0;
  int64_t input_samples = 0, input_bytes = 0, boundaries = 0;
  size_t channels = 0;
  while ((result = ms3_readmsr_r(&reader, &input, argv[1], MSF_UNPACKDATA, 0)) == MS_NOERROR) {
    if (input->reclen < 0 || input_bytes > INT64_MAX - input->reclen) { failed = 1; break; }
    input_bytes += input->reclen;
    if (input->sampletype != 'i' || input->numsamples <= 0 || !input->datasamples ||
        input->numsamples != input->samplecnt || !isfinite(input->samprate) || input->samprate == 0) {
      fprintf(stderr, "Unsupported input record: channel=%s record=%" PRId64 " encoding=%d sampletype=%c samples=%" PRId64 " header_samples=%" PRId64 " rate=%.17g\n",
              input->sid, reader->recordcount, input->encoding,
              input->sampletype ? input->sampletype : '?', input->numsamples,
              input->samplecnt, input->samprate);
      failed = 1; break;
    }
    unsigned bucket=stream_bucket(input->sid);
    Stream *stream;
    for (stream = stream_index[bucket]; stream; stream = stream->hash_next)
      if (!strcmp(stream->sid, input->sid)) break;
    if (!stream) {
      stream = calloc(1, sizeof(*stream));
      if (!stream) { perror("Allocating channel state"); failed = 1; break; }
      strcpy(stream->sid, input->sid);
      stream->capacity = 2 * threshold;
      stream->samples = malloc(stream->capacity * sizeof(int32_t));
      stream->next = streams; streams = stream; channels++;
      if (!stream->samples) { perror("Allocating channel sample buffer"); failed = 1; break; }
      stream->hash_next=stream_index[bucket];stream_index[bucket]=stream;
    }
    if(wait_channel(&pool,stream) || writer.failed){failed=1;break;}
    int contiguous = 0;
    if (stream->metadata && same_metadata(stream->metadata, input)) {
      nstime_t expected = ms_sampletime(stream->origin, stream->received, input->samprate);
      /* miniSEED2 starts are rounded to microseconds. Accept only rounding noise. */
      contiguous = expected != NSTERROR &&
                   fabsl((long double)input->starttime - (long double)expected) <= 500;
    }
    if (!contiguous) {
      if (stream->metadata) { boundaries++; if (emit(stream, &writer, 1)) { failed = 1; break; } }
      if (start_segment(stream, input, encoding, length)) { failed = 1; break; }
    }
    if (stream->received > INT64_MAX - input->numsamples ||
        input_samples > INT64_MAX - input->numsamples) { failed = 1; break; }
    stream->received += input->numsamples;
    input_samples += input->numsamples;
    size_t offset = 0;
    while (offset < (size_t)input->numsamples) {
      if(wait_channel(&pool,stream)){failed=1;break;}
      size_t n = (size_t)input->numsamples - offset;
      if (n > stream->capacity - stream->count) n = stream->capacity - stream->count;
      if (!n) { fprintf(stderr, "%s: Channel buffer made no packing progress\n", stream->sid); failed = 1; break; }
      memcpy(stream->samples + stream->count, (int32_t *)input->datasamples + offset, n * sizeof(int32_t));
      stream->count += n; offset += n;
      if (stream->count >= threshold && schedule(&pool,stream,0)) { failed = 1; break; }
    }
    if (failed) break;
  }
  if (result != MS_ENDOFFILE) {
    if (!failed) fprintf(stderr, "Input read failed: %s (status %d)\n", ms_errorstr(result), result);
    failed = 1;
  }
  if (!failed && input_bytes != source.st_size) {
    fputs("Input has truncated records or unconsumed trailing bytes\n", stderr); failed = 1;
  }
  if (!failed)
    for (Stream *stream = streams; stream; stream = stream->next)
      if (schedule(&pool,stream,1)) { failed = 1; break; }
  /* Drain before freeing any channel state, even following a read error. */
  pool_finish(&pool);
  ms3_readmsr_r(&reader, &input, NULL, 0, 0);
  while (streams) {
    Stream *next = streams->next;
    msr3_free(&streams->metadata); free(streams->samples); free(streams); streams = next;
  }
  if (fclose(out)) { perror("Closing output file"); failed = 1; }
  pthread_mutex_destroy(&writer.mutex);
  if (failed || writer.failed || writer.samples != input_samples) {
    remove(argv[2]); fputs("Conversion failed; incomplete output removed. See diagnostic above.\n", stderr); return 1;
  }
  printf("Packed %" PRId64 " samples in %" PRId64 " records, %" PRId64 " bytes; %zu channels, %" PRId64 " discontinuities/metadata changes\n",
         writer.samples, writer.records, writer.records * length, channels, boundaries);
  return 0;
}
