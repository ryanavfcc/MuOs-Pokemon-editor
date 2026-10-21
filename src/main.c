
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#define SAVE_SIZE 131072
#define MAX_FILES 200
#define SECTION_SIZE 4096
static char files[MAX_FILES][512];
static int count=0, selected=0, pokemon=0, species=1, detail=0, status=0, bag_index=0, bag_qty=50, tab=0, box_num=0, box_slot=0, box_species=1;
static uint8_t data[SAVE_SIZE];
static int dirty=0, confirming=0, odds_index=0, egg_species=1, edit_field=0, folder_view=0, root_index=0;
static int catalog_game=0, delete_confirm=0, event_index=0, save_game=0, save_slot_valid[2]={0,0}, tool_cursor=0;
static char root_custom[512]="";
static const char *root_paths[]={"/run/muos/storage/save", "/run/muos/storage/roms", "/mnt/mmc/SAVES", "/mnt/sdcard/SAVES", "/mnt/mmc/ROMS", "/mnt/sdcard/ROMS", "/mnt/mmc/RetroArch/saves", "/mnt/sdcard/RetroArch/saves", "/mnt/mmc/MUOS/save", "/mnt/sdcard/MUOS/save", "/mnt/mmc", "/mnt/sdcard"};
#define ROOT_COUNT ((int)(sizeof(root_paths)/sizeof(root_paths[0])))
static char current_path[512]="/run/muos/storage/save";
static char dirs[128][512];static int ndirs=0, directory_cursor=0;
static const int egg_odds[]={8192,4096,2048,1024,512,128,16,1};
static const char *egg_odds_label[]={"1 IN 8192","1 IN 4096","1 IN 2048","1 IN 1024","1 IN 512","1 IN 128","1 IN 16","ALWAYS SHINY"};
static int active=-1, sec[14];
static const int lengths[14]={3884,3968,3968,3968,3848,3968,3968,3968,3968,3968,3968,3968,3968,2000};
static uint16_t rd16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static uint32_t rd32(const uint8_t *p){return (uint32_t)rd16(p)|((uint32_t)rd16(p+2)<<16);}
static void wr16(uint8_t *p,uint16_t v){p[0]=v&255;p[1]=v>>8;}
static void wr32(uint8_t *p,uint32_t v){wr16(p,v);wr16(p+2,v>>16);}
static uint16_t checksum(const uint8_t *p,int len){
 uint32_t sum=0;for(int i=0;i<len;i+=4)sum+=rd32(p+i);
 return (uint16_t)((sum&65535)+(sum>>16));
}
static int valid_slot(int slot,int *indexes,uint32_t *counter){
 int seen=0;uint32_t c=0;
 for(int i=0;i<14;i++){
  int ix=slot*14+i;uint8_t *p=data+ix*SECTION_SIZE;
  int id=rd16(p+0xff4);
  if(id>=14||(seen&(1<<id))||rd32(p+0xff8)!=0x08012025)return 0;
  if(checksum(p,lengths[id])!=rd16(p+0xff6))return 0;
  seen|=1<<id;indexes[id]=ix;
  if(i==0)c=rd32(p+0xffc);
  else if(rd32(p+0xffc)!=c)return 0;
 }
 *counter=c;return seen==0x3fff;
}
static int load(const char *path){
 /* Format detection is provisional (filename only). Never allow editing for
    unknown/FRLG/Emerald games until save structures are supported. */
 save_game=0;
 if(strcasestr(path,"firered")||strcasestr(path,"fire red"))save_game=2;
 else if(strcasestr(path,"leafgreen")||strcasestr(path,"leaf green"))save_game=3;
 else if(strcasestr(path,"emerald"))save_game=4;
 else if(strcasestr(path,"ruby"))save_game=1;
 else if(strcasestr(path,"sapphire"))save_game=1;
 struct stat st; if(stat(path,&st)||st.st_size!=SAVE_SIZE)return 0;
 FILE *f=fopen(path,"rb");if(!f)return 0;
 int ok=fread(data,1,SAVE_SIZE,f)==SAVE_SIZE;fclose(f);if(!ok)return 0;
 int a[14],b[14];uint32_t ca=0,cb=0;
 int va=valid_slot(0,a,&ca),vb=valid_slot(1,b,&cb);
 save_slot_valid[0]=va;save_slot_valid[1]=vb;
 if(!va&&!vb)return 0;
 active=vb&&(!va||(int32_t)(cb-ca)>0)?1:0;
 memcpy(sec,active?b:a,sizeof(sec));dirty=0;return 1;
}
static int party_count(void){
 int n=rd32(data+sec[1]*SECTION_SIZE+0x234);
 return n>=0&&n<=6?n:0;
}
static uint8_t *party(int i){
 return data+sec[1]*SECTION_SIZE+0x238+i*100;
}
static const int growth_index[24]={0,0,0,0,0,0,1,1,2,3,2,3,1,1,2,3,2,3,1,1,2,3,2,3};
/* Growth block offset determined by permutation PID % 24. */
static int growth_pos(int perm){
 static const char *orders[]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA",
 "AGEM","AGME","AEGM","AEMG","AMGE","AMEG",
 "EGAM","EGMA","EAGM","EAMG","EMGA","EMAG",
 "MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
 const char *s=orders[perm%24];for(int i=0;i<4;i++)if(s[i]=='G')return i;
 return 0;
}
static int pokemon_species(int i){
 uint8_t *p=party(i);uint32_t key=rd32(p)^rd32(p+4);
 int pos=growth_pos(rd32(p)%24);
 return (uint16_t)((rd32(p+32+pos*12)^key)&65535);
}
static int update_species(int i,int new_species){
 uint8_t *p=party(i);
 uint32_t key=rd32(p)^rd32(p+4);
 uint32_t decrypted[12];
 for(int j=0;j<12;j++)decrypted[j]=rd32(p+32+j*4)^key;
 int pos=growth_pos(rd32(p)%24)*3;
 decrypted[pos]=(decrypted[pos]&0xffff0000)|(uint16_t)new_species;
 uint32_t sum=0;
 for(int j=0;j<12;j++)sum+=(decrypted[j]&65535)+(decrypted[j]>>16);
 wr16(p+28,sum&65535);
 for(int j=0;j<12;j++)wr32(p+32+j*4,decrypted[j]^key);
 /* Party Pokemon derived species and stats are not recalculated here.
    Limit this feature to an experimental copied save. */
 uint8_t *s=data+sec[1]*SECTION_SIZE;
 wr16(s+0xff6,checksum(s,lengths[1]));
 return 1;
}
static int copy_file(const char *src,const char *dst){
 FILE *in=fopen(src,"rb");if(!in)return 0;
 int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0644);
 if(fd<0){fclose(in);return 0;}
 uint8_t buf[8192];size_t n;int ok=1;
 while((n=fread(buf,1,sizeof(buf),in))>0)
  if(write(fd,buf,n)!=(ssize_t)n){ok=0;break;}
 if(ferror(in))ok=0;
 if(close(fd))ok=0;fclose(in);
 if(!ok)unlink(dst);
 return ok;
}
static int unique_path(const char *src,const char *suffix,char *dst,size_t cap){
 for(int i=1;i<=999;i++){
  int n=snprintf(dst,cap,"%s.%s.%03d",src,suffix,i);
  if(n<0||(size_t)n>=cap)return 0;
  if(access(dst,F_OK)!=0)return 1;
 }
 return 0;
}
static int backup(void){
 char dst[640];if(!unique_path(files[selected],"backup",dst,sizeof(dst)))return 0;
 return copy_file(files[selected],dst);
}

/* Original Gen 3 item IDs, NOT modern decompilation expansion IDs.
   Ruby/Sapphire writer only; other catalogues are explicitly read-only. */
typedef struct {const char *name;uint16_t id;int offset;int slots;} BagItem;
#define BI(n,i,p,c) {n,i,p,c}
static const BagItem rs_items[]={
 BI("MASTER BALL",1,0x600,16),BI("ULTRA BALL",2,0x600,16),
 BI("GREAT BALL",3,0x600,16),BI("POKE BALL",4,0x600,16),
 BI("SAFARI BALL",5,0x600,16),BI("NET BALL",6,0x600,16),
 BI("DIVE BALL",7,0x600,16),BI("NEST BALL",8,0x600,16),
 BI("REPEAT BALL",9,0x600,16),BI("TIMER BALL",10,0x600,16),
 BI("LUXURY BALL",11,0x600,16),BI("PREMIER BALL",12,0x600,16),
 BI("POTION",13,0x560,20),BI("ANTIDOTE",14,0x560,20),
 BI("BURN HEAL",15,0x560,20),BI("ICE HEAL",16,0x560,20),
 BI("AWAKENING",17,0x560,20),BI("PARLYZ HEAL",18,0x560,20),
 BI("FULL RESTORE",19,0x560,20),BI("MAX POTION",20,0x560,20),
 BI("HYPER POTION",21,0x560,20),BI("SUPER POTION",22,0x560,20),
 BI("FULL HEAL",23,0x560,20),BI("REVIVE",24,0x560,20),
 BI("MAX REVIVE",25,0x560,20),BI("ESCAPE ROPE",85,0x560,20),
 BI("REPEL",86,0x560,20),BI("SUPER REPEL",87,0x560,20),
 BI("MAX REPEL",88,0x560,20),BI("RARE CANDY",68,0x560,20),
 BI("PP UP",69,0x560,20),BI("ZINC",70,0x560,20),
 BI("PP MAX",71,0x560,20),BI("HP UP",63,0x560,20),
 BI("PROTEIN",64,0x560,20),BI("IRON",65,0x560,20),
 BI("CARBOS",66,0x560,20),BI("CALCIUM",67,0x560,20),
 BI("MACH BIKE",259,0x5b0,20),BI("COIN CASE",260,0x5b0,20),
 BI("ITEMFINDER",261,0x5b0,20),BI("OLD ROD",262,0x5b0,20),
 BI("GOOD ROD",263,0x5b0,20),BI("SUPER ROD",264,0x5b0,20),
 BI("S S TICKET",265,0x5b0,20),BI("CONTEST PASS",266,0x5b0,20),
 BI("WAILMER PAIL",268,0x5b0,20),BI("DEVON GOODS",269,0x5b0,20),
 BI("SOOT SACK",270,0x5b0,20),BI("BASEMENT KEY",271,0x5b0,20),
 BI("ACRO BIKE",272,0x5b0,20),BI("POKEBLOCK CASE",273,0x5b0,20),
 BI("LETTER",274,0x5b0,20),BI("EON TICKET EVENT",275,0x5b0,20),
 BI("RED ORB",276,0x5b0,20),BI("BLUE ORB",277,0x5b0,20),
 BI("SCANNER",278,0x5b0,20),BI("GO GOGGLES",279,0x5b0,20),
 BI("METEORITE",280,0x5b0,20),BI("STORAGE KEY",285,0x5b0,20),
 BI("ROOT FOSSIL",286,0x5b0,20),BI("CLAW FOSSIL",287,0x5b0,20),
 BI("DEVON SCOPE",288,0x5b0,20),
 BI("TM01",289,0x640,64),BI("TM13 ICE BEAM",301,0x640,64),
 BI("TM24 THUNDERBOLT",312,0x640,64),BI("TM26 EARTHQUAKE",314,0x640,64),
 BI("TM50",338,0x640,64),BI("HM01 CUT",339,0x640,64),
 BI("HM03 SURF",341,0x640,64),BI("HM07 WATERFALL",345,0x640,64),
 BI("CHERI BERRY",133,0x740,46),BI("PECHA BERRY",134,0x740,46),
 BI("ORAN BERRY",139,0x740,46),BI("SITRUS BERRY",142,0x740,46),
};
/* IDs below are Gen 3 original event items. Preview only while save
   layouts and encryption for Emerald/FRLG are unimplemented. */
static const BagItem frlg_items[]={
 BI("MASTER BALL",1,0,0),BI("RARE CANDY",68,0,0),
 BI("BICYCLE",549,0,0),BI("POKE FLUTE",541,0,0),
 BI("S S TICKET",265,0,0),BI("TRI PASS",556,0,0),
 BI("RAINBOW PASS",557,0,0),BI("MYSTIC TICKET EVENT",559,0,0),
 BI("AURORA TICKET EVENT",560,0,0),BI("POWDER JAR",561,0,0),
 BI("RUBY",562,0,0),BI("SAPPHIRE",563,0,0)
};
static const BagItem em_items[]={
 BI("MASTER BALL",1,0,0),BI("RARE CANDY",68,0,0),
 BI("MACH BIKE",259,0,0),BI("ACRO BIKE",272,0,0),
 BI("EON TICKET EVENT",275,0,0),BI("MYSTIC TICKET EVENT",559,0,0),
 BI("AURORA TICKET EVENT",560,0,0),BI("MAGMA EMBLEM",564,0,0),
 BI("OLD SEA MAP EVENT",565,0,0)
};
static const char *catalog_names[]={"RUBY SAPPHIRE","FIRERED LEAFGREEN","EMERALD"};
static int bag_choice_count(void){return catalog_game==0?(int)(sizeof(rs_items)/sizeof(rs_items[0])):catalog_game==1?(int)(sizeof(frlg_items)/sizeof(frlg_items[0])):(int)(sizeof(em_items)/sizeof(em_items[0]));}
static const BagItem *bag_choice(void){return catalog_game==0?&rs_items[bag_index]:catalog_game==1?&frlg_items[bag_index]:&em_items[bag_index];}
static int add_bag_item(int choice,int qty){
 if(save_game!=1)return 0;
 if(active<0||catalog_game!=0||choice<0||choice>=bag_choice_count()||qty<1||qty>99)return 0;
 const BagItem *item=&rs_items[choice];
 if(item->offset==0x5b0)qty=1;
 uint8_t *p=data+sec[1]*SECTION_SIZE+item->offset;
 int free_slot=-1;
 for(int i=0;i<item->slots;i++){
  uint16_t id=rd16(p+i*4),amount=rd16(p+i*4+2);
  if(id==item->id){
   if(item->offset==0x5b0)return 1;
   if(amount+qty>99)return 0;
   wr16(p+i*4+2,amount+qty);
   goto done;
  }
  if(id==0&&free_slot<0)free_slot=i;
 }
 if(free_slot<0)return 0;
 wr16(p+free_slot*4,item->id);
 wr16(p+free_slot*4+2,qty);
done:
 {uint8_t *section=data+sec[1]*SECTION_SIZE;wr16(section+0xff6,checksum(section,lengths[1]));}
 dirty=1;
 return 1;
}
static int save_bag_copy(void){
 if(active<0)return 0;
 char dst[640];
 if(!unique_path(files[selected],"bag-edit.srm",dst,sizeof(dst)))return 0;
 if(!backup())return 0;
 if(!add_bag_item(bag_index,bag_qty))return 0;
 int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 0;
 int ok=write(fd,data,SAVE_SIZE)==SAVE_SIZE;
 if(close(fd))ok=0;
 if(!ok)unlink(dst);
 return ok;
}


/* Gen 3 PokemonStorage is 0x8344 bytes spread across section IDs 5..13.
   Each section stores 3968 bytes, except section 13 (2000 bytes).
   PokemonStorage starts with currentBox (4 bytes), followed by
   14 * 30 boxed Pokemon records of 80 bytes each. */
static int storage_read(int offset,uint8_t *dst,int n){
 if(offset<0||n<0||offset+n>0x8344)return 0;
 for(int i=0;i<n;i++){
  int logical=offset+i, sid=5+logical/3968, off=logical%3968;
  if(sid>13||off>=lengths[sid])return 0;
  dst[i]=data[sec[sid]*SECTION_SIZE+off];
 }
 return 1;
}
static int storage_write(int offset,const uint8_t *src,int n){
 if(offset<0||n<0||offset+n>0x8344)return 0;
 for(int i=0;i<n;i++){
  int logical=offset+i,sid=5+logical/3968,off=logical%3968;
  if(sid>13||off>=lengths[sid])return 0;
  data[sec[sid]*SECTION_SIZE+off]=src[i];
 }
 return 1;
}
static int box_read(int box,int slot,uint8_t *dst){
 if(box<0||box>=14||slot<0||slot>=30)return 0;
 return storage_read(4+(box*30+slot)*80,dst,80);
}
static int box_species_id(int box,int slot){
 uint8_t mon[80];if(!box_read(box,slot,mon))return -1;
 if(rd32(mon)==0&&rd32(mon+4)==0)return 0;
 uint32_t key=rd32(mon)^rd32(mon+4);
 int pos=growth_pos(rd32(mon)%24);
 return (rd32(mon+32+pos*12)^key)&65535;
}
static int box_update_species(int box,int slot,int new_species){
 uint8_t mon[80];if(!box_read(box,slot,mon))return 0;
 if(rd32(mon)==0&&rd32(mon+4)==0)return 0;
 uint32_t key=rd32(mon)^rd32(mon+4);
 uint32_t words[12];
 for(int j=0;j<12;j++)words[j]=rd32(mon+32+j*4)^key;
 int pos=growth_pos(rd32(mon)%24)*3;
 words[pos]=(words[pos]&0xffff0000u)|(uint16_t)new_species;
 uint32_t sum=0;
 for(int j=0;j<12;j++)sum+=(words[j]&65535)+(words[j]>>16);
 wr16(mon+28,sum&65535);
 for(int j=0;j<12;j++)wr32(mon+32+j*4,words[j]^key);
 if(!storage_write(4+(box*30+slot)*80,mon,80))return 0;
 for(int sid=5;sid<=13;sid++){
  uint8_t *s=data+sec[sid]*SECTION_SIZE;
  wr16(s+0xff6,checksum(s,lengths[sid]));
 }
 return 1;
}

static void recompute_section(int sid){
 uint8_t *s=data+sec[sid]*SECTION_SIZE;
 wr16(s+0xff6,checksum(s,lengths[sid]));
}
static void recompute_pc(void){for(int sid=5;sid<=13;sid++)recompute_section(sid);}
static int validate_box_mon(const uint8_t mon[80]){
 if(rd32(mon)==0&&rd32(mon+4)==0)return 0;
 uint32_t key=rd32(mon)^rd32(mon+4),sum=0;
 for(int i=0;i<12;i++){
  uint32_t v=rd32(mon+32+i*4)^key;
  sum+=(v&65535)+(v>>16);
 }
 return (sum&65535)==rd16(mon+28);
}
static void decrypt_mon(uint8_t mon[80],uint8_t plain[48]){
 uint32_t key=rd32(mon)^rd32(mon+4);
 for(int i=0;i<12;i++)wr32(plain+4*i,rd32(mon+32+i*4)^key);
}
static void encrypt_mon(uint8_t mon[80],const uint8_t plain[48]){
 uint32_t key=rd32(mon)^rd32(mon+4),sum=0;
 for(int i=0;i<12;i++){
  uint32_t v=rd32(plain+4*i);
  sum+=(v&65535)+(v>>16);
  wr32(mon+32+i*4,v^key);
 }
 wr16(mon+28,(uint16_t)sum);
}
static int edit_box_field(int field,int adjustment){
 if(save_game!=1)return 0;
 uint8_t mon[80], plain[48];
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return 0;
 decrypt_mon(mon,plain);
 int growth=growth_pos(rd32(mon)%24)*12;
 int off=field==0?0:field==1?2:field==2?9:-1;
 if(off<0)return 0;
 if(field==2){
  int v=plain[growth+9]+adjustment;
  if(v<0)v=0;if(v>255)v=255;
  plain[growth+9]=(uint8_t)v;
 }else{
  int v=(int)rd16(plain+growth+off)+adjustment;
  int limit=field==0?411:376;
  if(v<1&&field==0)v=1;
  if(v<0)v=0;if(v>limit)v=limit;
  wr16(plain+growth+off,(uint16_t)v);
 }
 encrypt_mon(mon,plain);
 if(!storage_write(4+(box_num*30+box_slot)*80,mon,80))return 0;
 recompute_pc();dirty=1;return 1;
}
static int box_field_value(int field){
 uint8_t mon[80], plain[48];
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return -1;
 decrypt_mon(mon,plain);
 int off=growth_pos(rd32(mon)%24)*12;
 return field==0?rd16(plain+off):field==1?rd16(plain+off+2):plain[off+9];
}
/* Generate a believable egg from an existing boxed Pokemon, retaining
   its species, moves, and IVs. No invented encounter/legality promises. */
static uint32_t egg_rng(void){
 static uint32_t state=0;
 if(!state)state=(uint32_t)time(NULL)^(uint32_t)SDL_GetTicks()^0xbacf1243u;
 state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
static int egg_create(void){
 if(save_game!=1)return 0;
 uint8_t mon[80],plain[48];
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return 0;
 int destbox=-1,destslot=-1;
 for(int b=0;b<14&&destbox<0;b++)for(int i=0;i<30;i++){
  uint8_t other[80];if(!box_read(b,i,other))return 0;
  if(rd32(other)==0&&rd32(other+4)==0){destbox=b;destslot=i;break;}
 }
 if(destbox<0)return 0;
 decrypt_mon(mon,plain);
 /* Set egg bit in M block and clear held item. */
 static const char *orders[]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA","AGEM","AGME","AEGM","AEMG","AMGE","AMEG","EGAM","EGMA","EAGM","EAMG","EMGA","EMAG","MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
 int pidperm=rd32(mon)%24;
 int mblock=0;while(mblock<4&&orders[pidperm][mblock]!='M')mblock++;
 if(mblock>=4)return 0;
 uint32_t ivs=rd32(plain+mblock*12+4);
 wr32(plain+mblock*12+4,ivs|(1u<<30));
 wr16(plain+growth_pos(pidperm)*12+2,0);
 plain[growth_pos(pidperm)*12+9]=20;
 /* Select PID for shiny roll and preserve the original substructure order
    by shuffling decrypted blocks into new PID permutation. */
 uint32_t trainer=rd32(data+sec[0]*SECTION_SIZE+0x0a);
 int shiny=(egg_rng()%egg_odds[odds_index])==0;
 uint32_t newpid=egg_rng();
 if(shiny){
  uint16_t lo=newpid&65535;
  uint16_t hi=lo^(trainer&65535)^(trainer>>16)^(egg_rng()&7);
  newpid=((uint32_t)hi<<16)|lo;
 }else{
  while((((trainer&65535)^(trainer>>16)^(newpid&65535)^(newpid>>16))<8))newpid=egg_rng();
 }
 char oldorder[5],neworder[5];
 memcpy(oldorder,orders[pidperm],5);memcpy(neworder,orders[newpid%24],5);
 uint8_t reordered[48];
 for(int i=0;i<4;i++){
  int from=0;while(from<4&&oldorder[from]!=neworder[i])from++;
  memcpy(reordered+i*12,plain+from*12,12);
 }
 wr32(mon,newpid);wr32(mon+4,trainer);
 memcpy(mon+20,data+sec[0]*SECTION_SIZE,7);
 mon[19]=(mon[19]&~2)|2;mon[19]|=4;
 /* Mark as an egg in the nickname field using Gen 3 text encoding. */
 memset(mon+8,0xff,10);
 mon[8]=0xBF;mon[9]=0xC1;mon[10]=0xC1; /* cosmetic text; game may rename */
 encrypt_mon(mon,reordered);
 if(!storage_write(4+(destbox*30+destslot)*80,mon,80))return 0;
 recompute_pc();dirty=1;return 1;
}

/* PC deletion: replace an occupied, checksum-valid Pokemon with a blank
   80-byte record, preserving every other record and section. */
static int box_delete(void){
 if(save_game!=1)return 0;
 uint8_t mon[80],empty[80]={0};
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return 0;
 if(!storage_write(4+(box_num*30+box_slot)*80,empty,80))return 0;
 recompute_pc();dirty=1;return 1;
}
typedef struct{const char *name;uint16_t species;} EventPokemon;
static const EventPokemon event_pokemon[]={
 {"MEW",151},{"CELEBI",251},{"LUGIA",249},{"HO OH",250},
 {"JIRACHI",407},{"DEOXYS",410}
};
#define EVENT_COUNT ((int)(sizeof(event_pokemon)/sizeof(event_pokemon[0])))
/* Create an experimental boxed Pokemon using a valid existing Pokemon
   as the template. This creates no Mystery Gift card, Wonder Card, or
   event flags. Its moves, IVs, EVs and encounter data originate from
   the template: generated Pokemon are NOT legality-verified. */
static int event_spawn(void){
 if(save_game!=1)return 0;
 uint8_t mon[80],plain[48],out[80];
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return 0;
 int db=-1,ds=-1;
 for(int b=0;b<14&&db<0;b++)for(int j=0;j<30;j++){
  uint8_t other[80];if(!box_read(b,j,other))return 0;
  if(rd32(other)==0&&rd32(other+4)==0){db=b;ds=j;break;}
 }
 if(db<0)return 0;
 decrypt_mon(mon,plain);
 int growth=growth_pos(rd32(mon)%24)*12;
 wr16(plain+growth,event_pokemon[event_index].species);
 wr16(plain+growth+2,0);/* no held item */
 static const char *orders[]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA","AGEM","AGME","AEGM","AEMG","AMGE","AMEG","EGAM","EGMA","EAGM","EAMG","EMGA","EMAG","MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
 int mpos=0;while(mpos<4&&orders[rd32(mon)%24][mpos]!='M')mpos++;
 if(mpos>=4)return 0;
 wr32(plain+mpos*12+4,rd32(plain+mpos*12+4)&~(1u<<30));
 memcpy(out,mon,80);
 /* Clear has-nickname and egg flags so game provides species name. */
 out[19]&=(uint8_t)~(0x04|0x02);
 memset(out+8,0xff,10);
 /* Nonzero PID generated for a distinct Pokemon identity; preserve
    encryption permutation by limiting PID to same mod-24. */
 uint32_t oldpid=rd32(mon),newpid=egg_rng();
 newpid-=newpid%24;newpid+=oldpid%24;
 if(newpid==0)newpid+=24;
 wr32(out,newpid);
 encrypt_mon(out,plain);
 if(!storage_write(4+(db*30+ds)*80,out,80))return 0;
 recompute_pc();dirty=1;return 1;
}
/* Always save to the original RetroArch filename after a verified backup.
   Use an adjacent temporary file and atomic rename to avoid partial writes. */
static int save_in_place(void){
 if(save_game!=1)return 0;
 if(!dirty)return 1;
 if(active<0)return 0;
 int outidx[14];uint32_t counter;
 if(!valid_slot(active,outidx,&counter))return 0;
 if(!backup())return 0;
 char temp[600];int n=snprintf(temp,sizeof(temp),"%s.tmp.%ld",files[selected],(long)getpid());
 if(n<=0||n>=(int)sizeof(temp))return 0;
 struct stat st;if(stat(files[selected],&st))return 0;
 int fd=open(temp,O_WRONLY|O_CREAT|O_EXCL,st.st_mode&0777);
 if(fd<0)return 0;
 size_t offset=0;int ok=1;
 while(offset<SAVE_SIZE){ssize_t w=write(fd,data+offset,SAVE_SIZE-offset);if(w<=0){ok=0;break;}offset+=w;}
 if(ok&&fsync(fd))ok=0;
 if(close(fd))ok=0;
 if(!ok){unlink(temp);return 0;}
 if(rename(temp,files[selected])){unlink(temp);return 0;}
 dirty=0;return 1;
}

static int save_box_copy(void){
 char dst[640];
 if(!unique_path(files[selected],"box-edit.srm",dst,sizeof(dst)))return 0;
 if(!backup())return 0;
 if(!box_update_species(box_num,box_slot,box_species))return 0;
 int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 0;
 int ok=write(fd,data,SAVE_SIZE)==SAVE_SIZE;
 if(close(fd))ok=0;
 if(!ok)unlink(dst);
 return ok;
}

static int save_edited(void){
 if(active<0||party_count()<1)return 0;
 char dst[640];if(!unique_path(files[selected],"edited.srm",dst,sizeof(dst)))return 0;
 if(!backup())return 0;
 if(!update_species(pokemon,species))return 0;
 int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0644);if(fd<0)return 0;
 int ok=write(fd,data,SAVE_SIZE)==SAVE_SIZE;
 if(close(fd))ok=0;if(!ok)unlink(dst);return ok;
}
static void scan(const char *dir,int depth){
 if(depth>12||count>=MAX_FILES)return;
 DIR *d=opendir(dir);if(!d)return;struct dirent *e;
 while((e=readdir(d))&&count<MAX_FILES){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  char path[512];int n=snprintf(path,sizeof(path),"%s/%s",dir,e->d_name);
  if(n<0||n>=512)continue;
  struct stat st;if(lstat(path,&st)||S_ISLNK(st.st_mode))continue;
  if(S_ISDIR(st.st_mode))scan(path,depth+1);
  else if(S_ISREG(st.st_mode)&&st.st_size>=32768&&st.st_size<=262144){
   size_t z=strlen(path);
   if(z>=4&&(!strcasecmp(path+z-4,".srm")||!strcasecmp(path+z-4,".sav")))
    {int dup=0;for(int i=0;i<count;i++)if(!strcmp(files[i],path)){dup=1;break;}
    if(!dup)snprintf(files[count++],512,"%s",path);}
  }
 }
 closedir(d);
}

static void scan_current(void){count=0;selected=0;scan(current_path,0);}
static void list_directories(void){
 ndirs=0;directory_cursor=0;
 DIR *d=opendir(current_path);if(!d)return;
 struct dirent *e;
 while((e=readdir(d))&&ndirs<128){
  if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
  char p[512];int n=snprintf(p,sizeof(p),"%s/%s",current_path,e->d_name);
  struct stat st;
  if(n>0&&n<(int)sizeof(p)&&!lstat(p,&st)&&S_ISDIR(st.st_mode)&&!S_ISLNK(st.st_mode))
   snprintf(dirs[ndirs++],512,"%s",p);
 }
 closedir(d);
}
static void parent_folder(void){
 char *q=strrchr(current_path,'/');
 if(q&&q!=current_path)*q=0;else strcpy(current_path,"/");
 list_directories();
}

static const char *abc=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/:";
static const unsigned char glyph[][7]={
 {0,0,0,0,0,0,0},{14,17,31,17,17,17,0},{30,17,30,17,17,30,0},
 {14,17,16,16,17,14,0},{30,17,17,17,17,30,0},{31,16,30,16,16,31,0},
 {31,16,30,16,16,16,0},{14,17,16,23,17,15,0},{17,17,31,17,17,17,0},
 {14,4,4,4,4,14,0},{7,2,2,2,18,12,0},{17,18,28,18,17,17,0},
 {16,16,16,16,16,31,0},{17,27,21,21,17,17,0},{17,25,21,21,21,10,0},
 {14,17,17,17,17,14,0},{30,17,30,16,16,16,0},{14,17,17,21,18,13,0},
 {30,17,30,20,18,17,0},{15,16,14,1,1,30,0},{31,4,4,4,4,4,0},
 {17,17,17,17,17,14,0},{17,10,4,4,10,4,0},{17,17,21,21,21,10,0},
 {17,10,4,4,10,17,0},{17,10,4,4,4,4,0},{31,2,4,8,16,31,0},
 {14,17,19,21,25,14,0},{4,12,4,4,4,14,0},{14,17,2,4,8,31,0},
 {30,1,6,1,17,14,0},{2,6,10,31,2,2,0},{31,16,30,1,17,14,0},
 {6,8,30,17,17,14,0},{31,1,2,4,4,4,0},{14,17,14,17,17,14,0},
 {14,17,15,1,2,12,0},{0,0,0,0,0,4,0},{1,2,4,8,16,0,0},
 {0,0,0,0,0,0,0},{0,0,0,0,0,31,0},{0,0,0,0,0,0,0},{0,4,0,0,4,0,0}
};
static void fill(SDL_Renderer *r,int x,int y,int w,int h,int red,int green,int blue){
 SDL_Rect q={x,y,w,h};SDL_SetRenderDrawColor(r,red,green,blue,255);SDL_RenderFillRect(r,&q);
}
static void label(SDL_Renderer *r,int x,int y,const char *s,int scale){
 for(;*s&&x<620-6*scale;s++,x+=6*scale){
  char c=*s;if(c>='a'&&c<='z')c-=32;
  const char *q=strchr(abc,c);int idx=q?(int)(q-abc):0;
  if(idx>=(int)(sizeof(glyph)/sizeof(glyph[0])))idx=0;
  for(int row=0;row<7;row++)for(int col=0;col<5;col++)
   if(glyph[idx][row]&(1<<(4-col)))fill(r,x+col*scale,y+row*scale,scale,scale,236,241,248);
 }
}
static void number(SDL_Renderer *r,int x,int y,const char *fmt,int v){
 char b[100];snprintf(b,sizeof(b),fmt,v);label(r,x,y,b,2);
}

/* Minimal, deliberately lightweight pixel UI; icons are generated procedural
   pixel glyphs, not copyrighted game icon artwork. */
static void pixel_mon(SDL_Renderer *r,int x,int y,int sp,int scale){
 if(sp<=0)return;
 int c=(sp*37)%115;
 fill(r,x+4*scale,y,6*scale,2*scale,90+c,120+c/2,130);
 fill(r,x+2*scale,y+2*scale,10*scale,6*scale,92+c,126+c/2,148);
 fill(r,x+3*scale,y+8*scale,8*scale,3*scale,63+c,99+c/2,111);
 fill(r,x+4*scale,y+11*scale,2*scale,2*scale,60,65,84);
 fill(r,x+9*scale,y+11*scale,2*scale,2*scale,60,65,84);
 fill(r,x+4*scale,y+4*scale,2*scale,2*scale,19,29,50);
 fill(r,x+9*scale,y+4*scale,2*scale,2*scale,19,29,50);
 fill(r,x+4*scale,y+4*scale,scale,scale,242,248,255);
 fill(r,x+9*scale,y+4*scale,scale,scale,242,248,255);
}
static void panel(SDL_Renderer *r,int x,int y,int w,int h){
 fill(r,x,y,w,h,24,38,58);fill(r,x,y,w,2,64,117,130);
}
static void tabline(SDL_Renderer *r){
 const char *names[]={"BAG","PC","EGGS","EVENTS","HEALTH"};
 for(int i=0;i<5;i++){
  if(tab==i)fill(r,6+i*126,67,123,37,36,98,116);
  label(r,13+i*126,79,names[i],2);
 }
}
int main(void){
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER))return 1;
 SDL_Window *w=SDL_CreateWindow("Pokemon Editor",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,480,SDL_WINDOW_FULLSCREEN_DESKTOP);
 if(!w){SDL_Quit();return 1;}
 SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);
 if(!r){SDL_DestroyWindow(w);SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(r,640,480);
 for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i))SDL_GameControllerOpen(i);
 for(int i=0;i<ROOT_COUNT;i++)scan(root_paths[i],0);
 int running=1;
 while(running){
  SDL_Event e;
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT)running=0;
   int b=-1;
   if(e.type==SDL_CONTROLLERBUTTONDOWN)b=e.cbutton.button;
   if(e.type==SDL_KEYDOWN){
    SDL_Keycode k=e.key.keysym.sym;
    if(k==SDLK_DOWN)b=SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if(k==SDLK_UP)b=SDL_CONTROLLER_BUTTON_DPAD_UP;
    if(k==SDLK_LEFT)b=SDL_CONTROLLER_BUTTON_DPAD_LEFT;
    if(k==SDLK_RIGHT)b=SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
    if(k==SDLK_RETURN)b=SDL_CONTROLLER_BUTTON_A;
    if(k==SDLK_ESCAPE||k==SDLK_BACKSPACE)b=SDL_CONTROLLER_BUTTON_B;
    if(k==SDLK_x)b=SDL_CONTROLLER_BUTTON_X;
    if(k==SDLK_y)b=SDL_CONTROLLER_BUTTON_Y;
    if(k==SDLK_q)b=SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
    if(k==SDLK_e)b=SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    if(k==SDLK_TAB)b=SDL_CONTROLLER_BUTTON_START;
    if(k==SDLK_f)b=SDL_CONTROLLER_BUTTON_BACK;
   }
   if(b<0)continue;
   if(delete_confirm){
    if(b==SDL_CONTROLLER_BUTTON_A){status=box_delete()?12:5;delete_confirm=0;}
    if(b==SDL_CONTROLLER_BUTTON_B){delete_confirm=0;status=0;}
    continue;
   }
   if(confirming){
    if(b==SDL_CONTROLLER_BUTTON_A){status=save_in_place()?4:5;confirming=0;}
    if(b==SDL_CONTROLLER_BUTTON_B){confirming=0;status=0;}
    continue;
   }
   if(folder_view){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&directory_cursor<ndirs-1)directory_cursor++;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&directory_cursor>0)directory_cursor--;
    if(b==SDL_CONTROLLER_BUTTON_A&&ndirs){snprintf(current_path,sizeof(current_path),"%s",dirs[directory_cursor]);list_directories();}
    if(b==SDL_CONTROLLER_BUTTON_B)parent_folder();
    if(b==SDL_CONTROLLER_BUTTON_Y){scan_current();folder_view=0;status=count?6:7;}
    if(b==SDL_CONTROLLER_BUTTON_X){folder_view=0;status=0;}
    if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){root_index=(root_index+ROOT_COUNT-1)%ROOT_COUNT;snprintf(current_path,sizeof(current_path),"%s",root_paths[root_index]);list_directories();}
    if(b==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){root_index=(root_index+1)%ROOT_COUNT;snprintf(current_path,sizeof(current_path),"%s",root_paths[root_index]);list_directories();}
    continue;
   }
   if(!detail){
    if(b==SDL_CONTROLLER_BUTTON_B)running=0;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&selected<count-1)selected++;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&selected>0)selected--;
    if(b==SDL_CONTROLLER_BUTTON_START){count=0;selected=0;for(int i=0;i<ROOT_COUNT;i++)scan(root_paths[i],0);status=0;}
    if(b==SDL_CONTROLLER_BUTTON_X){folder_view=1;list_directories();status=0;}
    if(b==SDL_CONTROLLER_BUTTON_A&&count){
     if(load(files[selected])){detail=1;tab=0;status=0;bag_index=0;bag_qty=50;catalog_game=0;event_index=0;box_num=0;box_slot=0;edit_field=0;}
     else status=3;
    }
    continue;
   }
   if(b==SDL_CONTROLLER_BUTTON_Y){confirming=dirty&&save_game==1;status=save_game!=1?15:dirty?0:8;continue;}
   if(b==SDL_CONTROLLER_BUTTON_B){if(dirty){status=14;}else{detail=0;status=0;}continue;}
   if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){tab=(tab+4)%5;status=0;continue;}
   if(b==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){tab=(tab+1)%5;status=0;continue;}
   if(save_game!=1 && tab!=4){status=15;continue;}
   if(tab==0){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)bag_index=(bag_index+1)%bag_choice_count();
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)bag_index=(bag_index+bag_choice_count()-1)%bag_choice_count();
    if(b==SDL_CONTROLLER_BUTTON_START){catalog_game=(catalog_game+1)%3;bag_index=0;}
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&bag_qty>1)bag_qty--;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&bag_qty<99)bag_qty++;
    if(b==SDL_CONTROLLER_BUTTON_A){
     if(add_bag_item(bag_index,bag_qty)){dirty=1;status=10;}else status=5;
    }
   }else if(tab==1){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&box_slot<24)box_slot+=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&box_slot>=6)box_slot-=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&box_slot%6>0)box_slot--;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&box_slot%6<5)box_slot++;
    if(b==SDL_CONTROLLER_BUTTON_START)box_num=(box_num+1)%14;
    if(b==SDL_CONTROLLER_BUTTON_X)edit_field=(edit_field+1)%4;
    if(b==SDL_CONTROLLER_BUTTON_A){if(edit_field==3){uint8_t mon[80];if(box_read(box_num,box_slot,mon)&&validate_box_mon(mon))delete_confirm=1;else status=5;}else status=edit_box_field(edit_field,1)?10:5;}
    if(b==SDL_CONTROLLER_BUTTON_BACK&&edit_field<3){status=edit_box_field(edit_field,-1)?10:5;}
   }else if(tab==2){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&box_slot<24)box_slot+=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&box_slot>=6)box_slot-=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&box_slot%6>0)box_slot--;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&box_slot%6<5)box_slot++;
    if(b==SDL_CONTROLLER_BUTTON_START)box_num=(box_num+1)%14;
    if(b==SDL_CONTROLLER_BUTTON_X)odds_index=(odds_index+1)%8;
    if(b==SDL_CONTROLLER_BUTTON_A)status=egg_create()?11:5;
   }else if(tab==3){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)event_index=(event_index+1)%EVENT_COUNT;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)event_index=(event_index+EVENT_COUNT-1)%EVENT_COUNT;
    if(b==SDL_CONTROLLER_BUTTON_START)box_num=(box_num+1)%14;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)box_slot=(box_slot+1)%30;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT)box_slot=(box_slot+29)%30;
    if(b==SDL_CONTROLLER_BUTTON_A)status=event_spawn()?13:5;
   }else if(tab==4){
    if(b==SDL_CONTROLLER_BUTTON_A)status=backup()?1:2;
   }
  }
  SDL_SetRenderDrawColor(r,12,21,35,255);SDL_RenderClear(r);
  fill(r,0,0,640,58,20,67,80);
  label(r,17,17,"POKEMON EDITOR V0.8",3);
  if(folder_view){
   label(r,20,72,"CHOOSE RETROARCH SAVE FOLDER",2);
   panel(r,10,104,620,294);
   const char *cp=strrchr(current_path,'/');label(r,18,114,cp&&cp[1]?cp+1:current_path,2);
   int first=directory_cursor>5?directory_cursor-5:0;
   for(int i=first,y=146;i<ndirs&&y<386;i++,y+=30){
    if(i==directory_cursor)fill(r,12,y-3,610,28,41,97,119);
    const char *name=strrchr(dirs[i],'/');label(r,20,y,name?name+1:dirs[i],2);
   }
   label(r,15,411,"A OPEN  B UP  L R ROOT",2);
   label(r,15,441,"Y SCAN HERE  X EXIT",2);
  }else if(!detail){
   label(r,18,72,"RETROARCH SAVE FILES",2);
   if(!count)label(r,20,168,"NO SAVES - PRESS X FOR FOLDERS",2);
   int first=selected>7?selected-7:0;
   for(int i=first,y=115;i<count&&y<393;i++,y+=34){
    if(selected==i)fill(r,10,y-4,620,30,38,96,116);
    const char *name=strrchr(files[i],'/');label(r,17,y,name?name+1:files[i],2);
   }
   label(r,18,413,"A OPEN  X FOLDERS  START RESCAN",2);
   label(r,18,440,"R S WRITE  FR LG READ ONLY",2);
  }else{
   tabline(r);
   if(tab==4){
    panel(r,12,116,616,283);
    const char *games[]={"UNIDENTIFIED","RUBY SAPPHIRE","FIRERED","LEAFGREEN","EMERALD"};
    label(r,26,139,"SAVE HEALTH CHECK",3);
    label(r,26,183,games[save_game],2);
    label(r,26,215,save_slot_valid[0]?"SLOT A CHECKSUM: VALID":"SLOT A CHECKSUM: BAD",2);
    label(r,26,245,save_slot_valid[1]?"SLOT B CHECKSUM: VALID":"SLOT B CHECKSUM: BAD",2);
    label(r,26,275,save_game==1?"RUBY SAPPHIRE EDIT MODE":"READ ONLY - EDITING DISABLED",2);
    label(r,26,316,dirty?"UNSAVED MODIFICATIONS":"NO UNSAVED CHANGES",2);
    label(r,26,357,"A MAKE NUMBERED BACKUP",2);
   }else if(save_game!=1){
    panel(r,12,116,616,280);
    label(r,26,150,"SAVE OPENED - READ ONLY",3);
    label(r,26,213,"GAME EDITING NOT YET VALIDATED",2);
    label(r,26,250,"HEALTH TAB SHOWS SAVE STATUS",2);
    label(r,26,290,"YOU CAN MAKE A BACKUP THERE",2);
   }else if(tab==0){
    panel(r,12,116,616,274);
    label(r,26,138,"BAG STORAGE",3);
    label(r,26,177,catalog_names[catalog_game],2);
    label(r,26,218,bag_choice()->name,3);
    number(r,460,170,"ITEM %d",bag_index+1);
    number(r,460,197,"OF %d",bag_choice_count());
    number(r,26,275,"ADD AMOUNT: %d",bag_qty);
    label(r,26,329,"START GAME  UP DOWN ITEM",2);
    if(catalog_game==0)label(r,26,352,"A ADD TO BAG - RUBY SAPPHIRE",2);
    else label(r,26,352,"CATALOG ONLY - DO NOT WRITE",2);
    if(bag_choice()->offset==0x5b0)label(r,26,375,"KEY ITEM QUANTITY ONE",2);
   }else if(tab==3){
    panel(r,12,115,616,275);
    label(r,24,138,"EVENT POKEMON GENERATOR",3);
    label(r,24,193,event_pokemon[event_index].name,3);
    number(r,24,241,"INTERNAL SPECIES: %d",event_pokemon[event_index].species);
    number(r,24,280,"TEMPLATE BOX %d",box_num+1);
    number(r,320,280,"SLOT %d",box_slot+1);
    label(r,24,324,"A SPAWN FROM PC TEMPLATE",2);
    label(r,24,354,"CUSTOM TEMPLATE NOT AUTHENTIC",2);
   }else{
    panel(r,10,114,620,272);
    number(r,23,123,"BOX %d / 14",box_num+1);
    for(int i=0;i<30;i++){
     int x=19+i%6*102,y=158+i/6*43;
     if(i==box_slot)fill(r,x-4,y-3,96,40,43,107,127);
     int species_id=box_species_id(box_num,i);
     if(species_id>0){pixel_mon(r,x,y,species_id,2);number(r,x+35,y+10,"%d",species_id);}
     else label(r,x+25,y+10,"--",2);
    }
    if(tab==1){
     const char *fields[]={"SPECIES","HELD ITEM","FRIENDSHIP","DELETE POKEMON"};
     label(r,16,392,fields[edit_field],2);
     if(edit_field<3)number(r,190,392,"VALUE: %d",box_field_value(edit_field));
     label(r,16,418,edit_field==3?"A DELETE - CONFIRM REQUIRED":"X FIELD  A PLUS  SELECT MINUS",2);
    }else{
     label(r,14,388,"EGG SOURCE: SELECT BOX POKEMON",2);
     label(r,14,413,egg_odds_label[odds_index],2);
    }
   }
   if(tab==4)label(r,16,441,"A BACKUP   L R TABS",2);
   else if(tab==3)label(r,16,441,"A SPAWN  START BOX  L R TABS",2);
   else if(tab==2)label(r,16,441,"A MAKE EGG  X ODDS  START BOX",2);
   else label(r,16,441,"Y SAVE  L R TABS  START BOX",2);
  }
  if(delete_confirm){
   fill(r,32,160,576,165,14,32,48);
   label(r,55,182,"DELETE SELECTED POKEMON?",3);
   label(r,55,233,"THIS REMOVES THE PC SLOT",2);
   label(r,55,255,"SAVE REQUIRED TO COMMIT",2);
   label(r,55,280,"A DELETE    B CANCEL",2);
  }
  if(confirming){
   fill(r,32,160,576,165,14,32,48);
   label(r,55,182,"SAVE TO ORIGINAL FILE?",3);
   label(r,55,233,"CLOSE RETROARCH BEFORE SAVING",2);
   label(r,55,255,"BACKUP WILL BE MADE FIRST",2);
   label(r,55,280,"A YES    B CANCEL",2);
  }
  if(status){
   const char *msgs[]={"","BACKUP CREATED","BACKUP FAILED","INVALID SAVE","SAVED TO GAME FILE","OPERATION FAILED","FOLDER FOUND","FOLDER EMPTY","NOTHING TO SAVE","UNSAVED CHANGES LOST","CHANGE STAGED","EGG ADDED TO PC","POKEMON DELETED","EVENT MON STAGED","SAVE OR RELOAD BEFORE EXIT","READ ONLY SAVE"};
   if(status>=1&&status<=15){fill(r,6,456,628,23,23,57,68);label(r,13,460,msgs[status],2);}
  }else if(detail&&!confirming)label(r,520,460,dirty?"UNSAVED":"CLEAN",2);
  SDL_RenderPresent(r);SDL_Delay(16);
 }
 SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
