/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gamecontroller.h>
#include <game/server/gamemodes/DDRace.h>

#include <game/server/player.h>

#include "character.h"

#include "panicdoor.h"

CPanicDoor::CPanicDoor(CGameWorld *pGameWorld, int Number)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP)
{
	m_Number = Number;

	// m_State is defined by gamecontroller value. It do not have a default value

	GameWorld()->InsertEntity(this);
}

void CPanicDoor::Reset()
{
}

void CPanicDoor::Tick()
{
    m_State = GameServer()->m_pController->DoorState(m_Number);

	if(m_State == DOOR_OPEN || m_State == DOOR_ZOMBIE_OPEN || m_State == DOOR_ZOMBIE_CLOSING || m_State == DOOR_ZOMBIE_REOPENED)
		return;

	CCharacter *pCloseChar[MAX_CLIENTS];
	int Num = GameServer()->m_World.FindEntities(m_Pos, 8.0f, (CEntity**)pCloseChar, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; i++)
	{
		if(
			!pCloseChar[i]->IsAlive() || 
			GameServer()->Collision()->IntersectLine(m_Pos, pCloseChar[i]->m_Pos, NULL, NULL) || 
			(m_State == DOOR_ZOMBIE_CLOSED && pCloseChar[i]->GetPlayer()->GetTeam() == TEAM_BLUE)
		)
			continue;

		pCloseChar[i]->m_HittingDoor = true;
		pCloseChar[i]->m_PushDirection = normalize(pCloseChar[i]->m_PrevPos - m_Pos);
	}
}

void CPanicDoor::Snap(int SnappingClient)
{
	if(m_State == DOOR_OPEN || m_State == DOOR_ZOMBIE_OPEN || m_State == DOOR_ZOMBIE_CLOSING || m_State == DOOR_ZOMBIE_REOPENED || NetworkClipped(SnappingClient))
		return;

	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), sizeof(CNetObj_Pickup)));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = (m_Number > MAX_DOORS/2) ? POWERUP_HEALTH : POWERUP_ARMOR;
	pP->m_Subtype = 0;
}
