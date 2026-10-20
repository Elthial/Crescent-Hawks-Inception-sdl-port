#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Test-only dispatch boundaries. Original planning-menu body executes;
 * callee internals are covered separately, not claimed as integrated here. */
extern uint8_t CombatantActionState[Enemy_All_CombatantId_Range_First];
static uint16_t choices[16],selectedIds[16],defaults[16],optionCounts[16],movementModes[16];
static unsigned count,index,descriptions,previews,clearPreviews,aiCalls,moves,kicks,weapons,scans,drains;
static uint16_t currentId;
static char text[8192];
static void checkLine(int value,unsigned line) { if(!value) { fprintf(stderr,"Combat menu mismatch line%u\n",line); exit(1); } }
#define check(value) checkLine(!!(value),__LINE__)
void Menu_Memory_Variables(uint16_t panel) { check(panel==3 || panel==4); }
void Draw_Top_Graphic_Sidebar(void) {}
void Display_Text_From_Memory(uint8_t *message) {
    size_t used=strlen(text),length=strlen((char *)message);
    check(used+length<sizeof text); memcpy(text+used,message,length+1);
}
void Display_Friendly_Combatant_Description(uint16_t id,uint16_t traitor,uint16_t infantry) {
    check(id<12 && CombatantActive[id] && !traitor && !infantry); currentId=id; ++descriptions;
}
void Combat_Render_Movement_Preview(uint16_t id,uint16_t full) {
    check(id==currentId); ++previews;
    if(!full) {
        ++clearPreviews; check(id>=4 && !CombatantActionState[id]);
        for(unsigned byte=0;byte<CombatMovementOrderBytes;++byte) check(CombatMovementOrders[id*CombatMovementOrderBytes+byte]==255);
    }
}
void Combat_Computer_Control(uint16_t id,uint16_t preview) { check(id==currentId && preview==TRUE); ++aiCalls; }
void Combat_Select_Movement_Plan_For_Turn(uint16_t id,uint16_t mode) { check(id==currentId && moves<16); movementModes[moves++]=mode; }
void Combat_Kick_Target(uint16_t id) { check(id==currentId && id<4); ++kicks; }
void Combat_Weapon_UI(uint16_t id) { check(id==currentId); ++weapons; }
void Scan_Enemies(uint16_t id) { check(id==currentId); ++scans; }
void Drain_Pending_Keyboard_Input(void) { ++drains; }
uint16_t Display_Menu_Choices_And_Check(uint16_t panel) {
    check(panel==3 && index<count && TextColour==15);
    selectedIds[index]=currentId; defaults[index]=CombatMessageMenuDefaultOption;
    optionCounts[index]=CombatMessageMenuOptionCount; return choices[index++];
}
static void prepare(uint16_t id) {
    memset(CombatantActive,0,sizeof CombatantActive); CombatantActive[id]=1;
    memset(CombatantActionState,0,sizeof CombatantActionState);
    memset(CombatMovementOrders,7,sizeof CombatMovementOrders);
    count=index=descriptions=previews=clearPreviews=aiCalls=moves=kicks=weapons=scans=drains=0;
    text[0]=0; DisableComputerControl=0; CombatMessageMenuDefaultOption=55;
}
static void key(uint16_t choice) { check(count<16); choices[count++]=choice; }
static void finish(uint16_t expected) {
    check(Combat_UI_Menu_Logic()==expected); check(index==count && drains==count && descriptions==count);
}
int main(void) {
    prepare(2); key(9); finish(FALSE);
    check(selectedIds[0]==2 && defaults[0]==9 && optionCounts[0]==10 && previews==1 && !aiCalls);
    check(strstr(text,"Walk\rRun\rJump\rUse Weapons\rKick\rComputer\rScan Unit\rNext Unit\rFlee\rBegin Fight\r"));
    prepare(8); key(6); finish(TRUE); check(defaults[0]==7 && optionCounts[0]==8);
    check(strstr(text,"Move\rClear Moves\rUse Weapon\rComputer\rScan Unit\rNext Unit\rFlee\rBegin Fight\r"));
    for(unsigned choice=0;choice<7;++choice) {
        prepare(1); key((uint16_t)choice); key(9); finish(FALSE);
        check(moves==(unsigned)(choice<3) && kicks==(unsigned)(choice==4) && aiCalls==(unsigned)(choice==5) && weapons==(unsigned)(choice==3) && scans==(unsigned)(choice==6));
        if(moves) check(movementModes[0]==choice);
        check(defaults[1]==9); /*Menu selection boundary does not alter defaults*/
    }
    for(unsigned choice=0;choice<5;++choice) {
        prepare(5); key((uint16_t)choice); key(7); finish(FALSE);
        check(moves==(unsigned)(choice==0) && clearPreviews==(unsigned)(choice==1) && aiCalls==(unsigned)(choice==3) && weapons==(unsigned)(choice==2) && scans==(unsigned)(choice==4) && !kicks);
        if(choice==1) check(CombatMovementOrders[4*CombatMovementOrderBytes]==7 && CombatMovementOrders[6*CombatMovementOrderBytes]==7);
    }
    prepare(0); CombatantActive[5]=1; key(7); key(5); key(9); finish(FALSE);
    check(selectedIds[0]==0 && selectedIds[1]==5 && selectedIds[2]==0 && defaults[1]==5 && defaults[2]==7);
    prepare(11); key(5); key(7); finish(FALSE); check(selectedIds[1]==11 && defaults[1]==5);
    prepare(0); CombatantActionState[0]=2; key(9); finish(FALSE);
    check(aiCalls==1 && !previews && strstr(text,"\rThe computer is currently moving this unit.Walk\r"));
    prepare(0); CombatantActive[4]=1; DisableComputerControl=2;
    CombatantActionState[0]=CombatantActionState[4]=3; key(5); key(7); key(7); finish(FALSE);
    check(!CombatantActionState[0] && CombatantActionState[4]==3 && aiCalls==1 && previews==2);
    prepare(0); key(8); finish(TRUE);
    prepare(0); key(0x0100); finish(FALSE); check(!moves); /*No BYTE truncation*/
    prepare(0); key(UINT16_MAX); key(9); finish(FALSE); check(moves==1 && movementModes[0]==UINT16_MAX); /*Signed JL*/
    puts("Original combat planning-menu dispatch checks passed"); return 0;
}
