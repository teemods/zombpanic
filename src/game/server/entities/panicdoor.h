/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PANICDOOR_H
#define GAME_SERVER_ENTITIES_PANICDOOR_H

#include <game/server/entity.h>

enum
{
	MAX_DOORS = 200 // Must be an even number
};

enum
{
	DOOR_OPEN = 0,
	DOOR_CLOSED,

	DOOR_ZOMBIE_OPEN,
	DOOR_ZOMBIE_CLOSED,
	DOOR_ZOMBIE_CLOSING,
	DOOR_ZOMBIE_REOPENED
};

class CPanicDoor : public CEntity
{
public:
	CPanicDoor(CGameWorld *pGameWorld, int DoorNumber);

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

private:
	short int m_DoorNumber;
	short int m_State;
};

#endif
