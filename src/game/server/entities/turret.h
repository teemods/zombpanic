#ifndef GAME_SERVER_ENTITIES_TURRET_H
#define GAME_SERVER_ENTITIES_TURRET_H

#include <game/server/entity.h>

class CTurret : public CEntity
{
public:
	CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Type, vec2 Pos2 = vec2(0, 0));

	virtual void Reset();
	virtual void Tick();
	virtual void Snap(int SnappingClient);

	void Fire();

	void AddTurretExperience();
	void ResetRegenerationTime();

	int m_Owner;

private:
	int m_inIDs[9];
	int m_IDC;
	int m_IDG;
	int m_IDS;
	int m_Ammo;
	vec2 Direction;
	vec2 SavePosion;
	vec2 InitGrenadePos;
	vec2 m_Pos2;
	bool BackSpeed;
	int m_Type;
	int m_ReloadTick;
	int m_RegenerationTime;
};

#endif // GAME_SERVER_ENTITIES_TURRET_H
