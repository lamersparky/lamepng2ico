/*
 * lamepng2ico - freestanding Linux x86-64 PNG -> multi-resolution ICO converter
 * Copyright (c) 2026. Public-domain style: use, modify, and redistribute freely.
 *
 * Runtime: Linux x86-64 kernel syscalls only. No libc, no shared objects, no
 * third-party libraries. Initial format scope: non-interlaced, 8-bit PNGs with
 * color types 0, 2, 3, 4, and 6; PNG CRCs are currently not validated.
 */
#if !defined(__x86_64__)
#error "This freestanding build currently targets Linux x86-64 only."
#endif
#include <stdint.h>
#include <stddef.h>

#define SYS_read       0
#define SYS_write      1
#define SYS_openat     257
#define SYS_mkdir       83
#define SYS_close      3
#define SYS_mmap       9
#define SYS_exit       60
#define AT_FDCWD       -100
#define O_RDONLY       0
#define O_WRONLY      1
#define O_CREAT       64
#define O_TRUNC       512
#define PROT_READ      1
#define PROT_WRITE     2
#define MAP_PRIVATE    2
#define MAP_ANONYMOUS  32
#define MAP_FAILED     ((void*)-1)
#define MAX_INPUT      (128UL * 1024UL * 1024UL)
#define MAX_COMPRESSED (128UL * 1024UL * 1024UL)
#define MAX_RAW        (96UL * 1024UL * 1024UL)
#define MAX_PIXELS     (4096UL * 4096UL)
#define MAX_ICON       (2UL * 1024UL * 1024UL)

static long syscall1(long n,long a){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a):"rcx","r11","memory"); return r; }
static long syscall3(long n,long a,long b,long c){ long r; __asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b),"d"(c):"rcx","r11","memory"); return r; }
static long syscall4(long n,long a,long b,long c,long d){ register long r10 __asm__("r10")=d; long rax; __asm__ volatile("syscall":"=a"(rax):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10):"rcx","r11","memory"); return rax; }
static long syscall6(long n,long a,long b,long c,long d,long e,long f){ register long r10 __asm__("r10")=d; register long r8 __asm__("r8")=e; register long r9 __asm__("r9")=f; long rax; __asm__ volatile("syscall":"=a"(rax):"a"(n),"D"(a),"S"(b),"d"(c),"r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory"); return rax; }
static void *mapmem(unsigned long n){ long r=syscall6(SYS_mmap,0,(long)n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0); return r<0?MAP_FAILED:(void*)r; }
static void msg(const char *s){ unsigned long n=0; while(s[n])n++; syscall3(SYS_write,2,(long)s,n); }
static void putstr(int fd,const char *s){ unsigned long n=0; while(s[n])n++; syscall3(SYS_write,fd,(long)s,n); }
static int same(const char*a,const char*b){while(*a&&*b&&*a==*b){a++;b++;}return *a==*b;}
static unsigned long slen(const char*s){unsigned long n=0;while(s[n])n++;return n;}
static void bytes_copy(uint8_t*d,const uint8_t*s,unsigned long n){for(unsigned long i=0;i<n;i++)d[i]=s[i];}
static void zero_bytes(uint8_t*d,unsigned long n){for(unsigned long i=0;i<n;i++)d[i]=0;}
static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint32_t le32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t le16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static void wr16(uint8_t*p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t*p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void wrbe32(uint8_t*p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}

/* Linux process entry: call app_main(argc, argv), then exit via syscall. */
__attribute__((naked,noreturn)) void _start(void){
  __asm__ volatile("xor %rbp,%rbp\n\t mov (%rsp),%rdi\n\t lea 8(%rsp),%rsi\n\t and $-16,%rsp\n\t call app_main\n\t mov %eax,%edi\n\t mov $60,%eax\n\t syscall\n\t hlt\n\t");
}

struct Bits { const uint8_t *p; unsigned long n, bit; int bad; };
static unsigned getbits(struct Bits*b,unsigned n){unsigned v=0;for(unsigned i=0;i<n;i++){if(b->bit>=b->n*8){b->bad=1;return 0;}v|=((b->p[b->bit>>3]>>(b->bit&7))&1U)<<i;b->bit++;}return v;}
struct Huff { unsigned short count[16]; unsigned short symbol[288]; unsigned max; };
static int huff_build(struct Huff*h,const uint8_t*l,unsigned n){
  zero_bytes((uint8_t*)h,sizeof(*h));
  for(unsigned i=0;i<n;i++){if(l[i]>15)return 0;h->count[l[i]]++;if(l[i]>h->max)h->max=l[i];}
  if(h->count[0]==n)return 0;
  int left=1;for(unsigned len=1;len<=15;len++){left<<=1;left-=h->count[len];if(left<0)return 0;}
  unsigned short offs[16];offs[1]=0;for(unsigned len=1;len<15;len++)offs[len+1]=offs[len]+h->count[len];
  for(unsigned sym=0;sym<n;sym++)if(l[sym])h->symbol[offs[l[sym]]++]=(unsigned short)sym;
  return 1;
}
static int huff_decode(struct Bits*b,const struct Huff*h){
  unsigned code=0,first=0,index=0;
  for(unsigned len=1;len<=h->max;len++){
    code=(code<<1)|getbits(b,1);if(b->bad)return -1;
    unsigned count=h->count[len];
    if(code>=first && code-first<count)return h->symbol[index+(code-first)];
    index+=count;first=(first+count)<<1;
  }
  b->bad=1;return -1;
}
static const unsigned short lbase[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const uint8_t lextra[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const unsigned short dbase[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const uint8_t dextra[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
static int inflate_zlib(const uint8_t*src,unsigned long srclen,uint8_t*out,unsigned long outcap,unsigned long*outlen){
  if(srclen<6)return 0;
  if((src[0]&15)!=8 || (((unsigned)src[0]<<8)|src[1])%31!=0 || (src[1]&32))return 0;
  struct Bits b={src+2,srclen-6,0,0};unsigned long op=0;int final=0;
  while(!final&&!b.bad){final=getbits(&b,1);unsigned type=getbits(&b,2);if(b.bad)return 0;
    if(type==0){b.bit=(b.bit+7)&~7UL;unsigned len=getbits(&b,16),nlen=getbits(&b,16);if(b.bad||((len^0xffffU)&0xffffU)!=nlen||op+len>outcap)return 0;for(unsigned i=0;i<len;i++)out[op++]=(uint8_t)getbits(&b,8);if(b.bad)return 0;continue;}
    if(type==3)return 0;
    struct Huff lit,dist;uint8_t ll[288],dd[32];
    if(type==1){for(unsigned i=0;i<=143;i++)ll[i]=8;for(unsigned i=144;i<=255;i++)ll[i]=9;for(unsigned i=256;i<=279;i++)ll[i]=7;for(unsigned i=280;i<=287;i++)ll[i]=8;for(unsigned i=0;i<32;i++)dd[i]=5;
      if(!huff_build(&lit,ll,288)||!huff_build(&dist,dd,32))return 0;
    }else{
      unsigned nl=getbits(&b,5)+257,nd=getbits(&b,5)+1,nc=getbits(&b,4)+4;if(b.bad||nl>286||nd>32)return 0;
      static const uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};uint8_t cl[19];zero_bytes(cl,19);
      for(unsigned i=0;i<nc;i++)cl[order[i]]=(uint8_t)getbits(&b,3);struct Huff ch;if(b.bad||!huff_build(&ch,cl,19))return 0;
      uint8_t all[318];unsigned total=nl+nd,pos=0;
      while(pos<total){int sym=huff_decode(&b,&ch);if(sym<0)return 0;if(sym<=15)all[pos++]=(uint8_t)sym;else if(sym==16){if(pos==0)return 0;unsigned rep=getbits(&b,2)+3;if(pos+rep>total)return 0;uint8_t v=all[pos-1];while(rep--)all[pos++]=v;}else if(sym==17){unsigned rep=getbits(&b,3)+3;if(pos+rep>total)return 0;while(rep--)all[pos++]=0;}else if(sym==18){unsigned rep=getbits(&b,7)+11;if(pos+rep>total)return 0;while(rep--)all[pos++]=0;}else return 0;if(b.bad)return 0;}
      for(unsigned i=0;i<nl;i++)ll[i]=all[i];for(unsigned i=0;i<nd;i++)dd[i]=all[nl+i];if(!huff_build(&lit,ll,nl)||!huff_build(&dist,dd,nd))return 0;
    }
    for(;;){int s=huff_decode(&b,&lit);if(s<0)return 0;if(s<256){if(op>=outcap)return 0;out[op++]=(uint8_t)s;continue;}if(s==256)break;if(s<257||s>285)return 0;unsigned ix=(unsigned)s-257;unsigned length=lbase[ix]+getbits(&b,lextra[ix]);int ds=huff_decode(&b,&dist);if(ds<0||ds>=30)return 0;unsigned d=dbase[ds]+getbits(&b,dextra[ds]);if(b.bad||d==0||d>op||op+length>outcap)return 0;while(length--){out[op]=out[op-d];op++;}}
  }
  if(b.bad)return 0;*outlen=op;return 1;
}

static uint8_t *filebuf,*compbuf,*rawbuf,*rgba,*icobuf;
static unsigned long filelen,complen,rawcap,rawlen;
static uint32_t iw,ih;static uint8_t ctype,depth,palette[768],palalpha[256],paln;static unsigned long palalen;
static int png_read(void){
  static const uint8_t sig[8]={137,80,78,71,13,10,26,10};if(filelen<33)return 0;for(int i=0;i<8;i++)if(filebuf[i]!=sig[i])return 0;
  unsigned long p=8;int gotih=0,gotend=0;paln=0;palalen=0;complen=0;for(int i=0;i<256;i++)palalpha[i]=255;
  while(p+12<=filelen){uint32_t n=be32(filebuf+p);const uint8_t*t=filebuf+p+4;const uint8_t*d=filebuf+p+8;if((unsigned long)n>filelen-p-12)return 0;
    if(t[0]=='I'&&t[1]=='H'&&t[2]=='D'&&t[3]=='R'){if(n!=13)return 0;iw=be32(d);ih=be32(d+4);depth=d[8];ctype=d[9];if(!iw||!ih||iw>4096||ih>4096||depth!=8||d[10]!=0||d[11]!=0||d[12]!=0)return 0;gotih=1;}
    else if(t[0]=='P'&&t[1]=='L'&&t[2]=='T'&&t[3]=='E'){if(n>768||n%3)return 0;bytes_copy(palette,d,n);paln=(uint8_t)(n/3);}
    else if(t[0]=='t'&&t[1]=='R'&&t[2]=='N'&&t[3]=='S'){if(n>256)return 0;for(unsigned i=0;i<n;i++)palalpha[i]=d[i];palalen=n;}
    else if(t[0]=='I'&&t[1]=='D'&&t[2]=='A'&&t[3]=='T'){if(complen+n>MAX_COMPRESSED)return 0;bytes_copy(compbuf+complen,d,n);complen+=n;}
    else if(t[0]=='I'&&t[1]=='E'&&t[2]=='N'&&t[3]=='D'){gotend=1;break;}
    p+=(unsigned long)n+12;
  }
  if(!gotih||!gotend||!complen)return 0;if(ctype!=0&&ctype!=2&&ctype!=3&&ctype!=4&&ctype!=6)return 0;if(ctype==3&&!paln)return 0;
  unsigned channels=ctype==0?1:ctype==2?3:ctype==3?1:ctype==4?2:4;
  unsigned long row=(unsigned long)iw*channels;if((row+1)*ih>MAX_RAW||((unsigned long)iw*ih)>MAX_PIXELS)return 0;rawcap=(row+1)*ih;
  if(!inflate_zlib(compbuf,complen,rawbuf,rawcap,&rawlen)||rawlen!=rawcap)return 0;
  for(unsigned long y=0;y<ih;y++){uint8_t*r=rawbuf+y*(row+1);unsigned f=r[0];uint8_t*q=r+1;uint8_t*prev=y?rawbuf+(y-1)*(row+1)+1:0;if(f>4)return 0;
    for(unsigned long x=0;x<row;x++){unsigned a=x>=channels?q[x-channels]:0,b=prev?prev[x]:0,c=(prev&&x>=channels)?prev[x-channels]:0;unsigned v=q[x];
      if(f==1)v+=a;else if(f==2)v+=b;else if(f==3)v+=(a+b)/2;else if(f==4){int pp=(int)a+(int)b-(int)c,pa=pp-(int)a,pb=pp-(int)b,pc=pp-(int)c;if(pa<0)pa=-pa;if(pb<0)pb=-pb;if(pc<0)pc=-pc;v+=(pa<=pb&&pa<=pc)?a:(pb<=pc?b:c);}q[x]=(uint8_t)v;
    }
  }
  unsigned long pix=(unsigned long)iw*ih;for(unsigned long i=0;i<pix;i++){uint8_t *d=rgba+i*4,*s;unsigned char gray;switch(ctype){case 0:s=rawbuf+(i/iw)*(row+1)+1+(i%iw);gray=*s;d[0]=d[1]=d[2]=gray;d[3]=255;break;case 2:s=rawbuf+(i/iw)*(row+1)+1+(i%iw)*3;d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=255;break;case 3:s=rawbuf+(i/iw)*(row+1)+1+(i%iw);if(*s>=paln)return 0;d[0]=palette[*s*3];d[1]=palette[*s*3+1];d[2]=palette[*s*3+2];d[3]=*s<palalen?palalpha[*s]:255;break;case 4:s=rawbuf+(i/iw)*(row+1)+1+(i%iw)*2;gray=s[0];d[0]=d[1]=d[2]=gray;d[3]=s[1];break;default:s=rawbuf+(i/iw)*(row+1)+1+(i%iw)*4;d[0]=s[0];d[1]=s[1];d[2]=s[2];d[3]=s[3];break;}}
  return 1;
}
static int bmp_read(void){
  if(filelen<54||filebuf[0]!='B'||filebuf[1]!='M')return 0;
  uint32_t off=le32(filebuf+10),dib=le32(filebuf+14);if(dib<40||14UL+dib>filelen)return 0;
  int32_t sw=(int32_t)le32(filebuf+18),sh=(int32_t)le32(filebuf+22);uint16_t planes=le16(filebuf+26),bits=le16(filebuf+28);uint32_t compression=le32(filebuf+30);
  if(sw<=0||sh==0||sh==(-2147483647-1)||planes!=1||(bits!=24&&bits!=32)||compression!=0)return 0;
  uint32_t w=(uint32_t)sw,h=(uint32_t)(sh<0?-sh:sh);if(w>4096||h>4096||!w||!h||(unsigned long)w*h>MAX_PIXELS)return 0;
  unsigned long stride=(((unsigned long)w*bits+31)/32)*4;if(off<14UL+dib||off>filelen||stride*h>filelen-off)return 0;
  iw=w;ih=h;int topdown=sh<0;int has_alpha=bits==32,any_alpha=0;
  for(unsigned y=0;y<h;y++){unsigned sy=topdown?y:h-1-y;const uint8_t*r=filebuf+off+(unsigned long)sy*stride;for(unsigned x=0;x<w;x++){const uint8_t*s=r+(unsigned long)x*(bits/8);uint8_t*d=rgba+(((unsigned long)y*w+x)*4);d[2]=s[0];d[1]=s[1];d[0]=s[2];d[3]=bits==32?s[3]:255;if(d[3])any_alpha=1;}}
  /* BI_RGB 32-bit BMPs often leave the alpha byte unused and zero-filled. */
  if(has_alpha&&!any_alpha)for(unsigned long i=0;i<(unsigned long)w*h;i++)rgba[i*4+3]=255;
  ctype=6;depth=8;return 1;
}
static unsigned char *pixel_at(unsigned x,unsigned y){return rgba+(((unsigned long)y*iw+x)*4);}
static void sample(unsigned n,unsigned x,unsigned y,unsigned dw,unsigned dh,uint8_t*out){
  /* Bilinear interpolation at pixel centers; alpha-weight RGB to avoid dark fringes. */
  uint64_t fx=(((uint64_t)(2*x+1)*iw)<<15)/dw-32768,fy=(((uint64_t)(2*y+1)*ih)<<15)/dh-32768;
  long x0=(long)(fx>>16),y0=(long)(fy>>16);unsigned wx=(unsigned)(fx&65535),wy=(unsigned)(fy&65535);if((long)fx<0){x0=0;wx=0;}if((long)fy<0){y0=0;wy=0;}long x1=x0+1,y1=y0+1;if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=(long)iw)x1=iw-1;if(y1>=(long)ih)y1=ih-1;if(x0>=(long)iw)x0=iw-1;if(y0>=(long)ih)y0=ih-1;
  uint8_t*p[4]={pixel_at((unsigned)x0,(unsigned)y0),pixel_at((unsigned)x1,(unsigned)y0),pixel_at((unsigned)x0,(unsigned)y1),pixel_at((unsigned)x1,(unsigned)y1)};uint64_t w[4]={(uint64_t)(65536-wx)*(65536-wy),(uint64_t)wx*(65536-wy),(uint64_t)(65536-wx)*wy,(uint64_t)wx*wy};uint64_t asum=0,cs[3]={0,0,0};
  for(int i=0;i<4;i++){uint64_t aw=(uint64_t)p[i][3]*w[i];asum+=aw;for(int c=0;c<3;c++)cs[c]+=(uint64_t)p[i][c]*aw;}
  uint64_t alpha=(asum+(1ULL<<31))>>32;if(alpha>255)alpha=255;out[3]=(uint8_t)alpha;if(asum){for(int c=0;c<3;c++){uint64_t v=(cs[c]+asum/2)/asum;out[c]=(uint8_t)(v>255?255:v);}}else out[0]=out[1]=out[2]=0;
  (void)n;
}

/* PNG encoder: RGBA, filter type 0, and valid zlib stored DEFLATE blocks. */
static uint32_t png_crc(const uint8_t *p,unsigned long n){
  uint32_t c=0xffffffffU;
  for(unsigned long i=0;i<n;i++){c^=p[i];for(int k=0;k<8;k++)c=(c>>1)^((c&1)?0xedb88320U:0);}
  return c^0xffffffffU;
}
static uint32_t adler32_sum(const uint8_t*p,unsigned long n){uint32_t a=1,b=0;for(unsigned long i=0;i<n;i++){a=(a+p[i])%65521U;b=(b+a)%65521U;}return (b<<16)|a;}
static unsigned long png_chunk(uint8_t*b,unsigned long pos,const char type[4],const uint8_t*data,unsigned long n){
  wrbe32(b+pos,(uint32_t)n);bytes_copy(b+pos+4,(const uint8_t*)type,4);if(n)bytes_copy(b+pos+8,data,n);wrbe32(b+pos+8+n,png_crc(b+pos+4,n+4));return pos+n+12;
}
static unsigned long encode_png(unsigned n,unsigned content_box){
  /* rawbuf stores filter bytes plus straight RGBA pixels for each scanline. */
  unsigned long row=(unsigned long)n*4+1,raw_n=row*n;
  if(raw_n>MAX_RAW||n>512)return 0;
  zero_bytes(rawbuf,raw_n);
  unsigned dw,dh;
  if(iw>=ih){dw=content_box;dh=(unsigned)(((uint64_t)ih*content_box+iw/2)/iw);}else{dh=content_box;dw=(unsigned)(((uint64_t)iw*content_box+ih/2)/ih);}
  if(!dw)dw=1;if(!dh)dh=1;if(dw>n)dw=n;if(dh>n)dh=n;
  unsigned offx=(n-dw)/2,offy=(n-dh)/2;
  for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
    uint8_t rgba_px[4];sample(0,x,y,dw,dh,rgba_px);
    unsigned long at=(unsigned long)(offy+y)*row+1+(unsigned long)(offx+x)*4;
    rawbuf[at]=rgba_px[0];rawbuf[at+1]=rgba_px[1];rawbuf[at+2]=rgba_px[2];rawbuf[at+3]=rgba_px[3];
  }
  /* PNG signature and IHDR. */
  static const uint8_t sig[8]={137,80,78,71,13,10,26,10};bytes_copy(icobuf,sig,8);
  uint8_t ihdr[13];wrbe32(ihdr,n);wrbe32(ihdr+4,n);ihdr[8]=8;ihdr[9]=6;ihdr[10]=ihdr[11]=ihdr[12]=0;
  unsigned long pos=png_chunk(icobuf,8,"IHDR",ihdr,13);
  unsigned long blocks=(raw_n+65534)/65535;
  unsigned long zlen=2+raw_n+blocks*5+4;
  if(pos+zlen+16>MAX_ICON)return 0;
  unsigned long chunk_pos=pos;wrbe32(icobuf+chunk_pos,(uint32_t)zlen);bytes_copy(icobuf+chunk_pos+4,(const uint8_t*)"IDAT",4);
  unsigned long zp=chunk_pos+8;icobuf[zp++]=0x78;icobuf[zp++]=0x01;
  unsigned long rp=0;
  while(rp<raw_n){unsigned long left=raw_n-rp;uint16_t blen=(uint16_t)(left>65535?65535:left);icobuf[zp++]=(rp+blen==raw_n)?1:0;icobuf[zp++]=(uint8_t)blen;icobuf[zp++]=(uint8_t)(blen>>8);uint16_t inv=(uint16_t)~blen;icobuf[zp++]=(uint8_t)inv;icobuf[zp++]=(uint8_t)(inv>>8);bytes_copy(icobuf+zp,rawbuf+rp,blen);zp+=blen;rp+=blen;}
  wrbe32(icobuf+zp,adler32_sum(rawbuf,raw_n));zp+=4;
  if(zp!=chunk_pos+8+zlen)return 0;
  wrbe32(icobuf+zp,png_crc(icobuf+chunk_pos+4,zlen+4));zp+=4;
  pos=zp;pos=png_chunk(icobuf,pos,"IEND",0,0);
  return pos;
}

static unsigned long make_ico(void){
  static const unsigned sizes[6]={16,32,48,64,128,256};zero_bytes(icobuf,MAX_ICON);wr16(icobuf,0);wr16(icobuf+2,1);wr16(icobuf+4,6);unsigned long pos=6+6*16;
  for(unsigned k=0;k<6;k++){unsigned n=sizes[k];unsigned long xorbytes=(unsigned long)n*n*4,maskstride=((n+31)/32)*4,maskbytes=maskstride*n,framebytes=40+xorbytes+maskbytes;uint8_t*e=icobuf+6+k*16; e[0]=n==256?0:(uint8_t)n;e[1]=n==256?0:(uint8_t)n;e[2]=0;e[3]=0;wr16(e+4,1);wr16(e+6,32);wr32(e+8,(uint32_t)framebytes);wr32(e+12,(uint32_t)pos);
    uint8_t*h=icobuf+pos;wr32(h,40);wr32(h+4,n);wr32(h+8,n*2);wr16(h+12,1);wr16(h+14,32);wr32(h+16,0);wr32(h+20,(uint32_t)xorbytes);wr32(h+24,0);wr32(h+28,0);wr32(h+32,0);wr32(h+36,0);
    uint8_t*xor=h+40;uint8_t*mask=xor+xorbytes;zero_bytes(mask,maskbytes);unsigned dw,dh;if(iw>=ih){dw=n;dh=(unsigned)(((uint64_t)ih*n+iw/2)/iw);}else{dh=n;dw=(unsigned)(((uint64_t)iw*n+ih/2)/ih);}if(!dw)dw=1;if(!dh)dh=1;unsigned offx=(n-dw)/2,offy=(n-dh)/2;
    for(unsigned yy=0;yy<dh;yy++){unsigned desty=n-1-(offy+yy);for(unsigned xx=0;xx<dw;xx++){uint8_t s[4];sample(n,xx,yy,dw,dh,s);uint8_t*d=xor+((unsigned long)desty*n+(offx+xx))*4;d[0]=s[2];d[1]=s[1];d[2]=s[0];d[3]=s[3];if(s[3]<128){unsigned my= n-1-(offy+yy);mask[(unsigned long)my*maskstride+((offx+xx)>>3)]|=(uint8_t)(0x80>>((offx+xx)&7));}}}
    pos+=framebytes;
  }return pos;
}
static int read_all(const char*path){long fd=syscall3(SYS_openat,AT_FDCWD,(long)path,O_RDONLY);if(fd<0)return 0;filebuf=mapmem(MAX_INPUT);if(filebuf==MAP_FAILED){syscall1(SYS_close,fd);return 0;}filelen=0;for(;;){if(filelen==MAX_INPUT){uint8_t extra;long z=syscall3(SYS_read,fd,(long)&extra,1);syscall1(SYS_close,fd);return z==0;}long z=syscall3(SYS_read,fd,(long)(filebuf+filelen),MAX_INPUT-filelen);if(z<0){syscall1(SYS_close,fd);return 0;}if(z==0)break;filelen+=(unsigned long)z;}syscall1(SYS_close,fd);return filelen>0;}
static int write_all(const char*path,unsigned long n){long fd=syscall4(SYS_openat,AT_FDCWD,(long)path,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)return 0;unsigned long p=0;while(p<n){long z=syscall3(SYS_write,fd,(long)(icobuf+p),n-p);if(z<=0){syscall1(SYS_close,fd);return 0;}p+=(unsigned long)z;}syscall1(SYS_close,fd);return 1;}

static long syscall2(long n,long a,long b){long r;__asm__ volatile("syscall":"=a"(r):"a"(n),"D"(a),"S"(b):"rcx","r11","memory");return r;}
static int mkdir_one(const char *path){long r=syscall2(83,(long)path,0755);return r==0||r==-17;}
static int mkdir_tree(const char *path){
  char tmp[4096];unsigned long n=slen(path);if(!n||n>=sizeof(tmp))return 0;bytes_copy((uint8_t*)tmp,(const uint8_t*)path,n+1);
  for(unsigned long i=1;i<n;i++)if(tmp[i]=='/'){char c=tmp[i];tmp[i]=0;if(i&& !mkdir_one(tmp))return 0;tmp[i]=c;}
  return mkdir_one(tmp);
}
static int path_join(char*out,const char*dir,const char*sub,const char*file){unsigned long p=0;const char*parts[3]={dir,sub,file};for(int i=0;i<3;i++){if(!parts[i]||!parts[i][0])continue;if(p&&out[p-1]!='/')out[p++]='/';unsigned long n=slen(parts[i]);if(p+n>=4095)return 0;bytes_copy((uint8_t*)out+p,(const uint8_t*)parts[i],n);p+=n;out[p]=0;}return 1;}
static int save_android_png(const char*dir,const char*sub,const char*filename,unsigned size,unsigned box){char path[4096];if(!path_join(path,dir,sub,filename))return 0;unsigned long n=encode_png(size,box);if(!n)return 0;return write_all(path,n);}
static int write_text(const char*path,const char*content){long fd=syscall4(SYS_openat,AT_FDCWD,(long)path,O_WRONLY|O_CREAT|O_TRUNC,0644);if(fd<0)return 0;unsigned long n=slen(content),p=0;while(p<n){long z=syscall3(SYS_write,fd,(long)(content+p),n-p);if(z<=0){syscall1(SYS_close,fd);return 0;}p+=(unsigned long)z;}syscall1(SYS_close,fd);return 1;}
static int make_android(const char*input,const char*dir){
  const char*dirs[]={"res/mipmap-mdpi","res/mipmap-hdpi","res/mipmap-xhdpi","res/mipmap-xxhdpi","res/mipmap-xxxhdpi","res/drawable-nodpi","res/values","res/mipmap-anydpi-v26"};
  for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){char p[4096];if(!path_join(p,dir,dirs[i],0)||!mkdir_tree(p))return 0;}
  const unsigned sizes[]={48,72,96,144,192};const char*folders[]={"res/mipmap-mdpi","res/mipmap-hdpi","res/mipmap-xhdpi","res/mipmap-xxhdpi","res/mipmap-xxxhdpi"};
  for(unsigned i=0;i<5;i++)if(!save_android_png(dir,folders[i],"ic_launcher.png",sizes[i],sizes[i]))return 0;
  if(!save_android_png(dir,"res/drawable-nodpi","ic_launcher_foreground.png",432,288))return 0;
  if(!save_android_png(dir,"","play_store_icon.png",512,512))return 0;
  char p[4096];
  if(!path_join(p,dir,"res/values","ic_launcher_background.xml")||!write_text(p,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n    <color name=\"ic_launcher_background\">#FFFFFFFF</color>\n</resources>\n"))return 0;
  if(!path_join(p,dir,"res/mipmap-anydpi-v26","ic_launcher.xml")||!write_text(p,"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n    <background android:drawable=\"@color/ic_launcher_background\" />\n    <foreground android:drawable=\"@drawable/ic_launcher_foreground\" />\n</adaptive-icon>\n"))return 0;
  putstr(1,"lamepng2ico: created Android launcher PNG resources in ");putstr(1,dir);putstr(1,"\n  density icons: 48, 72, 96, 144, 192 px\n  adaptive foreground: 432 px canvas (288 px safe content)\n  Play Store image: 512 x 512 px\n  Copy the generated res/ files into the Android app's res/ folder, then rebuild and sign the APK.\n");(void)input;return 1;
}
static void android_output_name(char*out,const char*in){unsigned long n=slen(in),slash=0,dot=n;for(unsigned long i=0;i<n;i++){if(in[i]=='/')slash=i+1;else if(in[i]=='.')dot=i;}if(dot<slash)dot=n;unsigned long j=0;for(;j<dot&&j<4000;j++)out[j]=in[j];const char*ext="-android";for(unsigned k=0;ext[k];k++)out[j++]=ext[k];out[j]=0;}

static int output_name(char*out,const char*in){unsigned long n=slen(in),slash=0,dot=n;for(unsigned long i=0;i<n;i++){if(in[i]=='/')slash=i+1;else if(in[i]=='.')dot=i;}if(dot<slash)dot=n;unsigned long j=0;for(;j<dot&&j<4000;j++)out[j]=in[j];const char*ext=".ico";for(unsigned k=0;ext[k];k++)out[j++]=ext[k];out[j]=0;return 1;}
int app_main(long argc,char**argv){
  if(argc==2&&(same(argv[1],"--help")||same(argv[1],"-h"))){putstr(1,"lamepng2ico 1.1 - dependency-free Linux image icon converter\nUsage:\n  lamepng2ico input.png|input.bmp [output.ico]\n  lamepng2ico --android input.png|input.bmp [output-directory]\n  lamepng2ico --help\n\nICO mode creates 16, 32, 48, 64, 128 and 256 px Windows icons.\nAndroid mode creates res/ launcher icon density buckets, adaptive icon XML/foreground, and a 512 px Play Store image.\nPNG: non-interlaced, 8-bit grayscale/RGB/indexed/gray-alpha/RGBA.\nBMP: uncompressed 24-bit or 32-bit BI_RGB. Other formats are not supported.\nRuntime uses Linux x86-64 syscalls only; no libc or shared libraries.\n");return 0;}
  if(argc==2&&same(argv[1],"--version")){putstr(1,"lamepng2ico 1.1 (freestanding Linux x86-64)\n");return 0;}
  int android=(argc>=2&&same(argv[1],"--android"));
  if(android?(argc<3||argc>4):(argc<2||argc>3)){msg("Usage: lamepng2ico input.png [output.ico]\n       lamepng2ico --android input.png [output-directory]\n");return 2;}
  const char*input=android?argv[2]:argv[1];
  if(!android&&argc==3&&same(argv[1],argv[2])){msg("lamepng2ico: input and output paths must differ\n");return 2;}
  char dirbuf[4096];const char*outdir=0;
  if(android){if(argc==4)outdir=argv[3];else{android_output_name(dirbuf,input);outdir=dirbuf;}if(same(input,outdir)){msg("lamepng2ico: input path cannot be the Android output directory\n");return 2;}}
  if(!read_all(input)){msg("lamepng2ico: cannot read input, or input exceeds 128 MiB\n");return 1;}
  compbuf=mapmem(MAX_COMPRESSED);rawbuf=mapmem(MAX_RAW);rgba=mapmem(MAX_PIXELS*4);icobuf=mapmem(MAX_ICON);if(compbuf==MAP_FAILED||rawbuf==MAP_FAILED||rgba==MAP_FAILED||icobuf==MAP_FAILED){msg("lamepng2ico: unable to allocate memory\n");return 1;}
  int ok=(filelen>=2&&filebuf[0]=='B'&&filebuf[1]=='M')?bmp_read():png_read();if(!ok){msg("lamepng2ico: unsupported, corrupt, or invalid image (see --help for supported formats)\n");return 1;}
  if(android){if(!make_android(input,outdir)){msg("lamepng2ico: failed while creating Android resources; check directory permissions and available disk space\n");return 1;}return 0;}
  char namebuf[4096];const char*out=argc==3?argv[2]:namebuf;if(argc!=3)output_name(namebuf,input);unsigned long n=make_ico();if(n>MAX_ICON||!write_all(out,n)){msg("lamepng2ico: could not write output ICO\n");return 1;}
  putstr(1,"lamepng2ico: created ");putstr(1,out);putstr(1," (6 resolutions: 16, 32, 48, 64, 128, 256 px)\n");return 0;
}
