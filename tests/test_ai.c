#include "game.h"
#include "dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint8_t CombatantActionState[Enemy_All_CombatantId_Range_First];
static uint16_t clearPath,pathTarget,pathActor,plannedActor,budget;
static unsigned mapMoves,pathCalls,plans,previews;
static void checkLine(int value,unsigned line) { if(!value) { fprintf(stderr,"Combat AI mismatch line%u\n",line); exit(1); } }
#define check(value) checkLine(!!(value),__LINE__)
/* Whole original AI, real budgets, distance, weapon ordinal and heading.
 * Cache/path rendering boundaries isolated here; separate plan test runs
 * AI with the actual terrain/path/step/occupancy chain as well. */
void Move_Map_View_To_Packed_Position(uint16_t x,uint16_t y) { ++mapMoves; CrescentHawkMapPositionX=x; CrescentHawkMapPositionY=y; }
uint16_t Combat_Check_Terrain_Path(uint16_t actor,uint16_t target,uint16_t x,uint16_t y) {
    check(x==CombatantPackedX[target] && y==CombatantPackedY[target]); ++pathCalls; pathActor=actor; pathTarget=target; return clearPath;
}
void Combat_Calculate_Movement(uint16_t actor) { ++plans; plannedActor=actor; budget=CharacterMovementPointsRemaining; }
void Combat_Render_Movement_Preview(uint16_t actor,uint16_t full) { check(actor==plannedActor && full==TRUE && actor<12); ++previews; }
void Menu_Memory_Variables(uint16_t panel) { (void)panel; check(0); }
void Draw_Top_Graphic_Sidebar(void) { check(0); }
void Display_Text_From_Memory(uint8_t *text) { (void)text; check(0); }
uint16_t Keyboard_Get_ASCII_Hex_Input(void) { check(0); return 0; }
uint16_t Display_Menu_Choices_And_Check(uint16_t menu) { (void)menu; check(0); return 0; }
uint16_t Prompt_Yes_No(uint16_t choice) { (void)choice; check(0); return 0; }
static void prepare(uint16_t actor,uint16_t target,uint16_t targetX,uint16_t targetY) {
    memset(Mechs,0,sizeof Mechs); memset(Characters,0,sizeof Characters);
    memset(MechHeatLevel,0,sizeof MechHeatLevel); memset(CombatantActive,0,sizeof CombatantActive);
    memset(CombatantActionState,0,sizeof CombatantActionState);
    memset(CombatMovementOrders,0x77,sizeof CombatMovementOrders); memset(CombatWeaponTarget,0x44,sizeof CombatWeaponTarget);
    memset(CombatantMovementDirection,0,sizeof CombatantMovementDirection); memset(CombatantAnimationSelector,0,sizeof CombatantAnimationDirection);
    for(unsigned record=0;record<8;++record) {
        Mechs[record].walkMove=4; Mechs[record].jumpMove=2;
        Mechs[record].currentActuators[0]=Mechs[record].currentActuators[1]=15;
        Mechs[record].criticalSlots[0]=Mech_Med_Laser; Mechs[record].currentAmmo[0]=255;
    }
    for(unsigned character=0;character<16;++character) { Characters[character].dexterity=8; Characters[character].weapon=7; }
    CombatantPackedX[actor]=0x0220; CombatantPackedY[actor]=0x3020;
    CombatantPackedX[target]=targetX; CombatantPackedY[target]=targetY; CombatantActive[target]=2;
    CrescentHawkMapPositionX=0x0210; CrescentHawkMapPositionY=0x3010;
    ArenaRentalMechMode=TraitorInParty=0; FriendlyPersonnelWithdrawal=EnemyPersonnelFlightPossible=0;
    clearPath=TRUE; mapMoves=pathCalls=plans=previews=0;
}
static void run(uint16_t actor,uint16_t preview) {
    Combat_Computer_Control(actor,preview);
    check(mapMoves==2 && pathCalls==1 && plans==1 && plannedActor==actor && pathActor==actor);
    check(CrescentHawkMapPositionX==0x0210 && CrescentHawkMapPositionY==0x3010);
    check(previews==(unsigned)(actor<12 && preview!=0));
    if(actor<12) check(CombatantActionState[actor]==1);
}
int main(void) {
    prepare(0,12,0x0238,0x3020); run(0,0);
    /* Raw component17 looks up LargeLaser short6, fourfold=24. Distance12
     * is already close enough: cleared orders, NOT component16's short4. */
    check(pathTarget==12 && CombatMovementOrders[0]==255 && budget==6 && CombatWeaponTarget[0]==12 && CombatWeaponTarget[11]==12 && CombatWeaponTarget[10]==255);
    prepare(0,12,0x0238,0x3020); Mechs[0].criticalSlots[0]=Mech_Small_Laser; run(0,2);
    /* Raw16 MedLaser short4 =>approach16, also within threshold. */
    check(CombatMovementOrders[0]==255);
    prepare(0,12,0x0238,0x3020); Mechs[0].criticalSlots[0]=0; run(0,0);
    check(CombatMovementOrders[0]==1 && CombatMovementOrders[1]==0x32 && CombatMovementOrders[2]==0x38 && CombatMovementOrders[3]==0x20);
    prepare(0,12,0x0238,0x3020); clearPath=0; run(0,0); check(CombatMovementOrders[0]==1 && CombatMovementOrders[2]==0x38);
    prepare(0,12,0x0260,0x3020); Mechs[0].criticalSlots[0]=Mech_Small_Laser; run(0,0);
    check(CombatMovementOrders[0]==1 && CombatMovementOrders[2]==0x30); /*Distance32-threshold16 =16 fixed steps*/
    prepare(0,12,0x0230,0x3020); CombatantActive[13]=1; CombatantPackedX[13]=0x0210; CombatantPackedY[13]=0x3020;
    run(0,0); check(pathTarget==12); /*Equal-distance tie retains first*/
    prepare(0,13,0x0221,0x3020); CombatantActive[12]=1; CombatantPackedX[12]=0x0230; CombatantPackedY[12]=0x3020;
    ArenaRentalMechMode=2; run(0,0); check(pathTarget==12);
    prepare(0,16,0x0230,0x3020); run(0,0); check(pathTarget==16); /*Mech scan falls back to infantry*/
    prepare(4,16,0x0230,0x3020); run(4,1);
    check(pathTarget==16 && CombatWeaponTarget[4*12]==16 && CombatWeaponTarget[4*12+1]==0x44 && budget==6 && CombatMovementOrders[4*48+2]==0x25);
    prepare(4,12,0x0230,0x3020); run(4,0);
    check(pathTarget==12 && FriendlyPersonnelWithdrawal==1 && !EnemyPersonnelFlightPossible && CombatMovementOrders[4*48+2]==0x1A && CombatMovementOrders[4*48+3]==0x20);
    prepare(16,0,0x0210,0x3010); Characters[4].dexterity=4; run(16,1);
    check(pathTarget==0 && !FriendlyPersonnelWithdrawal && EnemyPersonnelFlightPossible==1 && CombatMovementOrders[16*48+2]==0x26 && CombatMovementOrders[16*48+3]==0x26 && budget==3);
    prepare(16,4,0x0230,0x3020); CombatantActive[5]=1; CombatantPackedX[5]=0x0238; CombatantPackedY[5]=0x3020;
    TraitorInParty=1; TraitorCharacterId=0; run(16,0); check(pathTarget==5);
    prepare(4,16,0x0230,0x3020); TraitorInParty=1; TraitorCharacterId=0; run(4,0); check(CombatWeaponTarget[4*12]==255);
    for(unsigned heat=0;heat<256;++heat) {
        prepare(12,0,0x0238,0x3020); MechHeatLevel[4]=(int8_t)(uint8_t)heat; run(12,0);
        if(heat>=30 && heat<128) check(CombatWeaponTarget[12*12]==255 && CombatWeaponTarget[12*12+11]==255 && CombatMovementOrders[12*48]==255);
        else check(CombatWeaponTarget[12*12]==0 && CombatWeaponTarget[12*12+11]==0);
    }
    prepare(0,12,0x0223,0x3021); run(0,0);
    check(CombatMovementOrders[0]==255 && CombatantAnimationSelector[0]==255 && CombatantMovementDirection[0]==(uint8_t)Get_Target_Compass_Direction(0x0220,0x3020,0x0223,0x3021));
    prepare(0,12,0x0223,0x3021); clearPath=0; run(0,0); check(CombatMovementOrders[0]==1 && CombatantAnimationSelector[0]==0);
    prepare(0,12,0x0230,0x3020); Mechs[0].criticalSlots[0]=0x91; Mechs[0].currentAmmo[0]=0; run(0,0);
    check(CombatWeaponTarget[0]==255 && CombatWeaponTarget[11]==12); /*Kick unconditionally offered*/
    prepare(0,12,0x0230,0x3020); Mechs[0].criticalSlots[0]=0x91; run(0,0); check(CombatWeaponTarget[0]==12); /*Destroyed raw ordinal still !=FF*/
    puts("Original whole combat AI planning checks passed"); return 0;
}
