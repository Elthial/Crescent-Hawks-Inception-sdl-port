#include "game.h"
#include "dos.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned selectedSlot,mediaFailures,opens,requests,closes,writes,failMask,keys,panels,topDraws,exitStage;
static int createFails;
static uint8_t image[OriginalSavedStateBytes+5],original[OriginalSavedStorageBytes];
static unsigned imageSize;
static void verify(int ok,unsigned line) { if(!ok) { fprintf(stderr,"Save mismatch line%u\n",line); exit(1); } }
#define check(x) verify(!!(x),__LINE__)
void Menu_Memory_Variables(uint16_t layout) { check(layout==3 && panels++==0); }
void Draw_Top_Graphic_Sidebar(void) { ++topDraws; }
void Draw_Message_Box(void) { check(CacheMapRoomLoaded!=0); }
void Display_Text_From_Memory(uint8_t *text)
{
    if(!strcmp((char *)text,"Save Game:\rOne\rTwo\rThree\rFour\rFive\rSix\rCancel")) check(topDraws==1);
    else {
        check(!strcmp((char *)text,"Save game failed! Check your disk.\x06\x0F"));
        check(TextColour==(GraphicsAdapter==0?2:4) && topDraws==2);
    }
}
void Display_Text_From_Memory_ScreenRetrace_KeyboardInput(uint8_t *text)
{ check(!strcmp((char *)text,"You can't save the game inside the map room.")); }
uint16_t Display_Menu_Choices_And_Check(uint16_t menu) { check(menu==40); return (uint16_t)selectedSlot; }
uint16_t Keyboard_Get_ASCII_Hex_Input(void) { ++keys; return 13; }
void Select_Game_Disk_And_Drive(uint16_t disk)
{
    if(disk==3) { check(opens==0 && exitStage==0); RequestedGameDiskNumber=3; }
    else check(disk==1 && exitStage++==2);
}
uint16_t Request_Game_Disk(uint16_t disk) { check(disk==3); ++requests; return TRUE; }
int16_t Get_FileHandle(const uint8_t *name,uint16_t mode,...)
{
    ++opens;
    if(!strcmp((char *)name,"INFOCOM.CMP")) {
        check(mode==0x8000);
        return opens<=mediaFailures?-1:17;
    }
    check(name==SaveGameFileName && closes==1 && mode==0x8101);
    check(name[4]=='1'+selectedSlot);
    va_list arguments; va_start(arguments,mode); check(va_arg(arguments,int)==0x180); va_end(arguments);
    return createFails?-1:23;
}
int16_t DOS_close_file(uint16_t handle)
{ check(handle==(closes==0?17:23)); ++closes; return -1; /* native ignores close failure */ }
int16_t DOS_write_memory_to_save_file(uint16_t handle,const void *data,uint16_t count)
{
    check(handle==23 && writes<4);
    const void *expected[4]={NULL,OriginalSavedState.bytes,&CrescentHawkMapPositionX,&CrescentHawkMapPositionY};
    const unsigned sizes[4]={1,OriginalSavedStateBytes,2,2};
    check(count==sizes[writes]);
    if(writes==0) check(*(const uint8_t *)data==12);
    else check(data==expected[writes]);
    memcpy(image+imageSize,data,count); imageSize+=count;
    return (failMask&(1u<<writes++))?-1:(int16_t)count;
}
void Menu_Draw_MultiSelect(uint16_t redraw) { check(redraw==FALSE && exitStage++==0); }
void Draw_Health_and_C_Bills_Sidebar(uint16_t redraw) { check(redraw==TRUE && exitStage++==1); }
static void reset(void)
{
    opens=requests=closes=writes=keys=panels=topDraws=exitStage=imageSize=0;
    for(unsigned index=0;index<sizeof original;++index) original[index]=(uint8_t)(index*31+7);
    memcpy(OriginalSavedState.bytes,original,sizeof original);
    CacheMapRoomLoaded=0; original[0xD34E - 0xC614]=0;
    CrescentHawkMapPositionX=0x0C35; CrescentHawkMapPositionY=0xC047;
    memcpy(SaveGameFileName,"Game0",6);
}
int main(void)
{
    for(selectedSlot=0;selectedSlot<7;++selectedSlot)
    for(failMask=0;failMask<16;++failMask)
    for(createFails=0;createFails<2;++createFails)
    for(mediaFailures=0;mediaFailures<3;++mediaFailures)
    for(unsigned adapter=0;adapter<2;++adapter) {
        reset(); GraphicsAdapter=(uint16_t)adapter;
        Save_Game(); check(exitStage==3 && panels==1);
        check(!memcmp(OriginalSavedState.bytes,original,sizeof original));
        if(selectedSlot==6) check(opens==0 && writes==0 && closes==0 && keys==0 && topDraws==1);
        else {
            check(opens==mediaFailures+2 && requests==mediaFailures);
            check(writes==(createFails?0u:4u) && closes==(createFails?1u:2u));
            check(keys==(unsigned)(createFails || failMask) && topDraws==(unsigned)(createFails || failMask)+1);
            if(!createFails) {
                check(imageSize==OriginalSavedStateBytes+5 && image[0]==12);
                check(!memcmp(image+1,original,OriginalSavedStateBytes));
                check(image[OriginalSavedStateBytes+1]==0x35 && image[OriginalSavedStateBytes+2]==12);
                check(image[OriginalSavedStateBytes+3]==0x47 && image[OriginalSavedStateBytes+4]==0xC0);
            }
        }
    }
    for(unsigned flag=1;flag<256;++flag) {
        reset(); CacheMapRoomLoaded=(uint8_t)flag;
        Save_Game(); check(exitStage==3 && panels==0 && opens==0 && writes==0 && keys==1);
    }
    return 0;
}
