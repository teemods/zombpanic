#include <base/math.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include "laserwall.h"

#include "character.h"

CLaserWall::CLaserWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Pos2, int Owner, int Duration) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Pos = Pos;
	m_Pos2 = Pos2;
	m_Owner = Owner;

	m_Time = Duration * Server()->TickSpeed();

	m_ID2 = Server()->SnapNewID();

	GameWorld()->InsertEntity(this);
}

void CLaserWall::Reset()
{
	m_MarkedForDestroy = true;

	Server()->SnapFreeID(m_ID2);
}

void CLaserWall::Tick()
{
	CCharacter *OwnerCharacter = GameServer()->GetPlayerChar(m_Owner);
	if(!OwnerCharacter || OwnerCharacter->GetPlayer()->GetTeam() != TEAM_BLUE || !m_Time)
		return Reset();

	m_Time--;

	for(auto &pCharacter : GameWorld()->IntersectedCharacters(m_Pos, m_Pos2, 2.0f))
	{
		if(pCharacter->GetPlayer()->GetTeam() != TEAM_RED)
			continue;

		pCharacter->Core()->m_Pos = pCharacter->m_PrevPos;
		pCharacter->Core()->m_Vel = vec2(0, 0);

		if(pCharacter->Core()->m_Jumped >= 2)
			pCharacter->Core()->m_Jumped = 1;
	}
}

void CLaserWall::Snap(int SnappingClient)
{
	CNetObj_Laser *pObj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos2.x;
	pObj->m_Y = (int)m_Pos2.y;
	pObj->m_FromX = (int)m_Pos.x;
	pObj->m_FromY = (int)m_Pos.y;
	if(SnappingClient == m_Owner)
		pObj->m_StartTick = Server()->Tick() - 3;
	else
		pObj->m_StartTick = Server()->Tick() - 1;

	CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID2, sizeof(CNetObj_Laser)));
	if(!pObj2)
		return;

	pObj2->m_FromX = (int)m_Pos.x;
	pObj2->m_FromY = (int)m_Pos.y;
	pObj2->m_X = (int)m_Pos.x;
	pObj2->m_Y = (int)m_Pos.y;
	if(SnappingClient == m_Owner)
		pObj2->m_StartTick = Server()->Tick() - 3;
	else
		pObj2->m_StartTick = Server()->Tick() - 1;
}
