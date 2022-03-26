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

	void HandleGrenadeTurret();
	void HandleHammerTurret();
	void HandleLaserTurret();
	void HandleShootingTurret();

	int GetInitialReloadTick();
	CCharacter *GetClosestCharacter();

	int m_Owner;

private:
	int m_inIDs[6];
	int m_IDC;
	int m_IDG;
	int m_IDS;

	vec2 m_Direction;

	vec2 m_Pos2;
	vec2 m_TemporaryPos;
	vec2 m_InitGrenadePos;

	int m_Type;
	int m_ReloadTick;
	bool m_TurretReturning;
};

#endif // GAME_SERVER_ENTITIES_TURRET_H
