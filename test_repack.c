/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#include "libmseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const char *sids[]={"FDSN:IU_TEST_00_B_H_Z","FDSN:IU_TEST_00_B_H_N","FDSN:IU_TEST_00_B_H_E"};
static void write_record(char *r,int n,void *p){CHECK(fwrite(r,1,(size_t)n,p)==(size_t)n);}
int main(int argc,char **argv){
  if(argc!=3)return 2;
  int continuous=strstr(argv[1],"continuous")!=NULL;
  if(!strncmp(argv[1],"generate",8)){
    FILE *f=fopen(argv[2],"wb");CHECK(f);
    nstime_t next[3];for(int ch=0;ch<3;ch++)next[ch]=ms_timestr2nstime("2024-01-02T00:00:00.019538Z");
    for(int i=0;i<400;i++)for(int ch=0;ch<3;ch++){
      int32_t data[101];for(int j=0;j<101;j++)data[j]=ch*100000+i*101+j;
      if(!continuous&&i==100)next[ch]+=2000000000LL;
      if(!continuous&&i==200)next[ch]-=1000000000LL;
      MS3Record r=MS3Record_INITIALIZER;strcpy(r.sid,sids[ch]);r.starttime=next[ch];
      r.samprate=strstr(argv[1],"fractional")?40.5:(!continuous&&i>=350?80:40);r.flags=!continuous&&i>=250?2:0;r.encoding=11;r.formatversion=2;r.reclen=512;
      const char *extra=!continuous&&i>=300?"{\"FDSN\":{\"Time\":{\"Quality\":89}}}":"{\"FDSN\":{\"Time\":{\"Quality\":100}}}";
      r.extra=(char *)extra;r.extralength=(uint16_t)strlen(extra);
      r.datasamples=data;r.numsamples=r.samplecnt=101;r.sampletype='i';int64_t packed;
      CHECK(msr3_pack(&r,write_record,f,&packed,MSF_FLUSHDATA,0)==1&&packed==101);
      next[ch]=ms_sampletime(next[ch],101,r.samprate);
    }
    CHECK(!fclose(f));return 0;
  }
  MS3Record *r=NULL;MS3FileParam *reader=NULL;int rv;int64_t counts[3]={0};int records=0,joined=0;
  const int boundaries[]={10100,20200,25250,30300,35350,40400};
  while((rv=ms3_readmsr_r(&reader,&r,argv[2],MSF_UNPACKDATA,0))==MS_NOERROR){
    CHECK(r->reclen==512);int ch;for(ch=0;ch<3;ch++)if(!strcmp(r->sid,sids[ch]))break;CHECK(ch<3);
    int64_t end=counts[ch]+r->numsamples;
    if(!continuous)for(int k=0;k<6;k++)CHECK(!(counts[ch]<boundaries[k]&&end>boundaries[k]));
    int i=(int)(counts[ch]/101);
    CHECK(r->samprate==(!continuous&&i>=350?80:40));CHECK(r->flags==(!continuous&&i>=250?2:0));
    CHECK(r->extra&&strstr(r->extra,!continuous&&i>=300?"89":"100"));
    for(int64_t j=0;j<r->numsamples;j++)CHECK(((int32_t *)r->datasamples)[j]==ch*100000+counts[ch]+j);
    counts[ch]=end;records++;if(r->numsamples>101)joined=1;
  }
  CHECK(rv==MS_ENDOFFILE);for(int ch=0;ch<3;ch++)CHECK(counts[ch]==40400);
  CHECK(records<1200);if(continuous)CHECK(joined);ms3_readmsr_r(&reader,&r,NULL,0,0);
  printf("PASS buffered repack: 3 interleaved channels; 121200 exact samples; gap/overlap/flags/quality/rate boundaries or continuous-stream case; %d fixed 512-byte records vs 1200 input\n",records);
  return 0;
}
