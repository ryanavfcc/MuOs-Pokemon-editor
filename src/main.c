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
static int dirty=0, confirming=0, odds_index=0, egg_species=1, edit_field=0, pokemon_editor_open=0, folder_view=0, root_index=0, party_slot=0, editor_party=0, egg_from_scratch=1;
static int catalog_game=0, delete_confirm=0, event_index=0, save_game=0, save_slot_valid[2]={0,0}, tool_cursor=0, creator_dex=151, bag_raw_mode=0, import_mode=0, import_cursor=0;
static char root_custom[512]="";
/* e-Reader workshop: exports editable card specifications only, not raw
   scanner-ready e-Reader dotcodes or unsupported save injections. */
static int ecard_type=0, ecard_field=0, ecard_dex=151, ecard_level=30;
static int ecard_berry=0, ecard_trainer=0, ecard_saved=0;
static const char *ecard_types[]={"EON TICKET REF","BATTLE E TRAINER","E CARD BERRY","CUSTOM TRAINER"};
static const char *ecard_berries[]={"PUMKIN","DRASH","EGGANT","STRIB","CHILAN","NUTPEA","GINEMA","KICE","YAGO","TOUGA"};
static const char *ecard_trainers[]={"YOUNGSTER","LASS","ACE TRAINER","POKEMON FAN","GENTLEMAN","POKEMANIAC"};


/* Card library: imports card files as inert data, never executes content. */
#include "bundled_me3.h"
#define MAX_ECARDS 256
static char ecard_files[MAX_ECARDS][512];
static int ecard_count=0,ecard_cursor=0,ecard_mode=0;
static const char *ecard_exts[]={".raw",".bin",".wc3",".me3",".ecb",".sav"};
static const char *ecard_card_roots[]={
 "/run/muos/storage/application/PokemonSaveBrowser/cards",
 "/mnt/mmc/PokemonEvents", "/mnt/sdcard/PokemonEvents",
 "/mnt/mmc/EREADER", "/mnt/sdcard/EREADER",
 "/run/muos/storage/roms/PokemonEvents"
};
static int ecard_extension(const char *name){
 size_t n=strlen(name);
 for(unsigned i=0;i<sizeof(ecard_exts)/sizeof(ecard_exts[0]);i++){
  size_t k=strlen(ecard_exts[i]);
  if(n>=k&&!strcasecmp(name+n-k,ecard_exts[i]))return 1;
 }
 return 0;
}
static void ecard_search(const char *dir,int depth){
 if(depth>3||ecard_count>=MAX_ECARDS)return;
 DIR *d=opendir(dir);if(!d)return;
 struct dirent *e;
 while((e=readdir(d))&&ecard_count<MAX_ECARDS){
  if(e->d_name[0]=='.')continue;
  char path[512];
  if(snprintf(path,sizeof(path),"%s/%s",dir,e->d_name)>=(int)sizeof(path))continue;
  struct stat st;if(lstat(path,&st))continue;
  if(S_ISLNK(st.st_mode))continue;
  if(S_ISDIR(st.st_mode))ecard_search(path,depth+1);
  else if(S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=1024*1024&&ecard_extension(path)){
   snprintf(ecard_files[ecard_count++],512,"%s",path);
  }
 }
 closedir(d);
}
static void ecard_scan(void){
 ecard_count=0;ecard_cursor=0;
 for(int i=0;i<EMBEDDED_EVENT_COUNT && ecard_count<MAX_ECARDS;i++){
  snprintf(ecard_files[ecard_count++],512,"BUILT-IN/%s.me3",embedded_events[i].name);
 }
 for(unsigned i=0;i<sizeof(ecard_card_roots)/sizeof(ecard_card_roots[0]);i++)
  ecard_search(ecard_card_roots[i],0);
}
static int ecard_import(void){
 if(ecard_count<=0||ecard_cursor>=ecard_count)return 0;
 const char *source=ecard_files[ecard_cursor];
 /* Existing in-app cards are already imported. */
 const char *targetdir=ecard_card_roots[0];
 if(strncmp(source,targetdir,strlen(targetdir))==0)return 1;
 if(mkdir(targetdir,0755)&&errno!=EEXIST)return 0;
 const char *base=strrchr(source,'/');base=base?base+1:source;
 if(strstr(base,"..")||strlen(base)>120)return 0;
 char dst[640];int found=0;
 for(int n=1;n<=999;n++){
  if(snprintf(dst,sizeof(dst),"%s/%03d-%s",targetdir,n,base)>=(int)sizeof(dst))return 0;
  if(access(dst,F_OK)!=0){found=1;break;}
 }
 if(!found)return 0;
 FILE *in=fopen(source,"rb");if(!in)return 0;
 int fd=open(dst,O_CREAT|O_EXCL|O_WRONLY,0644);
 if(fd<0){fclose(in);return 0;}
 char buf[4096];size_t n;int ok=1;size_t total=0;
 while((n=fread(buf,1,sizeof(buf),in))>0){
  total+=n;if(total>1024*1024){ok=0;break;}
  size_t off=0;while(off<n){ssize_t w=write(fd,buf+off,n-off);if(w<=0){ok=0;break;}off+=(size_t)w;}
  if(!ok)break;
 }
 if(ferror(in)||fsync(fd))ok=0;
 if(close(fd))ok=0;
 fclose(in);
 if(!ok)unlink(dst);
 return ok;
}
/* Ruby/Sapphire Mystery Event (.me3) injector.
 * Based on Gen3 Mystery Event save research: SaveBlock1 logical section 4,
 * script block offset 0x810, 1004 bytes including 4-byte header;
 * optional Record Mixing Gift at 0xBFC (8 bytes).
 * Game/version detection elsewhere currently uses the filename and therefore
 * needs manual review before release on unknown saves.
 */
#define ME3_SCRIPT_SIZE 1004
#define ME3_RECORD_SIZE 8
static int me3_validate(const uint8_t *b,size_t n){
 if(n!=ME3_SCRIPT_SIZE && n!=ME3_SCRIPT_SIZE+ME3_RECORD_SIZE)return 0;
 /* Ruby/Sapphire MysteryEvent3RS checksum: 16-bit byte sum over 1000 data bytes. */
 uint32_t sum=0;for(int i=4;i<ME3_SCRIPT_SIZE;i++)sum+=b[i];
 if((uint16_t)sum!=(uint16_t)(b[0]|(b[1]<<8)))return 0;
 if(n==ME3_SCRIPT_SIZE+ME3_RECORD_SIZE){
  uint32_t record=0;
  for(int i=ME3_SCRIPT_SIZE+4;i<ME3_SCRIPT_SIZE+8;i++)record+=b[i];
  if((uint16_t)record!=(uint16_t)(b[ME3_SCRIPT_SIZE]|(b[ME3_SCRIPT_SIZE+1]<<8)))return 0;
 }
 return 1;
}
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
/* Stage a validated ME3 event into a Ruby/Sapphire save. Do not write
 * the original file until the user uses the usual Y -> A save confirmation.
 * Do not apply scripts from unknown/untrusted sources. */
static int apply_me3_selected(void){
 if(save_game!=1||active<0||ecard_count<1||ecard_cursor>=ecard_count)return 0;
 const char *path=ecard_files[ecard_cursor];
 size_t l=strlen(path);
 if(l<4||strcasecmp(path+l-4,".me3"))return 0;
  uint8_t event[ME3_SCRIPT_SIZE+ME3_RECORD_SIZE];
  size_t n=0;
  if(!strncmp(path,"BUILT-IN/",9)){
   int found=0;
   for(int i=0;i<EMBEDDED_EVENT_COUNT;i++){
    char virtual_path[128];
    snprintf(virtual_path,sizeof(virtual_path),"BUILT-IN/%s.me3",embedded_events[i].name);
    if(!strcmp(path,virtual_path)){
     n=embedded_events[i].size;
     memcpy(event,embedded_events[i].bytes,n);
     found=1;break;
    }
   }
   if(!found)return 0;
  }else{
   struct stat st;
   if(stat(path,&st)||(st.st_size!=ME3_SCRIPT_SIZE&&st.st_size!=ME3_SCRIPT_SIZE+ME3_RECORD_SIZE))return 0;
   FILE *f=fopen(path,"rb");if(!f)return 0;
   n=fread(event,1,(size_t)st.st_size,f);
   int bad=ferror(f);fclose(f);
   if(bad||n!=(size_t)st.st_size)return 0;
  }
  if(!me3_validate(event,n))return 0;
 /* Script replacement is intentionally explicit. Existing Mystery Event
  * data is overwritten when staged; an untouched backup is made when saved. */
 uint8_t *section4=data+sec[4]*SECTION_SIZE;
 memcpy(section4+0x810,event,ME3_SCRIPT_SIZE);
 /* A script without a record-mixing gift must clear any previous gift. */
 if(n==ME3_SCRIPT_SIZE+ME3_RECORD_SIZE)
  memcpy(section4+0xBFC,event+ME3_SCRIPT_SIZE,ME3_RECORD_SIZE);
 else memset(section4+0xBFC,0,ME3_RECORD_SIZE);
 wr16(section4+0xFF6,checksum(section4,lengths[4]));
 /* Enable Mystery Event availability / Petalburg NPC in Ruby/Sapphire. */
 uint8_t *section2=data+sec[2]*SECTION_SIZE;
 section2[0x3A9]|=0x40;
 section2[0x5C0]|=0x04;
 wr16(section2+0xFF6,checksum(section2,lengths[2]));
 dirty=1;
 return 1;
}
static int party_offset(void){return (save_game==2||save_game==3)?0x34:0x234;}
static int party_count(void){
 int n=rd32(data+sec[1]*SECTION_SIZE+party_offset());
 return n>=0&&n<=6?n:0;
}
static uint8_t *party(int i){
 return data+sec[1]*SECTION_SIZE+party_offset()+4+i*100;
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
  if(field==0){
   int old=(int)rd16(plain+growth+off),dex=old<=251?old:old>=277&&old<=411?old-25:1;
   int next=dex+adjustment;if(next<1)next=386;if(next>386)next=1;
   v=next<=251?next:next+25;
  }
  int limit=field==0?411:376;
  if(v<1&&field==0)v=1;
  if(v<0)v=0;if(v>limit)v=limit;
  wr16(plain+growth+off,(uint16_t)v);
 }
 encrypt_mon(mon,plain);
 if(!storage_write(4+(box_num*30+box_slot)*80,mon,80))return 0;
 recompute_pc();dirty=1;return 1;
}

static int original_box_snapshot(uint8_t out[80]){
 int offset=4+(box_num*30+box_slot)*80;
 for(int i=0;i<80;i++){
  int sid=5+(offset+i)/3968, off=(offset+i)%3968;
  if(sid>13||off>=lengths[sid])return 0;
  out[i]=snapshot[sec[sid]*SECTION_SIZE+off];
 }
 return 1;
}
static int mon_shiny(const uint8_t mon[80]){
 uint32_t pid=rd32(mon),ot=rd32(mon+4);
 return (((pid&65535)^(pid>>16)^(ot&65535)^(ot>>16))<8);
}
/* New PID must preserve nature, shiny goal, and the four encrypted
 * substructures' logical order. This edits a BOXED Gen3 Pokemon only. */
static int set_mon_shiny(uint8_t mon[80],int want_shiny){
 uint32_t oldpid=rd32(mon),ot=rd32(mon+4),pid=oldpid;
 int nature=(int)(oldpid%25);
 if(mon_shiny(mon)==want_shiny)return 1;
 uint16_t tsv=(uint16_t)((ot&65535)^(ot>>16));
 uint8_t encrypted_plain[48],logical[48]={0},rearranged[48];
 static const char *orders[]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA",
 "AGEM","AGME","AEGM","AEMG","AMGE","AMEG",
 "EGAM","EGMA","EAGM","EAMG","EMGA","EMAG",
 "MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
 decrypt_mon(mon,encrypted_plain);
 const char *old=orders[oldpid%24];
 for(int i=0;i<4;i++){
  int block=old[i]=='G'?0:old[i]=='A'?1:old[i]=='E'?2:3;
  memcpy(logical+block*12,encrypted_plain+i*12,12);
 }
 /* deterministic candidate scan, no dependency on device RNG state */
 for(uint32_t trial=1;trial<1000000;trial++){
  uint32_t low=(oldpid+trial*2053u)&65535u;
  uint32_t high=want_shiny?(low^tsv^(trial&7u)):
                          ((oldpid>>16)+trial*2971u)&65535u;
  uint32_t candidate=(high<<16)|low;
  uint32_t sv=(low^high^tsv);
  if((int)(candidate%25)!=nature || ((sv<8)!=want_shiny))continue;
  pid=candidate;break;
 }
 if(pid==oldpid)return 0;
 const char *order=orders[pid%24];
 for(int i=0;i<4;i++){
  int block=order[i]=='G'?0:order[i]=='A'?1:order[i]=='E'?2:3;
  memcpy(rearranged+i*12,logical+block*12,12);
 }
 wr32(mon,pid);encrypt_mon(mon,rearranged);
 return validate_box_mon(mon)&&mon_shiny(mon)==want_shiny;
}
/* Shared boxed/party Pokemon field editor. Party records have 100 bytes,
   with the same first 80 bytes as boxed Pokemon. Party derived stats are
   retained; changing species may require an in-game recalculation. */
static int editor_read(uint8_t mon[80]){
 if(editor_party){
  if(party_slot>=party_count()||party_slot<0)return 0;
  memcpy(mon,party(party_slot),80);return 1;
 }
 return box_read(box_num,box_slot,mon);
}
static int editor_write(const uint8_t mon[80]){
 if(editor_party){
  if(party_slot>=party_count()||party_slot<0)return 0;
  memcpy(party(party_slot),mon,80);recompute_section(1);
 }else{
  if(!storage_write(4+(box_num*30+box_slot)*80,mon,80))return 0;
  recompute_pc();
 }
 dirty=1;return 1;
}
static int party_species_id(int slot){
 if(slot<0||slot>=party_count())return 0;
 uint8_t mon[80];memcpy(mon,party(slot),80);
 if(!validate_box_mon(mon))return 0;
 uint32_t key=rd32(mon)^rd32(mon+4);
 int pos=growth_pos(rd32(mon)%24);
 return (uint16_t)((rd32(mon+32+pos*12)^key)&65535);
}
static int box_current_shiny(void){
 uint8_t mon[80];
 return editor_read(mon)&&validate_box_mon(mon)?mon_shiny(mon):-1;
}
static int box_edit_shiny(void){
 uint8_t mon[80];
 if(!editor_read(mon)||!validate_box_mon(mon))return 0;
 if(!set_mon_shiny(mon,!mon_shiny(mon)))return 0;
 return editor_write(mon);
}

static int box_field_value(int field){
 uint8_t mon[80], plain[48];
 if(!box_read(box_num,box_slot,mon)||!validate_box_mon(mon))return -1;
 decrypt_mon(mon,plain);
 int off=growth_pos(rd32(mon)%24)*12;
 return field==0?rd16(plain+off):field==1?rd16(plain+off+2):plain[off+9];
}

static int dex_for_internal(int species);
static int dex_to_internal(int dex);
/* Dedicated boxed Pokemon editor: values are changed in memory, then saved
   through the existing original-file confirmation and backup routine. */
#define EDITOR_FIELD_COUNT 22
static const char *box_editor_labels[EDITOR_FIELD_COUNT]={
 "SPECIES","HELD ITEM","FRIENDSHIP","SHINY",
 "MOVE 1","MOVE 2","MOVE 3","MOVE 4",
 "IV HP","IV ATTACK","IV DEFENSE","IV SPEED","IV SP ATK","IV SP DEF",
 "EV HP","EV ATTACK","EV DEFENSE","EV SPEED","EV SP ATK","EV SP DEF",
 "ABILITY SLOT","DELETE POKEMON"
};
static const char *editor_blocks[24]={"GAEM","GAME","GEAM","GEMA","GMAE","GMEA", "AGEM","AGME","AEGM","AEMG","AMGE","AMEG", "EGAM","EGMA","EAGM","EAMG","EMGA","EMAG","MGAE","MGEA","MAGE","MAEG","MEGA","MEAG"};
static int editor_block(uint32_t pid,char type){
 const char *order=editor_blocks[pid%24];
 for(int i=0;i<4;i++)if(order[i]==type)return i*12;
 return -1;
}
static int box_detail_value(int field){
 uint8_t mon[80],plain[48];
 if(!editor_read(mon)||!validate_box_mon(mon))return -1;
 if(field==3)return mon_shiny(mon);
 if(field==21)return 1;
 decrypt_mon(mon,plain);
 int g=editor_block(rd32(mon),'G'),a=editor_block(rd32(mon),'A');
 int e=editor_block(rd32(mon),'E'),m=editor_block(rd32(mon),'M');
 if(g<0||a<0||e<0||m<0)return -1;
 if(field==0)return dex_for_internal(rd16(plain+g));
 if(field==1)return rd16(plain+g+2);
 if(field==2)return plain[g+9];
 if(field>=4&&field<=7)return rd16(plain+a+2*(field-4));
 if(field>=8&&field<=13)return (rd32(plain+m+4)>>(5*(field-8)))&31u;
 if(field>=14&&field<=19)return plain[e+field-14];
 if(field==20)return (rd32(plain+m+4)>>31)&1u;
 return -1;
}
static int box_detail_adjust(int field,int delta){
 if(field==3)return box_edit_shiny();
 if(field==21)return 0;
 uint8_t mon[80],plain[48];
 if(!editor_read(mon)||!validate_box_mon(mon))return 0;
 decrypt_mon(mon,plain);
 int g=editor_block(rd32(mon),'G'),a=editor_block(rd32(mon),'A');
 int e=editor_block(rd32(mon),'E'),m=editor_block(rd32(mon),'M');
 if(g<0||a<0||e<0||m<0)return 0;
 int val=box_detail_value(field);if(val<0)return 0;
 int low=0,high=255;
 if(field==0){low=1;high=386;}
 else if(field==1){high=376;}
 else if(field>=4&&field<=7){high=354;}
 else if(field>=8&&field<=13){high=31;}
 else if(field==20){high=1;}
 int next=val+delta;
 if(next<low)next=high;
 if(next>high)next=low;
 if(field==0)wr16(plain+g,(uint16_t)dex_to_internal(next));
 else if(field==1)wr16(plain+g+2,(uint16_t)next);
 else if(field==2)plain[g+9]=(uint8_t)next;
 else if(field>=4&&field<=7)wr16(plain+a+2*(field-4),(uint16_t)next);
 else if(field>=8&&field<=13){
  int shift=5*(field-8);uint32_t bits=rd32(plain+m+4);
  bits=(bits&~(31u<<shift))|((uint32_t)next<<shift);wr32(plain+m+4,bits);
 }else if(field>=14&&field<=19)plain[e+field-14]=(uint8_t)next;
 else if(field==20){uint32_t bits=rd32(plain+m+4);bits=(bits&0x7fffffffu)|((uint32_t)next<<31);wr32(plain+m+4,bits);}
 encrypt_mon(mon,plain);
 if(!validate_box_mon(mon))return 0;
 return editor_write(mon);
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
 if(save_game<1||editor_party)return 0;
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
static int creator_generate_internal(int dex,int reconstructed,int make_egg){
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
 gba_name(mon+8,10,make_egg?"EGG":nick);gba_name(mon+20,7,"EDITOR");
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
 plain[growth+9]=(uint8_t)(make_egg?20:70); /* friendship / hatch cycles */
 /* Order GAEM, chosen through PID %24 == 0? PID permutation may vary.
    Reorder logical G/A/E/M into permutation dictated by PID. */
 uint8_t logical[48];memcpy(logical,plain,48);
 /* Attacks substructure occupies logical bytes 12..23. */
 for(int i=0;i<4;i++)wr16(logical+12+i*2,(uint16_t)creator_moves[i]);
 for(int i=0;i<4;i++)logical[20+i]=creator_moves[i]?35:0;
 for(int i=0;i<6;i++)logical[24+i]=(uint8_t)creator_ev;
 /* IVs packed, 6x 5-bit; personality controls nature. */
 uint32_t iv=0;for(int i=0;i<6;i++)iv|=(uint32_t)creator_iv<<(5*i);
 wr32(logical+40,iv | (make_egg?(1u<<30):0));
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
static int creator_generate(int dex,int reconstructed){return creator_generate_internal(dex,reconstructed,0);}
static int egg_generate_fresh(void){return creator_generate_internal(egg_species,0,1);}
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

/* The sprite atlas is baked into the executable at build time.
   Gen VII pixel box icons (40x30) for National Dex 1..386.
   Conversion is done once at app start; one small SDL texture is used for all. */
#include "pokemon_icon_data.h"
#include "pokemon_names_data.h"
#define MINI_COLUMNS 16
#define MINI_ROWS 25
static SDL_Texture *mini_atlas=NULL;
static int dex_for_internal(int species){
 if(species>=1&&species<=251)return species;
 if(species>=277&&species<=411)return species-25;
 return 0;
}
static int init_mini_atlas(SDL_Renderer *renderer){
 const int stride=MINI_ICON_WIDTH*MINI_COLUMNS;
 const int height=MINI_ICON_HEIGHT*MINI_ROWS;
 uint32_t *rgba=(uint32_t *)calloc((size_t)stride*height,sizeof(uint32_t));
 if(!rgba)return 0;
 for(int dex=1;dex<=386;dex++){
  int x0=((dex-1)%MINI_COLUMNS)*MINI_ICON_WIDTH;
  int y0=((dex-1)/MINI_COLUMNS)*MINI_ICON_HEIGHT;
  const unsigned short *pix=mini_icon_data[dex-1];
  for(int y=0;y<MINI_ICON_HEIGHT;y++)for(int x=0;x<MINI_ICON_WIDTH;x++){
   uint16_t v=pix[y*MINI_ICON_WIDTH+x];
   if(!(v&0x8000))continue;
   uint8_t rr=(uint8_t)((v>>10)&31),gg=(uint8_t)((v>>5)&31),bb=(uint8_t)(v&31);
   rgba[(y0+y)*stride+x0+x]=0xff000000u | ((uint32_t)((rr<<3)|(rr>>2))<<16)
       |((uint32_t)((gg<<3)|(gg>>2))<<8)|((uint32_t)((bb<<3)|(bb>>2)));
  }
 }
 mini_atlas=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STATIC,stride,height);
 int ok=mini_atlas && SDL_UpdateTexture(mini_atlas,NULL,rgba,stride*4)==0;
 free(rgba);
 if(!ok){if(mini_atlas)SDL_DestroyTexture(mini_atlas);mini_atlas=NULL;return 0;}
 SDL_SetTextureBlendMode(mini_atlas,SDL_BLENDMODE_BLEND);
 SDL_SetTextureScaleMode(mini_atlas,SDL_ScaleModeNearest);
 return 1;
}
static void pixel_mon(SDL_Renderer *renderer,int x,int y,int species,int scale){
 int dex=dex_for_internal(species);
 if(!mini_atlas||dex<1)return;
 SDL_Rect source={((dex-1)%MINI_COLUMNS)*MINI_ICON_WIDTH,
                  ((dex-1)/MINI_COLUMNS)*MINI_ICON_HEIGHT,
                  MINI_ICON_WIDTH,MINI_ICON_HEIGHT};
 /* 40x30 pixel icons: near native size, preserving crisp pixels. */
 SDL_Rect target={x,y,MINI_ICON_WIDTH*scale,MINI_ICON_HEIGHT*scale};
 SDL_RenderCopy(renderer,mini_atlas,&source,&target);
}
static void label_color(SDL_Renderer *r,int x,int y,const char *s,int scale,int rr,int gg,int bb){
 for(;*s&&x<633-6*scale;s++,x+=6*scale){
  char c=*s;if(c>='a'&&c<='z')c-=32;
  const char *q=strchr(abc,c);int idx=q?(int)(q-abc):0;
  if(idx>=(int)(sizeof(glyph)/sizeof(glyph[0])))idx=0;
  for(int row=0;row<7;row++)for(int col=0;col<5;col++)
   if(glyph[idx][row]&(1<<(4-col)))fill(r,x+col*scale,y+row*scale,scale,scale,rr,gg,bb);
 }
}
static void muted(SDL_Renderer*r,int x,int y,const char*s,int z){label_color(r,x,y,s,z,148,166,190);}
static void aqua(SDL_Renderer*r,int x,int y,const char*s,int z){label_color(r,x,y,s,z,99,231,213);}
static void panel(SDL_Renderer *r,int x,int y,int w,int h){
 fill(r,x+2,y+3,w,h,7,11,22);
 fill(r,x,y,w,h,25,36,56);
 fill(r,x,y,w,2,48,66,91);
 fill(r,x,y,2,h,48,66,91);
}
static void tile(SDL_Renderer*r,int x,int y,int w,int h,int on){
 fill(r,x,y,w,h,on?35:30,on?72:45,on?88:65);
 fill(r,x,y,w,2,on?90:46,on?230:69,on?208:91);
 if(on){fill(r,x,y,3,h,97,228,207);fill(r,x+3,y+h-2,w-3,2,47,122,127);}
}
static void head(SDL_Renderer*r,const char*title,const char*sub){
 label(r,171,16,title,3);muted(r,171,45,sub,2);
}
static void dashboard(SDL_Renderer*r){
 fill(r,0,0,640,480,11,18,31);
 fill(r,0,0,149,480,17,27,43);
 fill(r,149,0,2,480,44,64,84);
 fill(r,0,0,640,4,87,212,193);
 aqua(r,14,18,"POKEMON",3);
 label(r,14,48,"EDITOR",3);
 muted(r,14,80,"MUOS  V1.4",2);
 const char *names[]={"BAG","PC BOX","PARTY","EGG LAB","EVENTS","E READER","HEALTH"};
 for(int i=0;i<7;i++){
  int y=125+i*43;
  tile(r,8,y,133,38,tab==i);
  if(tab==i)aqua(r,20,y+12,names[i],2);else muted(r,20,y+12,names[i],2);
 }
 muted(r,11,438,"L/R TABS",2);
 fill(r,151,74,489,1,42,63,83);
}
static void bottombar(SDL_Renderer*r,const char*msg){
 fill(r,151,441,489,39,16,27,42);
 fill(r,151,441,489,1,46,72,93);
 muted(r,161,451,msg,2);
}
static void sectiontitle(SDL_Renderer*r,int x,int y,const char*s){
 fill(r,x,y+1,4,21,86,224,199);label(r,x+12,y,s,2);
}
static void smallspecies(SDL_Renderer*r,int x,int y,int sid){
 int d=dex_for_internal(sid);
 if(d>0)pixel_mon(r,x,y,sid,1);
 else muted(r,x+9,y+9,"--",2);
}
static void draw_party(SDL_Renderer*r){
 head(r,"PARTY EDITOR","EDIT YOUR ACTIVE TEAM");
 panel(r,163,91,465,334);
 for(int i=0;i<6;i++){
  int x=173+(i%2)*226,y=103+(i/2)*101;
  tile(r,x,y,216,91,i==party_slot);
  int id=party_species_id(i),dex=dex_for_internal(id);
  smallspecies(r,x+9,y+12,id);
  if(dex>0){label(r,x+57,y+14,pokemon_names[dex],2);muted(r,x+57,y+47,"PRESS A TO EDIT",1);}
  else muted(r,x+57,y+23,"EMPTY SLOT",2);
  muted(r,x+9,y+70,"PARTY SLOT",1);
  number(r,x+161,y+70,"%d",i+1);
 }
 bottombar(r,"D-PAD SELECT   A EDIT   Y SAVE");
}
static void draw_box(SDL_Renderer*r){
 head(r,"PC STORAGE","CHOOSE A SLOT TO EDIT OR CREATE");
 panel(r,163,91,465,334);
 sectiontitle(r,176,104,"POKEMON STORAGE");
 number(r,517,104,"BOX %d",box_num+1);
 for(int i=0;i<30;i++){
  int x=175+(i%6)*74,y=142+(i/6)*47;
  tile(r,x,y,68,42,i==box_slot);
  int sid=box_species_id(box_num,i);
  smallspecies(r,x+3,y+6,sid);
  if(sid>0){int dex=dex_for_internal(sid);if(dex>0)number(r,x+43,y+26,"%d",dex);}
 }
 fill(r,173,384,449,35,18,32,48);
 int id=box_species_id(box_num,box_slot),dex=dex_for_internal(id);
 if(dex>0){aqua(r,182,395,pokemon_names[dex],2);muted(r,451,396,"A EDIT POKEMON",1);}
 else{aqua(r,182,395,"EMPTY SLOT",2);muted(r,451,396,"A CREATE",1);}
 bottombar(r,"D-PAD SLOT  START NEXT BOX  A OPEN");
}
static void draw_bag(SDL_Renderer*r){
 head(r,"BAG EDITOR","ITEMS, BALLS AND KEY ITEMS");
 panel(r,163,91,465,334);
 sectiontitle(r,178,111,"ITEM DIRECTORY");
 muted(r,180,153,bag_raw_mode?"ALL ITEM IDS":"GAME CATALOGUE",2);
 tile(r,178,183,436,77,1);
 if(bag_raw_mode)number(r,192,201,"ITEM ID: %d",bag_index+1);
 else label(r,192,201,bag_choice()->name,3);
 muted(r,192,239,catalog_names[catalog_game],1);
 muted(r,180,283,"QUANTITY TO ADD",2);
 number(r,475,283,"%d",bag_qty);
 fill(r,179,320,435,1,49,75,94);
 muted(r,180,337,"UP/DOWN ITEM   LEFT/RIGHT COUNT",1);
 muted(r,180,366,"A ADD ITEM   START ALL ITEM IDS",1);
 bottombar(r,"A ADD ITEM   Y SAVE   L/R TABS");
}
static void draw_eggs(SDL_Renderer*r){
 head(r,"EGG LAB","CREATE EGGS WITHOUT A TEMPLATE");
 panel(r,163,91,465,334);
 tile(r,175,104,166,218,1);
 pixel_mon(r,196,133,dex_to_internal(egg_species),3);
 aqua(r,187,257,pokemon_names[egg_species],2);
 muted(r,185,291,"EGG SPECIES",1);
 sectiontitle(r,355,116,"EGG SETTINGS");
 muted(r,359,166,"METHOD",2);
 label(r,359,189,egg_from_scratch?"FROM SCRATCH":"CLONE BOX MON",2);
 muted(r,359,238,"SHINY ODDS",2);
 aqua(r,359,266,egg_odds_label[odds_index],2);
 fill(r,355,311,255,1,51,76,98);
 muted(r,179,345,"LEFT/RIGHT SPECIES",2);
 muted(r,179,379,"X ODDS  A CREATE",2);
 bottombar(r,"A CREATE EGG   X SHINY ODDS");
}
static void draw_events(SDL_Renderer*r){
 head(r,"POKEMON LAB","CREATE, IMPORT OR USE AN EVENT");
 panel(r,163,91,465,334);
 const char *modes[]={"CREATE POKEMON","EVENT PRESETS","IMPORT PK3"};
 for(int i=0;i<3;i++){tile(r,174+i*149,104,141,32,creator_mode==i);muted(r,180+i*149,115,modes[i],1);}
 if(creator_mode==1){
  sectiontitle(r,182,163,"HISTORICAL RECREATION");
  label(r,183,213,event_pokemon[event_index].name,2);
  pixel_mon(r,485,195,dex_to_internal(event_pokemon[event_index].dex),2);
  muted(r,183,281,"RECONSTRUCTED NOT ORIGINAL",1);
  muted(r,183,318,"UP/DOWN EVENT   A GENERATE",2);
 }else if(creator_mode==2){
  sectiontitle(r,183,163,"PK3 IMPORT");
  number(r,183,221,"FILES FOUND: %d",pk3_count);
  if(pk3_count){const char*fn=strrchr(pk3_files[import_cursor],'/');muted(r,183,269,fn?fn+1:pk3_files[import_cursor],1);}
  else muted(r,183,269,"NO PK3 FILES FOUND",2);
  muted(r,183,334,"A IMPORT INTO PC",2);
 }else{
  const char *names[]={"SPECIES","LEVEL","NATURE","ABILITY","ITEM","MOVE 1","MOVE 2","MOVE 3","MOVE 4","IV ALL","EV ALL","SHINY"};
  int vals[]={creator_dex,creator_level,creator_nature,creator_ability,creator_item,creator_moves[0],creator_moves[1],creator_moves[2],creator_moves[3],creator_iv,creator_ev,creator_shiny};
  int first=creator_field>=6?6:0;
  for(int j=first;j<first+6;j++){
   int y=159+(j-first)*39;tile(r,175,y,278,34,j==creator_field);
   muted(r,187,y+10,names[j],2);number(r,394,y+10,"%d",vals[j]);
  }
  tile(r,465,160,150,235,0);
  pixel_mon(r,480,185,dex_to_internal(creator_dex),2);
  aqua(r,475,265,pokemon_names[creator_dex],1);
  muted(r,475,306,creator_shiny?"SHINY":"NORMAL",2);
  muted(r,475,350,"A CREATE",2);
 }
 bottombar(r,"START MODE   D-PAD EDIT   A APPLY");
}

static int export_ecard_definition(void){
 if(selected<0||selected>=count||save_game!=1&&save_game!=4)return 0;
 char target[640];
 if(!unique_path(files[selected],"ereader-card.txt",target,sizeof(target)))return 0;
 /* Only an editable descriptor. No GBA save bytes are changed. */
 FILE *f=fopen(target,"wx");if(!f)return 0;
 int ok=fprintf(f,
  "POKEMON EDITOR E-READER CARD SPEC V1\n"
  "Type: %s\n"
  "Game: %s\n"
  "Compatibility: %s\n"
  "Pokemon National Dex: %d\n"
  "Pokemon Name: %s\n"
  "Level: %d\n"
  "Trainer class: %s\n"
  "Berry: %s\n"
  "Status: DESIGN ONLY - NOT SCANNABLE; DOES NOT ACTIVATE AN EVENT\n",
  ecard_types[ecard_type],save_game==1?"RUBY / SAPPHIRE":"EMERALD",
  save_game==4?"EMERALD NEEDS COMPATIBLE ROM/CARD PIPELINE":"VERIFY CARD REGION AND VERSION",
  ecard_dex,pokemon_names[ecard_dex],ecard_level,
  ecard_trainers[ecard_trainer],ecard_berries[ecard_berry])>0;
 if(fflush(f)!=0)ok=0;
 if(fclose(f)!=0)ok=0;
 if(!ok)unlink(target);
 ecard_saved=ok;
 return ok;
}
static void adjust_ecard(int delta){
 switch(ecard_field){
  case 0: ecard_type=(ecard_type+4+delta)%4;break;
  case 1: ecard_dex=(ecard_dex-1+386+delta)%386+1;break;
  case 2: ecard_level=(ecard_level-1+100+delta)%100+1;break;
  case 3: ecard_trainer=(ecard_trainer+6+delta)%6;break;
  case 4: ecard_berry=(ecard_berry+10+delta)%10;break;
 }
}
static void draw_ereader(SDL_Renderer*r){
 head(r,"E READER WORKSHOP",ecard_mode?"IMPORTED AND CUSTOM CARD FILES":"DESIGN CUSTOM EVENT REFERENCES");
 panel(r,163,91,465,334);
 if(ecard_mode){
  sectiontitle(r,178,107,"BUILT-IN EVENTS + SD CARDS");
  number(r,469,109,"FILES %d",ecard_count);
  int first=ecard_cursor>6?ecard_cursor-6:0;
  for(int i=first;i<ecard_count&&i<first+7;i++){
   int y=141+(i-first)*32;
   tile(r,177,y,437,29,i==ecard_cursor);
   const char *name=strrchr(ecard_files[i],'/');name=name?name+1:ecard_files[i];
   char shortname[49];snprintf(shortname,sizeof(shortname),"%.44s",name);
   muted(r,185,y+8,shortname,1);
  }
  if(!ecard_count)muted(r,187,197,"NO CARD FILES FOUND",2);
  if(ecard_count>0){
   const char *p=ecard_files[ecard_cursor];size_t n=strlen(p);
   if(n>=4&&!strcasecmp(p+n-4,".me3")&&save_game==1)
    aqua(r,182,359,"A STAGE ME3 EVENT",2);
   else muted(r,182,359,"NOT INJECTABLE AS RS ME3",1);
  }
  muted(r,182,385,"X COPY CARD TO LIBRARY",1);
  bottombar(r,"START DESIGN  A STAGE EVENT  Y SAVE");
 }else{
  sectiontitle(r,178,109,"DESIGN A CUSTOM CARD");
  const char *fields[]={"CARD TYPE","POKEMON","LEVEL","TRAINER CLASS","E CARD BERRY"};
  for(int i=0;i<5;i++){
   int y=145+i*47;
   tile(r,177,y,435,40,i==ecard_field);
   muted(r,186,y+7,fields[i],1);
   if(i==0)aqua(r,186,y+20,ecard_types[ecard_type],2);
   else if(i==1)aqua(r,186,y+20,pokemon_names[ecard_dex],2);
   else if(i==2)number(r,186,y+20,"LEVEL %d",ecard_level);
   else if(i==3)aqua(r,186,y+20,ecard_trainers[ecard_trainer],2);
   else aqua(r,186,y+20,ecard_berries[ecard_berry],2);
  }
  muted(r,179,392,"A EXPORT DESIGN FILE",2);
  bottombar(r,"START LIBRARY  D-PAD EDIT  A EXPORT");
 }
}

static void draw_health(SDL_Renderer*r){
 head(r,"SAVE HEALTH","CHECK DATA AND BACK UP SAFELY");
 panel(r,163,91,465,334);
 const char*games[]={"UNKNOWN","RUBY / SAPPHIRE","FIRERED","LEAFGREEN","EMERALD"};
 sectiontitle(r,178,111,"GAME DETECTED");
 aqua(r,183,151,games[save_game],2);
 tile(r,178,198,436,55,save_slot_valid[0]);
 label(r,192,216,save_slot_valid[0]?"SAVE SLOT A  VALID":"SAVE SLOT A  INVALID",2);
 tile(r,178,263,436,55,save_slot_valid[1]);
 label(r,192,280,save_slot_valid[1]?"SAVE SLOT B  VALID":"SAVE SLOT B  INVALID",2);
 muted(r,180,344,dirty?"CHANGES WAITING TO SAVE":"NO UNSAVED CHANGES",2);
 muted(r,180,380,"A CREATE BACKUP",2);
 bottombar(r,"A BACKUP   Y SAVE CHANGES");
}
static void draw_editor(SDL_Renderer*r){
 fill(r,151,0,489,480,11,18,31);
 head(r,"EDIT POKEMON","CHANGE DETAILS THEN PRESS Y TO SAVE");
 panel(r,162,91,188,338);
 panel(r,359,91,270,338);
 int dex=box_detail_value(0);
 if(dex>0&&dex<=386){
  pixel_mon(r,181,133,dex_to_internal(dex),3);
  aqua(r,174,262,pokemon_names[dex],2);
  muted(r,175,298,box_current_shiny()==1?"SHINY":"NORMAL",2);
 }
 muted(r,174,350,editor_party?"PARTY POKEMON":"BOX POKEMON",2);
 muted(r,174,387,"B RETURN",2);
 const char *groups[]={"IDENTITY / ITEMS","MOVES / STATS","IVS / EVS","OTHER"};
 int page=edit_field/6;
 if(page>3)page=3;
 sectiontitle(r,369,106,groups[page]);
 int start=page*6,stop=start+6;
 if(stop>EDITOR_FIELD_COUNT)stop=EDITOR_FIELD_COUNT;
 for(int i=start;i<stop;i++){
  int y=148+(i-start)*45;
  tile(r,369,y,249,39,i==edit_field);
  muted(r,377,y+7,box_editor_labels[i],1);
  if(i!=21){
   int v=box_detail_value(i);
   if(i==0&&v>0&&v<=386)aqua(r,377,y+22,pokemon_names[v],1);
   else if(i==3)aqua(r,548,y+19,v?"YES":"NO",2);
   else number(r,553,y+19,"%d",v);
  }else muted(r,377,y+23,"PRESS A TO DELETE",1);
 }
 bottombar(r,"UP/DOWN FIELD  LEFT/RIGHT EDIT  Y SAVE");
}
static void draw_browser(SDL_Renderer*r){
 dashboard(r);head(r,folder_view?"SAVE FOLDERS":"SELECT A SAVE",folder_view?"PICK A RETROARCH DIRECTORY":"OPEN A GAME SAVE TO BEGIN");
 panel(r,164,91,464,335);
 if(folder_view){
  const char*cp=strrchr(current_path,'/');aqua(r,176,108,cp&&cp[1]?cp+1:current_path,2);
  int first=directory_cursor>7?directory_cursor-7:0;
  for(int i=first,y=143;i<ndirs&&y<405;i++,y+=33){
   tile(r,175,y,440,29,i==directory_cursor);
   const char*n=strrchr(dirs[i],'/');muted(r,184,y+8,n?n+1:dirs[i],2);
  }
  bottombar(r,"A OPEN  B UP  Y SCAN  X CLOSE");
 }else{
  if(!count)muted(r,178,223,"NO SAVES FOUND - PRESS X",2);
  int first=selected>8?selected-8:0;
  for(int i=first,y=105;i<count&&y<406;i++,y+=35){
   tile(r,175,y,440,31,selected==i);
   const char*n=strrchr(files[i],'/');muted(r,181,y+10,n?n+1:files[i],1);
  }
  bottombar(r,"A OPEN   X FOLDERS   START RESCAN");
 }
}

int main(void){
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER))return 1;
 SDL_Window *w=SDL_CreateWindow("Pokemon Editor v1.6",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,480,SDL_WINDOW_FULLSCREEN_DESKTOP);
 if(!w){SDL_Quit();return 1;}
 SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);
 if(!r){SDL_DestroyWindow(w);SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(r,640,480);
 if(!init_mini_atlas(r)){fprintf(stderr,"Could not initialise PC sprite atlas: %s\n",SDL_GetError());}
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
     if(load(files[selected])){detail=1;tab=0;status=0;bag_index=0;bag_qty=50;catalog_game=(save_game==1?0:save_game==4?2:1);event_index=0;bag_raw_mode=0;creator_dex=151;box_num=0;box_slot=0;edit_field=0;pokemon_editor_open=0;}
     else status=3;
    }
    continue;
   }
   if(pokemon_editor_open){
    if(b==SDL_CONTROLLER_BUTTON_B){pokemon_editor_open=0;status=0;continue;}
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)edit_field=(edit_field+1)%EDITOR_FIELD_COUNT;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)edit_field=(edit_field+EDITOR_FIELD_COUNT-1)%EDITOR_FIELD_COUNT;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT &&edit_field!=21)status=box_detail_adjust(edit_field,-1)?10:5;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&edit_field!=21)status=box_detail_adjust(edit_field,1)?10:5;
    if(b==SDL_CONTROLLER_BUTTON_A){
     if(edit_field==21 && !editor_party)delete_confirm=1;
     else status=box_detail_adjust(edit_field,1)?10:5;
    }
    if(b!=SDL_CONTROLLER_BUTTON_Y)continue;
   }
   if(b==SDL_CONTROLLER_BUTTON_Y){confirming=dirty&&save_game>0;status=save_game==0?15:dirty?0:8;continue;}
   if(b==SDL_CONTROLLER_BUTTON_B){if(dirty){status=14;}else{detail=0;status=0;}continue;}
   if(b==SDL_CONTROLLER_BUTTON_LEFTSHOULDER){tab=(tab+6)%7;status=0;continue;}
   if(b==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER){tab=(tab+1)%7;status=0;continue;}
   if(save_game==0 && tab!=6){status=15;continue;}
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
    if(b==SDL_CONTROLLER_BUTTON_A||b==SDL_CONTROLLER_BUTTON_X){
     uint8_t mon[80];
     if(box_read(box_num,box_slot,mon)&&validate_box_mon(mon)){
      pokemon_editor_open=1;editor_party=0;edit_field=0;status=0;
     }else if(b==SDL_CONTROLLER_BUTTON_A){tab=4;creator_mode=0;status=0;}
     else status=5;
    }
   }else if(tab==2){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&party_slot<party_count()-1)party_slot++;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&party_slot>0)party_slot--;
    if(b==SDL_CONTROLLER_BUTTON_A&&party_count()>party_slot){
     uint8_t mon[80];memcpy(mon,party(party_slot),80);
     if(validate_box_mon(mon)){pokemon_editor_open=1;editor_party=1;edit_field=0;status=0;}
     else status=5;
    }
   }else if(tab==3){
    if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&box_slot<24)box_slot+=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&box_slot>=6)box_slot-=6;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT&&box_slot%6>0)box_slot--;
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT&&box_slot%6<5)box_slot++;
    if(b==SDL_CONTROLLER_BUTTON_START)box_num=(box_num+1)%14;
    if(b==SDL_CONTROLLER_BUTTON_X)odds_index=(odds_index+1)%8;
    if(b==SDL_CONTROLLER_BUTTON_BACK)egg_from_scratch=!egg_from_scratch;
    if(b==SDL_CONTROLLER_BUTTON_START){egg_species++;if(egg_species>386)egg_species=1;}
    if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT){egg_species--;if(egg_species<1)egg_species=386;}
    if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT){egg_species++;if(egg_species>386)egg_species=1;}
    if(b==SDL_CONTROLLER_BUTTON_A){
     int previous=creator_shiny;
     creator_shiny=(egg_rng()%egg_odds[odds_index])==0;
     status=(egg_from_scratch?egg_generate_fresh():egg_create())?11:5;
     creator_shiny=previous;
    }
   }else if(tab==4){
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
   }else if(tab==5){
    if(save_game!=1&&save_game!=4){status=15;continue;}
    if(b==SDL_CONTROLLER_BUTTON_START){ecard_mode=!ecard_mode;if(ecard_mode)ecard_scan();status=0;}
    else if(ecard_mode){
     if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&ecard_cursor<ecard_count-1)ecard_cursor++;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&ecard_cursor>0)ecard_cursor--;
     if(b==SDL_CONTROLLER_BUTTON_X){status=ecard_import()?17:5;ecard_scan();}
     if(b==SDL_CONTROLLER_BUTTON_A){
      status=apply_me3_selected()?18:19;
     }
    }else{
     if(b==SDL_CONTROLLER_BUTTON_DPAD_UP)ecard_field=(ecard_field+4)%5;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN)ecard_field=(ecard_field+1)%5;
     if(b==SDL_CONTROLLER_BUTTON_DPAD_LEFT)adjust_ecard(-1);
     if(b==SDL_CONTROLLER_BUTTON_DPAD_RIGHT)adjust_ecard(1);
     if(b==SDL_CONTROLLER_BUTTON_A)status=export_ecard_definition()?16:5;
    }
   }else if(tab==6){
    if(b==SDL_CONTROLLER_BUTTON_A)status=backup()?1:2;
   }
  }
  SDL_SetRenderDrawColor(r,11,18,31,255);SDL_RenderClear(r);
  if(!detail || folder_view)draw_browser(r);
  else{
   dashboard(r);
   if(tab==0)draw_bag(r);
   else if(tab==1)draw_box(r);
   else if(tab==2)draw_party(r);
   else if(tab==3)draw_eggs(r);
   else if(tab==4)draw_events(r);
   else if(tab==5)draw_ereader(r);
   else draw_health(r);
   if(pokemon_editor_open)draw_editor(r);
  }
  if(delete_confirm){
   fill(r,167,157,455,174,13,37,54);
   fill(r,167,157,455,3,219,115,119);
   label(r,185,179,"DELETE POKEMON?",3);
   muted(r,185,235,"A DELETE  B CANCEL",2);
   muted(r,185,270,"SAVE NEEDED TO COMMIT",2);
  }
  if(confirming){
   fill(r,167,157,455,174,13,37,54);
   fill(r,167,157,455,3,99,231,213);
   label(r,185,179,"SAVE CHANGES?",3);
   muted(r,185,223,"CLOSE RETROARCH FIRST",2);
   muted(r,185,257,"BACKUP CREATED AUTOMATICALLY",1);
   aqua(r,185,287,"A CONFIRM  B CANCEL",2);
  }
  if(status){
   const char*msgs[]={"","BACKUP CREATED","BACKUP FAILED","INVALID SAVE","SAVED TO GAME FILE","OPERATION FAILED","FOLDER FOUND","FOLDER EMPTY","NOTHING TO SAVE","UNSAVED CHANGES LOST","CHANGE STAGED","EGG ADDED TO PC","POKEMON DELETED","EVENT MON STAGED","SAVE OR RELOAD BEFORE EXIT","READ ONLY SAVE","CARD SPEC FILE EXPORTED","CARD FILE IMPORTED","ME3 STAGED - Y SAVE","ME3 REJECTED / NOT RS"};
   if(status>=1&&status<=19){
    fill(r,157,443,476,34,38,87,91);
    label(r,167,453,msgs[status],2);
   }
  }else if(detail&&!confirming){muted(r,559,16,dirty?"UNSAVED":"READY",1);}
  SDL_RenderPresent(r);SDL_Delay(16);
 }
 if(mini_atlas)SDL_DestroyTexture(mini_atlas);
 SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
