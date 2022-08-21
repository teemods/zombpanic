/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_LASERWALL_H
#define GAME_SERVER_ENTITIES_LASERWALL_H

#include <game/server/entity.h>

class CLaserWall : public CEntity
{
public:
	CLaserWall(CGameWorld *pGameWorld, vec2 Pos, vec2 Pos2, int Owner, int Duration);

	virtual void Reset() override;
	virtual void Tick() override;
	virtual void Snap(int SnappingClient) override;

private:
	int m_ID2;
	vec2 m_Pos2;

	int m_Owner;
	int m_Time;
};

#endif
