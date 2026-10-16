/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#include "mseed_flac.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {int32_t *data; int consumed,records;} Context;
static void verify(char *raw,int length,void *context){
  Context *c=context;CHECK(length==512);
  unsigned char *r=(unsigned char *)raw;
  unsigned offset=((unsigned)r[44]<<8)|r[45];CHECK(offset==244);
  unsigned pos=((unsigned)r[46]<<8)|r[47],calibrations=0,steps=0;
  while(pos){
    CHECK(++steps<=r[39]&&pos+4<=offset);
    unsigned type=((unsigned)r[pos]<<8)|r[pos+1];
    if(type==300){CHECK(pos+60<=offset);calibrations++;}
    pos=((unsigned)r[pos+2]<<8)|r[pos+3];
  }
  CHECK(calibrations==3);
  MS3Record *out=NULL;
  CHECK(msr3_parse(raw,length,&out,MSF_UNPACKDATA,0)==MS_NOERROR);
  CHECK(out->encoding==21&&out->numsamples>0&&c->consumed+out->numsamples<=4000);
  CHECK(!strcmp(out->sid,"FDSN:TA_G05D__B_H_Z"));
  CHECK(out->starttime==ms_sampletime(ms_timestr2nstime("2016-02-16T23:06:48.274998Z"),c->consumed,40));
  CHECK(out->extra&&strstr(out->extra,"Calibration")&&strstr(out->extra,"STEP"));
  CHECK(!memcmp(c->data+c->consumed,out->datasamples,(size_t)out->numsamples*4));
  c->consumed+=(int)out->numsamples;c->records++;msr3_free(&out);
  // An offset too near the end must be rejected before following the chain.
  unsigned char saved44=r[44],saved45=r[45];r[44]=1;r[45]=255;
  CHECK(msflac_unpack_record(raw,length,&out)<0);r[44]=saved44;r[45]=saved45;
}
static void write_record(char *raw,int length,void *file){CHECK(fwrite(raw,1,(size_t)length,file)==(size_t)length);}
int main(int argc,char **argv){
  int32_t data[4000];uint32_t state=123;
  for(int i=0;i<4000;i++){state=state*1664525u+1013904223u;data[i]=(int32_t)(state&0xffffffu)-8388608;}
  MS3Record r=MS3Record_INITIALIZER;strcpy(r.sid,"FDSN:TA_G05D__B_H_Z");
  r.starttime=ms_timestr2nstime("2016-02-16T23:06:48.274998Z");
  r.samprate=40;r.formatversion=2;r.reclen=512;r.encoding=DE_FLAC_EXPERIMENTAL;r.sampletype='i';
  r.datasamples=data;r.numsamples=r.samplecnt=4000;
  const char *item="{\"Type\":\"STEP\",\"BeginTime\":\"2016-02-16T23:00:00Z\",\"Steps\":1,\"Amplitude\":10,\"InputChannel\":\"BHZ\"}";
  char extra[2048];snprintf(extra,sizeof(extra),"{\"FDSN\":{\"Calibration\":{\"Sequence\":[%s,%s,%s]}}}",item,item,item);
  r.extra=extra;r.extralength=(uint16_t)strlen(extra);
  Context c={data,0,0};int64_t packed;
  CHECK(msr3_pack(&r,verify,&c,&packed,MSF_FLUSHDATA,0)>0);
  CHECK(packed==4000&&c.consumed==4000);
  if(argc==2){
    FILE *file=fopen(argv[1],"wbx");CHECK(file);MS3Record source=r;source.encoding=DE_STEIM2;
    CHECK(msr3_pack(&source,write_record,file,&packed,MSF_FLUSHDATA,0)>0&&packed==4000);
    CHECK(!fclose(file));
  }
  // Eight 60-byte calibration blockettes exceed the record header space.
  snprintf(extra,sizeof(extra),"{\"FDSN\":{\"Calibration\":{\"Sequence\":[%s,%s,%s,%s,%s,%s,%s,%s]}}}",item,item,item,item,item,item,item,item);
  r.extralength=(uint16_t)strlen(extra);char raw[512];
  CHECK(msflac_pack_record(&r,data,1,raw,512)<0);
  printf("PASS: 244-byte headers, 3 calibration blockettes, 4000 exact noisy int32 samples in %d fixed 512-byte records; invalid offsets and oversized headers rejected\n",c.records);
  return 0;
}
