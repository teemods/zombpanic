/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_WALL_H
#define GAME_SERVER_ENTITIES_WALL_H

#include <game/server/entity.h>

class CWall : public CEntity
{
	int FindCharacters(vec2 Pos0, vec2 Pos1, float Radius, CCharacter **pChars, int Max);

public:
	CWall(CGameWorld *pGameWorld, vec2 From, vec2 To, int Owner, int Time, bool ZombiesIntersects);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

protected:
	bool HitCharacter(CCharacter *Hit);

private:
	vec2 m_Pos;
	vec2 m_From;
	int m_Owner;
	CCharacter *m_OwnerChar;
	int m_ID2;
	int m_Time;
	int ZombiesON;
};

#endif
