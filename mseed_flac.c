/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2026 Andrei Akimov, for original project contributions.
 * Developed with assistance from OpenAI ChatGPT/Codex; see AUTHORS.md. */
#include "mseed_flac.h"
#include "libmseed/unpack.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern int flac_encode_frame(const int32_t *,int,int,int,int,unsigned char *,int);
extern int flac_decode_frame(const unsigned char *,int,int32_t *,int,int,int *);
static unsigned get16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static uint32_t get32(const unsigned char *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static void put32(unsigned char *p,uint32_t v){p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v;}
/* CRC-32 IEEE with checksum bytes logically zero; no zlib dependency. */
static uint32_t crc_record(const unsigned char *p,uint32_t n){
    static const uint32_t table[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu
    };
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<n-4;i++){
        crc^=p[i];
        crc=(crc>>4)^table[crc&15];
        crc=(crc>>4)^table[crc&15];
    }
    for(unsigned i=0;i<4;i++){
        crc=(crc>>4)^table[crc&15];
        crc=(crc>>4)^table[crc&15];
    }
    return ~crc;
}
static int valid_length(uint32_t n){return n>=512 && n<=1048576 && !(n&(n-1));}
static int validate_chain(const unsigned char *r,unsigned offset,unsigned *b1000){
    unsigned position=get16(r+46),seen=0;*b1000=0;
    while(position){
        if(position<48 || position+4>offset || ++seen>r[39])return -1;
        unsigned type=get16(r+position),next=get16(r+position+2);
        /* Check the entire blockette, not just its four-byte chain header.
         * libmseed tolerates some overlaps with warnings; this profile must
         * reject metadata that extends into the FLAC payload. */
        if(type==2000 && position+6>offset)return -1;
        unsigned length=ms2_blktlen((uint16_t)type,(const char *)r+position,
                                   (int8_t)!ms_bigendianhost());
        if(length<4 || position+length>offset)return -1;
        if(type==1000){if(*b1000)return -1;*b1000=position;}
        if(next && next<position+length)return -1;
        position=next;
    }
    return seen==r[39] && *b1000?0:-1;
}
int msflac_pack_record(const MS3Record *metadata,const int32_t *samples,int count,char *output,uint32_t reclen){
    if(!metadata || !samples || !output || count<1 || !valid_length(reclen)){ms_log(2,"FLAC21: Invalid input pointers/count/record length\n");return -1;}
    double rate=msr3_sampratehz(metadata);
    if(!isfinite(rate) || rate<1 || rate>655350 || rate!=floor(rate)){ms_log(2,"%s: FLAC21 requires integer sample rate 1..655350 Hz, got %.17g\n",metadata->sid,rate);return -1;}
    if(count>16384)count=16384;
    MS3Record header=*metadata;header.encoding=DE_FLAC_EXPERIMENTAL;
    header.formatversion=2;header.reclen=(int32_t)reclen;header.samplecnt=count;
    memset(output,0,reclen);
    int offset=msr3_pack_header2(&header,output,reclen,0);
    if(offset<48 || offset>UINT16_MAX || (uint32_t)offset+4+16>reclen){ms_log(2,"%s: FLAC21 header cannot be used: offset=%d bytes, record=%u bytes; need a 16-bit offset and space for FLAC plus trailer\n",metadata->sid,offset,reclen);return -1;}
    unsigned char *r=(unsigned char *)output;
    put16(r+44,(unsigned)offset);
    unsigned b1000;
    if(validate_chain(r,(unsigned)offset,&b1000)){ms_log(2,"%s: FLAC21 invalid generated blockette chain\n",metadata->sid);return -1;}
    r[b1000+5]=1;
    int capacity=count*4+65536,available=(int)reclen-offset-4;
    unsigned char *packed=libmseed_memory.malloc((size_t)capacity);
    int32_t *checked=libmseed_memory.malloc((size_t)count*sizeof(int32_t));
    if(!packed || !checked){ms_log(2,"%s: FLAC21 cannot allocate codec buffers\n",metadata->sid);libmseed_memory.free(packed);libmseed_memory.free(checked);return -1;}
    int limit=count,upper=count,size;
    /* A local probe must not re-encode a prefix already rejected by this
     * record's search. No cache/state is shared between channels or threads. */
    unsigned char rejected[2049]={0};
    while(1){
        int order=count-1;if(order>32)order=32;
        size=flac_encode_frame(samples,count,count<32?32:count,order,(int)rate,packed,capacity);
        if(size<0){ms_log(2,"%s: FLAC encoder failed: code=%d samples=%d LPC=%d rate=%.17g capacity=%d\n",metadata->sid,size,count,order,rate,capacity);goto fail;}
        if(size<=available)break;
        rejected[(unsigned)count>>3]|=(unsigned char)(1u<<(count&7));
        if(count==1){ms_log(2,"%s: FLAC payload cannot fit one sample: bytes=%d available=%d\n",metadata->sid,size,available);goto fail;}
        upper=count-1;
        int smaller=(int)((double)count*available/size*.95);
        if(smaller>=count)smaller=count-1;
        if(smaller<1)smaller=1;
        count=smaller;
    }
    /* Keep the first fitting encoding while testing larger prefixes. FLAC
     * size is not strictly monotone (predictor choices can change), so this
     * bounded search improves utilization without claiming a global optimum.
     * Every accepted candidate is actually encoded and checked for fit. */
    if(count<upper){
        unsigned char *trial=libmseed_memory.malloc((size_t)capacity);
        if(!trial){ms_log(2,"%s: FLAC21 cannot allocate fitting buffer\n",metadata->sid);goto fail;}
        int lo=count+1,hi=upper;
        while(lo<=hi){
            int candidate=lo+(hi-lo)/2;
            int order=candidate-1;if(order>32)order=32;
            int bytes=flac_encode_frame(samples,candidate,candidate<32?32:candidate,order,(int)rate,trial,capacity);
            if(bytes<0){libmseed_memory.free(trial);goto fail;}
            if(bytes<=available){memcpy(packed,trial,(size_t)bytes);count=candidate;size=bytes;lo=candidate+1;}
            else {rejected[(unsigned)candidate>>3]|=(unsigned char)(1u<<(candidate&7));hi=candidate-1;}
        }
        /* Probe the next eight counts even after a failure: small local size
         * reversals must not make us discard a larger fitting prefix. */
        int end=count+8;if(end>limit)end=limit;
        for(int candidate=count+1;candidate<=end;candidate++){
            if(rejected[(unsigned)candidate>>3]&(1u<<(candidate&7)))continue;
            int order=candidate-1;if(order>32)order=32;
            int bytes=flac_encode_frame(samples,candidate,candidate<32?32:candidate,order,(int)rate,trial,capacity);
            if(bytes<0){libmseed_memory.free(trial);goto fail;}
            if(bytes<=available){memcpy(packed,trial,(size_t)bytes);count=candidate;size=bytes;}
        }
        libmseed_memory.free(trial);
    }
    int consumed=0;
    int decoded=flac_decode_frame(packed,size,checked,count,(int)rate,&consumed);
    if(decoded!=count || consumed!=size || memcmp(samples,checked,(size_t)count*4)){ms_log(2,"%s: FLAC self-check failed: decoded=%d expected=%d\n",metadata->sid,decoded,count);goto fail;}
    put16(r+30,(unsigned)count);
    memcpy(r+offset,packed,(size_t)size);
    /* Compact profile: frame sync at data offset, zero padding, CRC only. */
    put32(r+reclen-4,crc_record(r,reclen));
    libmseed_memory.free(packed);libmseed_memory.free(checked);return count;
fail:libmseed_memory.free(packed);libmseed_memory.free(checked);return -1;
}
int msflac_unpack_record(const char *record,uint32_t reclen,MS3Record **decoded){
    if(!record || !decoded || !valid_length(reclen))return -1;
    const unsigned char *r=(const unsigned char *)record;
    unsigned offset=get16(r+44),b1000;
    if(offset<48 || offset>reclen-4-16 || validate_chain(r,offset,&b1000))return -1;
    if(r[b1000+4]!=DE_FLAC_EXPERIMENTAL || r[b1000+5]!=1 || r[b1000+6]>20 ||
       reclen!=(1u<<r[b1000+6]))return -1;
    if(get32(r+reclen-4)!=crc_record(r,reclen))return -1;
    unsigned count=get16(r+30),length=0;
    if(!count)return -1;
    const unsigned char *p=r+offset;
    if(p[0]!=255 || (p[1]&254)!=248)return -1;
    MS3Record *msr=NULL;
    if(msr3_parse(record,reclen,&msr,0,0)!=MS_NOERROR){msr3_free(&msr);return -1;}
    double rate=msr3_sampratehz(msr);
    if(!isfinite(rate) || rate<1 || rate>655350 || rate!=floor(rate))goto fail;
    msr->datasamples=libmseed_memory.malloc((size_t)count*sizeof(int32_t));
    if(!msr->datasamples)goto fail;
    int used=0;
    if(flac_decode_frame(p,(int)(reclen-offset-4),msr->datasamples,(int)count,(int)rate,&used)!=(int)count)goto fail;
    length=(unsigned)used;
    for(unsigned i=offset+length;i<reclen-4;i++)if(r[i])goto fail;
    msr->datasize=(uint64_t)count*4;msr->numsamples=count;msr->sampletype='i';
    msr->datalength=length;
    if(*decoded)msr3_free(decoded);
    *decoded=msr;return 0;
fail:msr3_free(&msr);return -1;
}

