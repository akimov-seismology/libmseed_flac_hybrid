#include "mseed_flac.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint32_t crc(const unsigned char *r){uint32_t c=~0u;for(int i=0;i<512;i++){c^=i<508?r[i]:0;for(int j=0;j<8;j++)c=(c>>1)^((c&1)?0xedb88320u:0);}return ~c;}
int main(int argc,char **argv){
 if(argc!=2)return 2;unsigned char original[512],r[512];FILE *f=fopen(argv[1],"rb");if(!f || fread(original,1,512,f)!=512)return 2;fclose(f);
 uint32_t state=77;int accepted=0;
 for(int n=0;n<3000;n++){
  memcpy(r,original,512);
  for(int j=0;j<1+n%3;j++){state=1664525u*state+1013904223u;unsigned pos=state%508;state=1664525u*state+1013904223u;r[pos]^=(unsigned char)(1u<<(state%8));}
  uint32_t c=crc(r);for(int j=0;j<4;j++)r[508+j]=(unsigned char)(c>>(24-8*j));
  MS3Record *out=NULL;accepted+=msflac_unpack_record((char*)r,512,&out)==0;msr3_free(&out);
 }
 printf("PASS 3000 deterministic mutated records; %d structurally valid mutations accepted\n",accepted);return 0;
}
