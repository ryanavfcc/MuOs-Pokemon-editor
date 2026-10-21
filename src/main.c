
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#define MAX 200
static char paths[MAX][512];
static int nfiles=0;
static void search(const char *dir,int depth){
 if(depth>5||nfiles>=MAX)return;
 DIR *d=opendir(dir);if(!d)return;
 struct dirent *e;
 while((e=readdir(d)) && nfiles<MAX){
  if(e->d_name[0]=='.')continue;
  char p[512];int k=snprintf(p,sizeof(p),"%s/%s",dir,e->d_name);
  if(k<0||k>=(int)sizeof(p))continue;
  struct stat st;if(stat(p,&st))continue;
  if(S_ISDIR(st.st_mode))search(p,depth+1);
  else if(S_ISREG(st.st_mode)&&st.st_size==131072){
   size_t z=strlen(p);
   if(z>=4 && (!strcasecmp(p+z-4,".sav")))snprintf(paths[nfiles++],512,"%s",p);
  }
 }
 closedir(d);
}
static const char *abc=" ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_/:";
static const unsigned char glyph[][7]={
 {0,0,0,0,0,0,0},{14,17,31,17,17,17,0},{30,17,30,17,17,30,0},
 {14,17,16,16,17,14,0},{30,17,17,17,17,30,0},{31,16,30,16,16,31,0},
 {31,16,30,16,16,16,0},{14,17,16,23,17,15,0},{17,17,31,17,17,17,0},
 {14,4,4,4,4,14,0},{7,2,2,2,18,12,0},{17,18,28,18,17,17,0},
 {16,16,16,16,16,31,0},{17,27,21,21,17,17,0},{17,25,21,19,17,17,0},
 {14,17,17,17,17,14,0},{30,17,30,16,16,16,0},{14,17,17,21,18,13,0},
 {30,17,30,20,18,17,0},{15,16,14,1,1,30,0},{31,4,4,4,4,4,0},
 {17,17,17,17,17,14,0},{17,17,17,17,10,4,0},{17,17,21,21,21,10,0},
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
int main(void){
 if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER)){fprintf(stderr,"SDL: %s\n",SDL_GetError());return 1;}
 SDL_Window *w=SDL_CreateWindow("Pokemon Browser",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,640,480,SDL_WINDOW_FULLSCREEN_DESKTOP);
 if(!w){fprintf(stderr,"Window: %s\n",SDL_GetError());SDL_Quit();return 1;}
 SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);
 if(!r){SDL_DestroyWindow(w);SDL_Quit();return 1;}
 SDL_RenderSetLogicalSize(r,640,480);
 for(int i=0;i<SDL_NumJoysticks();i++)if(SDL_IsGameController(i))SDL_GameControllerOpen(i);
 const char *dirs[]={"/mnt/mmc/ROMS","/mnt/sdcard/ROMS","/mnt/mmc/SAVES","/mnt/sdcard/SAVES"};
 for(int i=0;i<4;i++)search(dirs[i],0);
 int selected=0,detail=0,running=1;
 while(running){
  SDL_Event e;
  while(SDL_PollEvent(&e)){
   if(e.type==SDL_QUIT)running=0;
   int b=-1;
   if(e.type==SDL_CONTROLLERBUTTONDOWN)b=e.cbutton.button;
   if(e.type==SDL_KEYDOWN){
    int k=e.key.keysym.sym;
    if(k==SDLK_DOWN)b=SDL_CONTROLLER_BUTTON_DPAD_DOWN;
    if(k==SDLK_UP)b=SDL_CONTROLLER_BUTTON_DPAD_UP;
    if(k==SDLK_RETURN)b=SDL_CONTROLLER_BUTTON_A;
    if(k==SDLK_ESCAPE||k==SDLK_BACKSPACE)b=SDL_CONTROLLER_BUTTON_B;
   }
   if(b==SDL_CONTROLLER_BUTTON_DPAD_DOWN&&selected<nfiles-1)selected++;
   if(b==SDL_CONTROLLER_BUTTON_DPAD_UP&&selected>0)selected--;
   if(b==SDL_CONTROLLER_BUTTON_A&&nfiles)detail=1;
   if(b==SDL_CONTROLLER_BUTTON_B){if(detail)detail=0;else running=0;}
  }
  SDL_SetRenderDrawColor(r,13,22,35,255);SDL_RenderClear(r);
  fill(r,0,0,640,65,27,76,87);label(r,16,19,"POKEMON SAVE BROWSER",3);
  if(detail){
   label(r,16,105,"SELECTED FILE",2);
   const char *q=strrchr(paths[selected],'/');label(r,16,160,q?q+1:paths[selected],2);
   label(r,16,230,"128 KB GBA SAVE",2);
   label(r,16,290,"READ ONLY - NO EDITS",2);
   label(r,16,430,"B BACK",2);
  }else{
   if(!nfiles){label(r,16,140,"NO SAV FILES FOUND",2);label(r,16,200,"CHECK YOUR SAVE FOLDERS",2);}
   int first=selected>3?selected-3:0;
   for(int i=first,y=90;i<nfiles&&y<415;i++,y+=40){
    if(i==selected)fill(r,8,y-6,624,36,39,87,105);
    const char *q=strrchr(paths[i],'/');label(r,16,y,q?q+1:paths[i],2);
   }
   label(r,16,448,"UP DOWN   A VIEW   B EXIT",2);
  }
  SDL_RenderPresent(r);SDL_Delay(15);
 }
 SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
}
