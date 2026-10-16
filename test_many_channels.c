/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#include "libmseed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static void write_record(char *r,int n,void *p){CHECK(fwrite(r,1,(size_t)n,p)==(size_t)n);}
int main(int argc,char **argv){
  if(argc!=4)return 2;
  int count=atoi(argv[2]);CHECK(count>0&&count<=9999);
  int reverse=strcmp(argv[3],"original")!=0,changed=!strcmp(argv[3],"changed");
  FILE *file=fopen(argv[1],"wbx");CHECK(file);
  nstime_t start=ms_timestr2nstime("2024-01-02T00:00:00.019538Z");
  for(int round=0;round<2;round++)for(int i=0;i<count;i++){
    int ch=reverse?count-i-1:i;
    int32_t data[7];for(int j=0;j<7;j++)data[j]=ch*1000+round*7+j;
    if(changed&&ch==count-1&&round==1)data[3]++;
    MS3Record r=MS3Record_INITIALIZER;snprintf(r.sid,sizeof(r.sid),"FDSN:XX_S%04d__B_H_Z",ch);
    r.starttime=ms_sampletime(start,round*7,40);r.samprate=40;
    r.formatversion=2;r.reclen=512;r.encoding=reverse?DE_FLAC_EXPERIMENTAL:DE_INT32;
    r.sampletype='i';r.datasamples=data;r.numsamples=r.samplecnt=7;int64_t packed;
    CHECK(msr3_pack(&r,write_record,file,&packed,MSF_FLUSHDATA,0)==1&&packed==7);
  }
  CHECK(!fclose(file));return 0;
}
