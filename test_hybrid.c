/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#include "libmseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#define N 70000
typedef struct {MS3Record *meta; int32_t *samples; int count,records,length; FILE *file;} Context;
static void verify(char *r,int length,void *p) {
  Context *c=p;MS3Record *out=NULL;
  CHECK(length==c->length);
  CHECK(msr3_parse(r,length,&out,MSF_UNPACKDATA,0)==MS_NOERROR);
  CHECK(out->numsamples>0 && c->count+out->numsamples<=N);
  CHECK(out->encoding==c->meta->encoding);
  CHECK(out->starttime==ms_sampletime(c->meta->starttime,c->count,c->meta->samprate));
  CHECK(!strcmp(out->sid,c->meta->sid));
  CHECK(out->samprate==40 && out->sampletype=='i');
  CHECK(!memcmp(out->datasamples,c->samples+c->count,(size_t)out->numsamples*4));
  c->count+=(int)out->numsamples;c->records++;
  if(c->file)CHECK(fwrite(r,1,length,c->file)==(size_t)length);
  if(out->encoding==21){
    unsigned offset=((unsigned char)r[44]<<8)|(unsigned char)r[45];
    CHECK(offset+16<(unsigned)length-4);
    CHECK((unsigned char)r[offset]==255 && ((unsigned char)r[offset+1]&254)==248);
    msr3_free(&out);r[length/2]^=1;
    CHECK(msr3_parse(r,length,&out,MSF_UNPACKDATA,0)!=MS_NOERROR);
    r[length/2]^=1;
  }
  msr3_free(&out);
}
int main(void) {
  int32_t *samples=malloc(N*4);CHECK(samples);
  FILE *file=fopen("hybrid_test.mseed","wb");CHECK(file);
  const char *sids[]={"FDSN:IU_TEST_00_B_H_Z","FDSN:IU_TEST_00_B_H_N","FDSN:IU_TEST_00_B_H_E"};
  int64_t expected=0;
  for(int sz=512;sz<=4096;sz*=8)for(int ch=0;ch<3;ch++)for(int enc=11;enc<=21;enc+=10){
    for(int i=0;i<N;i++)samples[i]=(i%1000)-500+ch*10000;
    if(enc==21){samples[0]=INT32_MIN;samples[1]=INT32_MAX;}
    MS3Record meta=MS3Record_INITIALIZER;strcpy(meta.sid,sids[ch]);
    meta.starttime=ms_timestr2nstime("2024-01-02T00:00:00.019538Z");
    meta.samprate=40;meta.encoding=enc;meta.formatversion=2;meta.reclen=sz;
    meta.datasamples=samples;meta.numsamples=N;meta.samplecnt=N;meta.sampletype='i';
    Context c={&meta,samples,0,0,sz,file};int64_t packed=-1;
    CHECK(msr3_pack(&meta,verify,&c,&packed,MSF_FLUSHDATA,0)>0);
    CHECK(packed==N&&c.count==N);expected+=N;
    // Stateful API must behave identically to callback packing.
    c.count=0;c.records=0;c.file=NULL;
    MS3RecordPacker *packer=msr3_pack_init(&meta,MSF_FLUSHDATA,0);CHECK(packer);
    char *r;int32_t length;int result;
    while((result=msr3_pack_next(packer,&r,&length))==1)verify(r,length,&c);
    CHECK(result==0&&c.count==N);msr3_pack_free(&packer,&packed);CHECK(packed==N);
    if(enc==21){
      MS3TraceList *tl=mstl3_init(NULL);CHECK(tl);
      CHECK(mstl3_addmsr(tl,&meta,0,1,0,NULL));
      c.count=0;c.records=0;
      CHECK(mstl3_pack(tl,verify,&c,sz,DE_FLAC_EXPERIMENTAL,&packed,
            MSF_FLUSHDATA|MSF_MAINTAINMSTL|MSF_PACKVER2,0,NULL)>0);
      CHECK(packed==N&&c.count==N);mstl3_free(&tl,0);
      meta.formatversion=3;CHECK(!msr3_pack_init(&meta,MSF_FLUSHDATA,0));
      meta.formatversion=2;meta.samprate=40.5;CHECK(!msr3_pack_init(&meta,MSF_FLUSHDATA,0));
    }
    printf("PASS encoding %d, %d-byte records, component %d\n",enc,sz,ch);
  }
  CHECK(!fclose(file));MS3Record *read=NULL;MS3FileParam *reader=NULL;
  int result;int64_t actual=0;int saw11=0,saw21=0;
  while((result=ms3_readmsr_r(&reader,&read,"hybrid_test.mseed",MSF_UNPACKDATA,0))==MS_NOERROR){
    actual+=read->numsamples;saw11|=read->encoding==11;saw21|=read->encoding==21;
  }
  CHECK(result==MS_ENDOFFILE&&actual==expected&&saw11&&saw21);
  ms3_readmsr_r(&reader,&read,NULL,0,0);free(samples);
  puts("PASS normal APIs: callback/stateful/trace-list/mixed-file reader, exact int32, CRC, 512/4096, >65535 samples");
  return 0;
}
