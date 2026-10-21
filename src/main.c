#define _GNU_SOURCE

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
static uint8_t data[SAVE_SIZE], snapshot[SAVE_SIZE];
static int dirty=0, confirming=0, odds_index=0, egg_species=1, edit_field=0, folder_view=0, root_index=0;
static int catalog_game=0, delete_confirm=0, event_index=0, save_game=0, save_slot_valid[2]={0,0}, tool_cursor=0, creator_dex=151, bag_raw_mode=0, import_mode=0, import_cursor=0;
static int enc_dex=151, enc_level=30, enc_field=0, enc_mode=0;
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
 memcpy(sec,active?b:a,sizeof(sec));memcpy(snapshot,data,SAVE_SIZE);dirty=0;return 1;
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
/* Pocket offsets are offsets inside logical section 1 (SaveBlock1).
   FRLG/E quantity fields are encrypted using SaveBlock2's security key. */
static void recompute_section(int sid);
typedef struct {int offset,slots;} Pocket;
static Pocket pocket_for(int game,int kind){
 static const Pocket p[3][5]={
  {{0x560,20},{0x5b0,20},{0x600,16},{0x640,64},{0x740,46}},
  {{0x310,42},{0x3b8,30},{0x430,13},{0x464,58},{0x54c,43}},
  {{0x560,30},{0x5d8,30},{0x650,16},{0x690,64},{0x790,46}}
 };
 int row=game==1?0:(game==2||game==3)?1:game==4?2:-1;
 Pocket bad={0,0};return row<0||kind<0||kind>4?bad:p[row][kind];
}
static uint16_t item_key(void){
 if(save_game==1)return 0;
 const uint8_t *s=data+sec[0]*SECTION_SIZE;
 return (uint16_t)rd32(s+(save_game==4?0xac:0xf20));
}
static int item_pocket(uint16_t id){
 if(id>=1&&id<=12)return 2;
 if(id>=133&&id<=175)return 4;
 if(id>=289&&id<=346)return 3;
 if(id>=259&&id<=288)return 1;
 if(id>=349&&id<=376)return 3;
 if(id>=500&&id<=575)return 1;
 return 0;
}
static uint16_t active_item_id(void){
 if(bag_raw_mode)return (uint16_t)(bag_index+1);
 return bag_choice()->id;
}
static int add_item_by_id(uint16_t id,int qty){
 if(active<0||save_game==0||id==0||qty<1||qty>999)return 0;
 int kind=item_pocket(id);
 Pocket pocket=pocket_for(save_game,kind);
 if(!pocket.slots)return 0;
 if(kind==1)qty=1;
 uint8_t *p=data+sec[1]*SECTION_SIZE+pocket.offset;
 int free_slot=-1;uint16_t key=item_key();
 for(int i=0;i<pocket.slots;i++){
  uint16_t found=rd16(p+i*4);
  if(found==id){
   int old=rd16(p+i*4+2)^key;
   if(kind==1)return 1;
   if(old+qty>999)return 0;
   wr16(p+i*4+2,(uint16_t)(old+qty)^key);
   recompute_section(1);dirty=1;return 1;
  }
  if(!found&&free_slot<0)free_slot=i;
 }
 if(free_slot<0)return 0;
 wr16(p+free_slot*4,id);wr16(p+free_slot*4+2,(uint16_t)qty^key);
 recompute_section(1);dirty=1;return 1;
}
static int add_bag_item(int choice,int qty){
 if(!bag_raw_mode&&(catalog_game!=0&&save_game==1))return 0;
 if(!bag_raw_mode&&(catalog_game==0&&save_game!=1))return 0;
 if(!bag_raw_mode&&(catalog_game==1&&(save_game!=2&&save_game!=3)))return 0;
 if(!bag_raw_mode&&(catalog_game==2&&save_game!=4))return 0;
 (void)choice;return add_item_by_id(active_item_id(),qty);
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
 if(save_game<1)return 0;
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
 if(save_game<1)return 0;
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
 if(save_game<1)return 0;
 uint8_t mon[80],empty[80]={0};
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return 0;
 if(!storage_write(4+(box_num*30+box_slot)*80,empty,80))return 0;
 recompute_pc();dirty=1;return 1;
}
/* Offline catalogue: these are HISTORICAL EVENT REFERENCES, not archived
   original PK3 files. They are reconstructed using the creator and never
   described as original distribution specimens. */
typedef struct {const char *name;int dex;} EventPreset;
static const EventPreset event_pokemon[]={
 {"WISHMKR JIRACHI",385},{"CHANNEL JIRACHI",385},
 {"MYSTRY MEW",151},{"AURA MEW",151},{"HADOU MEW",151},
 {"SPACE C DEOXYS",386},{"DOEL DEOXYS",386},
 {"AGETO CELEBI",251},{"MITSURIN CELEBI",251},
 {"ROCKS METANG",375},{"BERRY FIX ZIGZAGOON",263},
 {"POKEPARK MEW",151},{"POKEPARK CELEBI",251},
 {"TANABATA JIRACHI",385},{"NEGAIBOSHI JIRACHI",385},
 {"POKEPARK MEOWTH",52},{"ANA PIKACHU",25},
 {"FESTA METANG",375},{"SUNDAY WOBBUFFET",202},
 {"HADOU REGICE",378},{"HADOU REGISTEEL",379},
 {"GW PIKACHU",25},{"YOKOHAMA PIKACHU",25}
};
#define EVENT_COUNT ((int)(sizeof(event_pokemon)/sizeof(event_pokemon[0])))
static int dex_to_internal(int dex){return dex<=251?dex:dex<=386?dex+25:0;}
/* 0 creator, 1 reconstructed historical reference, 2 external PK3 import */
static int creator_mode=0,creator_field=0,creator_level=5,creator_nature=0;
static int creator_ability=0,creator_item=0,creator_iv=20,creator_ev=0;
static int creator_shiny=0,creator_moves[4]={33,0,0,0};
static uint32_t creator_seed=0;
static uint32_t creator_random(void){
 if(!creator_seed)creator_seed=(uint32_t)time(NULL)^((uint32_t)clock()<<11)^0xA5319B47u;
 creator_seed^=creator_seed<<13;creator_seed^=creator_seed>>17;creator_seed^=creator_seed<<5;
 return creator_seed?creator_seed:0x9e3779b9u;
}
static int creator_empty_slot(int *b,int *s){
 for(int box=0;box<14;box++)for(int slot=0;slot<30;slot++){
  uint8_t mon[80];if(!box_read(box,slot,mon))return 0;
  if(rd32(mon)==0&&rd32(mon+4)==0){*b=box;*s=slot;return 1;}
 }
 return 0;
}
/* GBA character encoding for uppercase Latin names and digits. */
static void gba_name(uint8_t *dst,int cap,const char *name){
 memset(dst,0xff,(size_t)cap);
 for(int i=0;i<cap-1&&name[i];i++){
  char ch=name[i];
  if(ch>='A'&&ch<='Z')dst[i]=(uint8_t)(0xBB+ch-'A');
  else if(ch>='0'&&ch<='9')dst[i]=(uint8_t)(0xA1+ch-'0');
  else if(ch==' ')dst[i]=0;
  else break;
 }
}
static int creator_generate(int dex,int reconstructed){
 if(save_game<1||dex<1||dex>386)return 0;
 int b=-1,s=-1;if(!creator_empty_slot(&b,&s))return 0;
 uint8_t mon[80]={0},plain[48]={0};
 /* Trainer ID at saveblock2 + 0xA in Gen 3 GBA games. */
 uint32_t tid=rd32(data+sec[0]*SECTION_SIZE+0x0a);
 uint32_t pid=0;
 /* Choose personality with exactly the requested nature and shiny flag. */
 if(creator_shiny){
  uint16_t tsv=(uint16_t)((tid&65535)^(tid>>16));
  for(int attempt=0;attempt<2000;attempt++){
   uint16_t low=(uint16_t)creator_random();
   uint16_t high=(uint16_t)(low^tsv^(creator_random()&7));
   pid=((uint32_t)high<<16)|low;
   if((int)(pid%25)==creator_nature)break;
  }
 }else{
  for(int attempt=0;attempt<2000;attempt++){
   pid=creator_random();
   if((int)(pid%25)==creator_nature &&
     (((tid&65535)^(tid>>16)^(pid&65535)^(pid>>16))>=8))break;
  }
 }
 if((int)(pid%25)!=creator_nature)return 0;
 wr32(mon,pid);wr32(mon+4,tid);
 /* A generic, valid nickname is used because species-specific GBA strings
    require a complete localized species-name table. */
 char nick[12];snprintf(nick,sizeof(nick),"PKMN%d",dex);
 gba_name(mon+8,10,nick);gba_name(mon+20,7,"EDITOR");
 mon[18]=2; /* English language */
 mon[19]=(uint8_t)(creator_ability?1:0); /* flag bit zero is not ability;
   actual ability selection is encrypted in misc origins bit 31 below */
 mon[19]=2; /* marked nickname */
 uint32_t growth=0;
 wr16(plain+growth,dex_to_internal(dex));
 wr16(plain+growth+2,(uint16_t)creator_item);
 /* Level is not a field in boxed PK3. Set experience as a medium-fast
    approximation; actual level depends on species growth group. */
 uint32_t exp=(uint32_t)creator_level*creator_level*creator_level;
 wr32(plain+growth+4,exp);
 plain[growth+9]=70; /* friendship */
 /* Order GAEM, chosen through PID %24 == 0? PID permutation may vary.
    Reorder logical G/A/E/M into permutation dictated by PID. */
 uint8_t logical[48];memcpy(logical,plain,48);
 /* Attacks substructure occupies logical bytes 12..23. */
 for(int i=0;i<4;i++)wr16(logical+12+i*2,(uint16_t)creator_moves[i]);
 for(int i=0;i<4;i++)logical[20+i]=creator_moves[i]?35:0;
 for(int i=0;i<6;i++)logical[24+i]=(uint8_t)creator_ev;
 /* IVs packed, 6x 5-bit; personality controls nature. */
 uint32_t iv=0;for(int i=0;i<6;i++)iv|=(uint32_t)creator_iv<<(5*i);
 wr32(logical+40,iv);
 /* Origin info: level at capture and game-of-origin are descriptive only.
    Do not forge a historical event's OT or encounter data. */
 wr16(logical+36,(uint16_t)(creator_level|(save_game==1?1u<<7:4u<<7)));
 if(creator_ability)wr32(logical+40,rd32(logical+40)|(1u<<31));
 static const char *orders[]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA",
 "AGEM","AGME","AEGM","AEMG","AMGE","AMEG",
 "EGAM","EGMA","EAGM","EAMG","EMGA","EMAG",
 "MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
 const char *perm=orders[pid%24];uint8_t arranged[48];
 for(int j=0;j<4;j++){
  int block=perm[j]=='G'?0:perm[j]=='A'?1:perm[j]=='E'?2:3;
  memcpy(arranged+j*12,logical+block*12,12);
 }
 encrypt_mon(mon,arranged);
 if(!validate_box_mon(mon))return 0;
 if(!storage_write(4+(b*30+s)*80,mon,80))return 0;
 recompute_pc();dirty=1;
 (void)reconstructed;
 return 1;
}
static int event_spawn(void){
 /* Preset changes dex, but does NOT claim authentic event data. */
 int dex=event_pokemon[event_index].dex;
 return creator_generate(dex,1);
}
static int creator_change(int field,int delta){
 int *value=NULL,low=0,high=0;
 switch(field){
  case 0:value=&creator_dex;low=1;high=386;break;
  case 1:value=&creator_level;low=1;high=100;break;
  case 2:value=&creator_nature;low=0;high=24;break;
  case 3:value=&creator_ability;low=0;high=1;break;
  case 4:value=&creator_item;low=0;high=376;break;
  case 5:case 6:case 7:case 8:value=&creator_moves[field-5];low=0;high=354;break;
  case 9:value=&creator_iv;low=0;high=31;break;
  case 10:value=&creator_ev;low=0;high=42;break;
  case 11:value=&creator_shiny;low=0;high=1;break;
  default:return 0;
 }
 int next=*value+delta;if(next<low)next=low;if(next>high)next=high;
 *value=next;return 1;
}
/* Always save to the original RetroArch filename after a verified backup.
   Use an adjacent temporary file and atomic rename to avoid partial writes. */
/* Experimental direct installation of a game-specific RetroArch cheat.
   Never overwrite existing cheats or patch RetroArch's global config. */
static int find_cheats_root(char *out,size_t cap){
 const char *env=getenv("LIBRETRO_CHEATS_DIRECTORY");
 if(env&&*env){struct stat st;if(!stat(env,&st)&&S_ISDIR(st.st_mode)){
   snprintf(out,cap,"%s",env);return 1;}}
 const char *cfgs[]={"/run/muos/storage/info/config/retroarch.cfg",
                     "/opt/muos/share/info/config/retroarch.cfg",
                     "/mnt/mmc/MUOS/info/config/retroarch.cfg",
                     "/mnt/sdcard/MUOS/info/config/retroarch.cfg"};
 for(int i=0;i<4;i++){
  FILE *f=fopen(cfgs[i],"r");if(!f)continue;
  char line[1024];while(fgets(line,sizeof(line),f)){
   char value[700];
   if(sscanf(line," cheat_database_path = \"%699[^\"]\"",value)==1){
    struct stat st;
    if(!stat(value,&st)&&S_ISDIR(st.st_mode)){
     snprintf(out,cap,"%s",value);fclose(f);return 1;
    }
   }
  }
  fclose(f);
 }
 const char *roots[]={"/run/muos/storage/retroarch/cheats",
                      "/mnt/mmc/MUOS/retroarch/cheats",
                      "/mnt/sdcard/MUOS/retroarch/cheats"};
 for(int i=0;i<3;i++){struct stat st;
  if(!stat(roots[i],&st)&&S_ISDIR(st.st_mode)){
   snprintf(out,cap,"%s",roots[i]);return 1;
  }}
 return 0;
}
static int export_encounter(void){
 if(save_game!=2&&save_game!=3)return 0;
 if(enc_dex<1||enc_dex>386||enc_level<1||enc_level>99)return 0;
 int sid=dex_to_internal(enc_dex);
 if(sid<1||sid>411)return 0;
 char root[700],sub[900],dst[1100];
 if(!find_cheats_root(root,sizeof(root)))return 0;
 /* RetroArch stores game-specific cheats in a core-named subdirectory.
    The exact core title can vary across versions; try established folders. */
 const char *cores[]={"mGBA","Nintendo - Game Boy Advance (mGBA)","gpSP"};
 int found=0;
 for(int i=0;i<3;i++){
  snprintf(sub,sizeof(sub),"%s/%s",root,cores[i]);
  struct stat st;if(!stat(sub,&st)&&S_ISDIR(st.st_mode)){found=1;break;}
 }
 if(!found)return 0;
 /* The save must share its basename with the ROM for game-specific loading. */
 const char *base=strrchr(files[selected],'/');base=base?base+1:files[selected];
 char stem[300];snprintf(stem,sizeof(stem),"%s",base);
 char *dot=strrchr(stem,'.');if(dot)*dot=0;
 if(strchr(stem,'/')||strchr(stem,'\\')||!stem[0])return 0;
 snprintf(dst,sizeof(dst),"%s/%s.cht",sub,stem);
 /* Existing per-game cheats might contain other valuable codes: never replace. */
 int fd=open(dst,O_WRONLY|O_CREAT|O_EXCL,0644);
 if(fd<0)return 0;
 FILE *f=fdopen(fd,"w");if(!f){close(fd);unlink(dst);return 0;}
 fprintf(f,"cheats = 1\n");
 fprintf(f,"cheat0_desc = \"Pokemon Editor wild encounter Dex %d Lv %d\"\n",enc_dex,enc_level);
 fprintf(f,"cheat0_enable = true\n");
 fprintf(f,"cheat0_handler = 1\n");
 fprintf(f,"cheat0_code = \"83007CEE %04X+33007CF0 %04X\"\n",sid,enc_level);
 int ok=!ferror(f);if(fclose(f))ok=0;
 if(!ok)unlink(dst);
 return ok;
}
static int save_in_place(void){
 if(save_game<1)return 0;
 if(!dirty)return 1;
 if(active<0)return 0;
 int outidx[14];uint32_t counter;
 if(!valid_slot(active,outidx,&counter))return 0;
 /* Refuse to overwrite a save changed by RetroArch since opening. */
 FILE *verify=fopen(files[selected],"rb");if(!verify)return 0;
 uint8_t checkbuf[8192];int identical=1;
 for(size_t i=0;i<SAVE_SIZE;i+=sizeof(checkbuf)){
  if(fread(checkbuf,1,sizeof(checkbuf),verify)!=sizeof(checkbuf)||
     memcmp(checkbuf,snapshot+i,sizeof(checkbuf))) {identical=0;break;}
 }
 fclose(verify);if(!identical)return 0;
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
 memcpy(snapshot,data,SAVE_SIZE);dirty=0;return 1;
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

/* Offline PK3 imports: only valid boxed records (80 bytes) and
   party records (100 bytes). No claim that a file represents a real event. */
#define MAX_PK3 128
static char pk3_files[MAX_PK3][512];static int pk3_count=0;
static void scan_pk3_dir(const char *dir,int depth){
 if(depth>5||pk3_count>=MAX_PK3)return;
 DIR *d=opendir(dir);if(!d)return;struct dirent *e;
 while((e=readdir(d))&&pk3_count<MAX_PK3){
  if(e->d_name[0]=='.')continue;
  char full[512];int n=snprintf(full,sizeof(full),"%s/%s",dir,e->d_name);
  if(n<=0||n>=512)continue;
  struct stat st;if(lstat(full,&st)||S_ISLNK(st.st_mode))continue;
  if(S_ISDIR(st.st_mode))scan_pk3_dir(full,depth+1);
  if(S_ISREG(st.st_mode)&&(st.st_size==80||st.st_size==100)){
   size_t z=strlen(full);
   if(z>=4&&!strcasecmp(full+z-4,".pk3"))snprintf(pk3_files[pk3_count++],512,"%s",full);
  }
 }
 closedir(d);
}
static void scan_pk3(void){
 pk3_count=0;import_cursor=0;
 const char *roots[]={"/run/muos/storage/application/PokemonSaveBrowser/events",
  "/run/muos/storage/application/PokemonEditor/events",
  "/mnt/mmc/PokemonEvents","/mnt/sdcard/PokemonEvents",
  "/run/muos/storage/roms/PokemonEvents"};
 for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);i++)scan_pk3_dir(roots[i],0);
}
static int import_pk3(void){
 if(!pk3_count||import_cursor>=pk3_count||save_game<1)return 0;
 uint8_t mon[80];FILE *f=fopen(pk3_files[import_cursor],"rb");if(!f)return 0;
 int ok=fread(mon,1,80,f)==80;fclose(f);
 if(!ok||!validate_box_mon(mon))return 0;
 int sp=(int)((rd32(mon+32+growth_pos(rd32(mon)%24)*12)^(rd32(mon)^rd32(mon+4)))&65535);
 if(sp<1||sp>411)return 0;
 for(int b=0;b<14;b++)for(int i=0;i<30;i++){
  uint8_t target[80];if(!box_read(b,i,target))return 0;
  if(rd32(target)==0&&rd32(target+4)==0){
   if(!storage_write(4+(b*30+i)*80,mon,80))return 0;
   recompute_pc();dirty=1;return 1;
  }
 }
 return 0;
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
 const char *names[]={"BAG","PC","EGGS","EVENTS","ENCOUNTR","HEALTH"};
 for(int i=0;i<6;i++){
  if(tab==i)fill(r,5+i*105,67,103,37,36,98,116);
  label(r,10+i*105,79,names[i],2);
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
     if(load(files[selected])){detail=1;tab=0;status=0;bag_index=0;bag_qty=50;catalog_game=(save_game==1?0:save_game==4?2:1);event_index=0;bag_raw_mode=0;creator_dex=151;box_num=0;box_slot=0;edit_field=0;}
     else status=3;
    }
    continue;
   }
   if(b==SDL_CONTROLLER_BUTTON_Y){confirming=dirty&&save_game>0;status=save_game==0?15:dirty?0:8;continue;}
   if(b==SDL_CONTROLLER_BUTTON_B){if(dirty){status=14;}else{detail=0;status=0;}continue;}
   if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){tab=(tab+5)%6;status=0;continue;}
   if(b==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){tab=(tab+1)%6;status=0;continue;}
   if(save_game==0 && tab!=5){status=15;continue;}
   if(tab==0){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)bag_index=(bag_index+1)%(bag_raw_mode?575:bag_choice_count());
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)bag_index=(bag_index+(bag_raw_mode?575:bag_choice_count())-1)%(bag_raw_mode?575:bag_choice_count());
    if(b==SDL_CONTROLLER_BUTTON_START){bag_raw_mode=!bag_raw_mode;bag_index=0;}
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&bag_qty>1)bag_qty--;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&bag_qty<999)bag_qty++;
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
    if(b==SDL_CONTROLLER_BUTTON_START){creator_mode=(creator_mode+1)%3;if(creator_mode==2)scan_pk3();}
    else if(creator_mode==0){
     if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)creator_field=(creator_field+1)%12;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)creator_field=(creator_field+11)%12;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT)creator_change(creator_field,-1);
     if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)creator_change(creator_field,1);
     if(b==SDL_CONTROLLER_BUTTON_X)creator_shiny=!creator_shiny;
     if(b==SDL_CONTROLLER_BUTTON_A)status=creator_generate(creator_dex,0)?13:5;
    }else if(creator_mode==1){
     if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)event_index=(event_index+1)%EVENT_COUNT;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)event_index=(event_index+EVENT_COUNT-1)%EVENT_COUNT;
     if(b==SDL_CONTROLLER_BUTTON_A)status=event_spawn()?13:5;
    }else{
     if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&import_cursor<pk3_count-1)import_cursor++;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&import_cursor>0)import_cursor--;
     if(b==SDL_CONTROLLER_BUTTON_A)status=import_pk3()?13:5;
    }
   }else if(tab==4){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)enc_field=(enc_field+1)%2;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)enc_field=(enc_field+1)%2;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT){
     if(enc_field==0&&enc_dex>1)enc_dex--;
     if(enc_field==1&&enc_level>1)enc_level--;
    }
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){
     if(enc_field==0&&enc_dex<386)enc_dex++;
     if(enc_field==1&&enc_level<99)enc_level++;
    }
    if(b==SDL_CONTROLLER_BUTTON_START)enc_dex=(enc_dex==151?385:151);
    if(b==SDL_CONTROLLER_BUTTON_A)status=export_encounter()?16:17;
   }else if(tab==5){
    if(b==SDL_CONTROLLER_BUTTON_A)status=backup()?1:2;
   }
  }
  SDL_SetRenderDrawColor(r,12,21,35,255);SDL_RenderClear(r);
  fill(r,0,0,640,58,20,67,80);
  label(r,17,17,"POKEMON EDITOR V1.0 TEST",3);
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
   label(r,18,440,"R S E FR LG - BACK UP FIRST",2);
  }else{
   tabline(r);
   if(tab==5){
    panel(r,12,116,616,283);
    const char *games[]={"UNIDENTIFIED","RUBY SAPPHIRE","FIRERED","LEAFGREEN","EMERALD"};
    label(r,26,139,"SAVE HEALTH CHECK",3);
    label(r,26,183,games[save_game],2);
    label(r,26,215,save_slot_valid[0]?"SLOT A CHECKSUM: VALID":"SLOT A CHECKSUM: BAD",2);
    label(r,26,245,save_slot_valid[1]?"SLOT B CHECKSUM: VALID":"SLOT B CHECKSUM: BAD",2);
    label(r,26,275,save_game>0?"EDIT MODE - BACK UP BEFORE SAVING":"READ ONLY - EDITING DISABLED",2);
    label(r,26,316,dirty?"UNSAVED MODIFICATIONS":"NO UNSAVED CHANGES",2);
    label(r,26,357,"A MAKE NUMBERED BACKUP",2);
   }else if(tab==4){
    panel(r,12,116,616,304);
    label(r,27,137,"WILD ENCOUNTER GENERATOR",3);
    label(r,27,185,"FIRERED / LEAFGREEN",2);
    if(save_game==2||save_game==3){
     if(enc_field==0)fill(r,20,215,595,30,38,97,119);
     number(r,29,222,"NATIONAL DEX: %d",enc_dex);
     if(enc_field==1)fill(r,20,255,595,30,38,97,119);
     number(r,29,264,"ENCOUNTER LEVEL: %d",enc_level);
     label(r,27,315,"A INSTALL GAME CHEAT FILE",2);
     label(r,27,346,"UP DOWN FIELD  LEFT RIGHT VALUE",2);
     label(r,27,378,"SET RETROARCH AUTO APPLY ONCE",2);
    }else{
     label(r,27,227,"GAME NOT YET SUPPORTED",2);
     label(r,27,284,"NO CODES EXPORTED FOR THIS SAVE",2);
    }
   }else if(save_game==0){
    panel(r,12,116,616,280);
    label(r,26,150,"SAVE OPENED - READ ONLY",3);
    label(r,26,213,"GAME EDITING NOT YET VALIDATED",2);
    label(r,26,250,"HEALTH TAB SHOWS SAVE STATUS",2);
    label(r,26,290,"YOU CAN MAKE A BACKUP THERE",2);
   }else if(tab==0){
    panel(r,12,116,616,274);
    label(r,26,138,"BAG STORAGE",3);
    label(r,26,177,bag_raw_mode?"ALL ITEM IDS - EXPERT MODE":catalog_names[catalog_game],2);
    if(bag_raw_mode)number(r,26,218,"ITEM ID: %d",bag_index+1);
    else label(r,26,218,bag_choice()->name,3);
    number(r,460,170,"ITEM %d",bag_index+1);
    number(r,460,197,"OF %d",bag_raw_mode?575:bag_choice_count());
    number(r,26,275,"ADD AMOUNT: %d",bag_qty);
    label(r,26,329,"START CATALOG RAW IDS  UP DOWN",2);
    label(r,26,352,"A ADD ITEM TO SELECTED GAME",2);
    label(r,26,375,"RAW IDS MAY BE INVALID IN GAME",2);
   }else if(tab==3){
    panel(r,12,115,616,306);
    label(r,24,135,creator_mode==0?"NEW POKEMON":creator_mode==1?"EVENT RECREATIONS":"IMPORT ARCHIVED PK3",3);
    if(creator_mode==2){
     number(r,24,196,"PK3 FILES FOUND: %d",pk3_count);
     if(pk3_count){const char *fn=strrchr(pk3_files[import_cursor],'/');
      label(r,24,233,fn?fn+1:pk3_files[import_cursor],2);}
     else label(r,24,233,"PLACE PK3 IN POKEMONEVENTS",2);
     label(r,24,315,"UP DOWN SELECT  A IMPORT",2);
    }else if(creator_mode==1){
     label(r,24,190,event_pokemon[event_index].name,3);
     number(r,24,241,"NATIONAL DEX: %d",event_pokemon[event_index].dex);
     label(r,24,287,"RECONSTRUCTED - NOT ORIGINAL",2);
     label(r,24,327,"UP DOWN EVENT   A CREATE",2);
    }else{
     const char *names[]={"SPECIES DEX","APPROX LEVEL","NATURE","ABILITY SLOT","HELD ITEM","MOVE 1","MOVE 2","MOVE 3","MOVE 4","ALL IVS","ALL EVS","SHINY"};
     int vals[]={creator_dex,creator_level,creator_nature,creator_ability,creator_item,creator_moves[0],creator_moves[1],creator_moves[2],creator_moves[3],creator_iv,creator_ev,creator_shiny};
     int first=creator_field>=6?6:0;
     for(int j=first;j<first+6;j++){
      int y=177+(j-first)*34;
      if(j==creator_field)fill(r,18,y-5,600,29,40,96,118);
      label(r,28,y,names[j],2);
      number(r,390,y,"%d",vals[j]);
     }
     label(r,24,393,"UP DOWN FIELD  LEFT RIGHT VALUE",2);
    }
    label(r,16,431,"A CREATE / IMPORT   START MODE",2);
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
   if(tab==5)label(r,16,441,"A BACKUP   L R TABS",2);
   else if(tab==4)label(r,16,441,"A EXPORT .TXT   L R TABS",2);
   else if(tab==3)label(r,16,451,"X SHINY IN CREATOR",2);
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
   const char *msgs[]={"","BACKUP CREATED","BACKUP FAILED","INVALID SAVE","SAVED TO GAME FILE","OPERATION FAILED","FOLDER FOUND","FOLDER EMPTY","NOTHING TO SAVE","UNSAVED CHANGES LOST","CHANGE STAGED","EGG ADDED TO PC","POKEMON DELETED","EVENT MON STAGED","SAVE OR RELOAD BEFORE EXIT","READ ONLY SAVE","GAME CHEAT FILE INSTALLED","NO MATCHING CHEAT DIR OR FILE EXISTS"};
   if(status>=1&&status<=17){fill(r,6,456,628,23,23,57,68);label(r,13,460,msgs[status],2);}
  }else if(detail&&!confirming)label(r,520,460,dirty?"UNSAVED":"CLEAN",2);
  SDL_RenderPresent(r);SDL_Delay(16);
 }
 SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
