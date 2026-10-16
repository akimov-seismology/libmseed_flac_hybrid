/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
/* Keep the original samples in RAM, then compare the second file record by
 * record, independently per channel. No temporary files or per-SID handles. */
#include "libmseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#define BUCKETS 8192

typedef struct Original {
  struct Original *next;
  nstime_t start;double rate;int64_t count;
  uint8_t flags,pubversion;
  int32_t data[];
} Original;
typedef struct Channel {
  char sid[LM_SIDLEN];Original *head,*tail;
  int64_t offset,compared;
  struct Channel *next,*hash_next;
} Channel;
typedef struct {Channel *channels,*buckets[BUCKETS];size_t count;int64_t matched;} State;
static Channel *find_channel(State *s,const char *sid,int create){
  uint32_t hash=2166136261u;
  for(const unsigned char *p=(const unsigned char *)sid;*p;p++)hash=(hash^*p)*16777619u;
  size_t bucket=hash%BUCKETS;
  for(Channel *c=s->buckets[bucket];c;c=c->hash_next)if(!strcmp(c->sid,sid))return c;
  if(!create)return NULL;
  Channel *c=calloc(1,sizeof(*c));
  if(!c){fputs("Insufficient RAM for comparison channel state\n",stderr);return NULL;}
  strcpy(c->sid,sid);c->next=s->channels;s->channels=c;s->count++;
  c->hash_next=s->buckets[bucket];s->buckets[bucket]=c;return c;
}
static int remember(State *s,const MS3Record *r){
  if((uint64_t)r->numsamples>(SIZE_MAX-sizeof(Original))/4){fputs("Original record exceeds addressable RAM\n",stderr);return 2;}
  Channel *c=find_channel(s,r->sid,1);if(!c)return 2;
  size_t bytes=sizeof(Original)+(size_t)r->numsamples*4;
  Original *a=malloc(bytes);
  if(!a){fprintf(stderr,"Insufficient RAM loading %s: cannot allocate %zu bytes\n",r->sid,bytes);return 2;}
  a->next=NULL;a->start=r->starttime;a->rate=r->samprate;a->count=r->numsamples;
  a->flags=r->flags;a->pubversion=r->pubversion;
  memcpy(a->data,r->datasamples,(size_t)r->numsamples*4);
  if(c->tail)c->tail->next=a;else c->head=a;c->tail=a;return 0;
}
static int compare_record(State *s,const MS3Record *r){
  Channel *c=find_channel(s,r->sid,0);
  if(!c){fprintf(stderr,"Channel absent from original: %s\n",r->sid);return 1;}
  int64_t offset=0;
  while(offset<r->numsamples){
    Original *a=c->head;
    if(!a){fprintf(stderr,"Extra samples: %s at sample %" PRId64 "\n",c->sid,c->compared);return 1;}
    nstime_t ta=ms_sampletime(a->start,c->offset,a->rate),tb=ms_sampletime(r->starttime,offset,r->samprate);
    if(a->rate!=r->samprate||a->flags!=r->flags||a->pubversion!=r->pubversion||
       ta==NSTERROR||tb==NSTERROR||fabsl((long double)ta-(long double)tb)>500){
      fprintf(stderr,"Timing/rate/flags mismatch: %s at sample %" PRId64 ": time_ns=%" PRId64 "/%" PRId64 " rate=%.17g/%.17g flags=%u/%u pubversion=%u/%u\n",c->sid,c->compared,ta,tb,a->rate,r->samprate,a->flags,r->flags,a->pubversion,r->pubversion);return 1;
    }
    int64_t n=a->count-c->offset;if(n>r->numsamples-offset)n=r->numsamples-offset;
    const int32_t *left=a->data+c->offset,*right=(const int32_t *)r->datasamples+offset;
    if(memcmp(left,right,(size_t)n*4)){
      int64_t j=0;while(j<n&&left[j]==right[j])j++;
      fprintf(stderr,"Sample mismatch: %s at sample %" PRId64 ": %" PRId32 " vs %" PRId32 "\n",c->sid,c->compared+j,left[j],right[j]);return 1;
    }
    offset+=n;c->offset+=n;c->compared+=n;s->matched+=n;
    if(c->offset==a->count){c->head=a->next;c->offset=0;free(a);}
  }
  return 0;
}
/* 0=match, 1=actual mismatch, 2=operational/unsupported-input error. */
static int read_file(const char *path,State *s,int original){
  struct stat source;
  if(stat(path,&source)||!S_ISREG(source.st_mode)){fprintf(stderr,"Cannot inspect local input: %s\n",path);return 2;}
  fprintf(stderr,"%s: %s\n",original?"Loading original samples into RAM":"Comparing repacked samples",path);
  time_t last_progress=time(NULL);int64_t processed=0;
  int64_t bytes=0;MS3FileParam *reader=NULL;MS3Record *r=NULL;int status,failed=0;
  while((status=ms3_readmsr_r(&reader,&r,path,MSF_UNPACKDATA,0))==MS_NOERROR){
    bytes+=r->reclen;
    if(r->sampletype!='i'||r->numsamples<=0||!r->datasamples){
      fprintf(stderr,"Unsupported comparison record: %s, encoding=%d type=%c samples=%" PRId64 "\n",r->sid,r->encoding,r->sampletype?r->sampletype:'?',r->numsamples);failed=2;break;
    }
    failed=original?remember(s,r):compare_record(s,r);
    if(failed)break;
    processed+=r->numsamples;
    if(reader->recordcount%1024==0&&time(NULL)-last_progress>=5){
      fprintf(stderr,"%s: %" PRId64 " samples, %zu channels, %" PRId64 " records\n",original?"Loaded":"Compared",processed,s->count,reader->recordcount);last_progress=time(NULL);
    }
  }
  if(!failed&&status!=MS_ENDOFFILE){fprintf(stderr,"Read failed: %s: %s (status %d)\n",path,ms_errorstr(status),status);failed=2;}
  if(!failed&&bytes!=source.st_size){fprintf(stderr,"Incomplete input: %s: parsed %" PRId64 " of %" PRId64 " bytes\n",path,bytes,(int64_t)source.st_size);failed=2;}
  ms3_readmsr_r(&reader,&r,NULL,0,0);return failed;
}
int main(int argc,char **argv){
  if(argc!=3){fprintf(stderr,"Usage: %s original.mseed repacked.mseed\n",argv[0]);return 2;}
  State *s=calloc(1,sizeof(*s));if(!s){fputs("Insufficient RAM for comparison state\n",stderr);return 2;}
  int failed=read_file(argv[1],s,1);
  if(!failed)failed=read_file(argv[2],s,0);
  if(!failed)for(Channel *c=s->channels;c;c=c->next)if(c->head){
    fprintf(stderr,"Missing samples in second file: %s after sample %" PRId64 "\n",c->sid,c->compared);failed=1;break;
  }
  int64_t total=s->matched;size_t channels=s->count;
  while(s->channels){Channel *c=s->channels;s->channels=c->next;
    while(c->head){Original *a=c->head;c->head=a->next;free(a);}free(c);
  }
  free(s);
  if(failed==2)fprintf(stderr,"ERROR: comparison could not complete; no mismatch conclusion. Matched %" PRId64 " samples before the error.\n",total);
  else printf("%s: %" PRId64 " matching int32 samples, channel timing, rates and flags; %zu channels\n",failed?"FAIL":"PASS",total,channels);
  return failed;
}
