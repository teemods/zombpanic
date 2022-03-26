#include <base/math.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/player.h>

#include "character.h"

#include "projectile.h"
#include "turret.h"
#include "laserwall.h"

CTurret::CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Type, vec2 Pos2) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_TURRET)
{
	m_Pos = Pos;
	m_Pos2 = Pos2;
	m_Owner = Owner;
	m_Type = Type;

	m_ReloadTick = 0;
	m_TemporaryPos = vec2(0, 0);
	m_InitGrenadePos = Pos;

	if(m_Type == WEAPON_LASER || m_Type == WEAPON_GRENADE)
		m_Direction = normalize(m_Pos2 - m_Pos);
	else
		m_Direction = GameServer()->GetPlayerChar(m_Owner)->GetVec2LastestInput();

	int InSize = sizeof(m_inIDs) / sizeof(int);
	for(int i = 0; i < InSize; i++)
		m_inIDs[i] = Server()->SnapNewID();

	m_IDC = Server()->SnapNewID();
	m_IDS = Server()->SnapNewID();
	m_IDG = Server()->SnapNewID();

	GameWorld()->InsertEntity(this);
}

void CTurret::Reset()
{
	m_MarkedForDestroy = true;

	int InSize = sizeof(m_inIDs) / sizeof(int);
	for(int i = 0; i < InSize; i++)
		Server()->SnapFreeID(m_inIDs[i]);

	Server()->SnapFreeID(m_IDS);
	Server()->SnapFreeID(m_IDC);
	Server()->SnapFreeID(m_IDG);

	if(GameServer()->GetPlayerChar(m_Owner))
	{
		if(m_Type == WEAPON_GUN || m_Type == WEAPON_SHOTGUN)
			GameServer()->GetPlayerChar(m_Owner)->m_TurretActive[m_Type] = false;
	}
}

void CTurret::Tick()
{
	// This is to prevent from human turret keep alive after the human turn into zombie
	CCharacter *OwnerCharacter = GameServer()->GetPlayerChar(m_Owner);

	if(!OwnerCharacter || !OwnerCharacter->IsAlive() || OwnerCharacter->GetPlayer()->GetTeam() != TEAM_BLUE)
		return Reset();

	if(m_ReloadTick)
		m_ReloadTick--;

	// Special handles
	if(m_Type == WEAPON_GRENADE)
	{
		HandleGrenadeTurret();
		return;
	}
	if(m_Type == WEAPON_HAMMER)
	{
		HandleHammerTurret();
		return;
	}
	if(m_Type == WEAPON_LASER)
	{
		HandleLaserTurret();
		return;
	}

	HandleShootingTurret();
}

void CTurret::HandleGrenadeTurret()
{
	// Move grenade position
	if(distance(m_Pos2, m_Pos) < 5)
		m_TurretReturning = true;

	if(m_TurretReturning)
	{
		m_TemporaryPos = normalize(m_InitGrenadePos - m_Pos);
		if(distance(m_InitGrenadePos, m_Pos) < 5)
			m_TurretReturning = false;
	}
	else
	{
		m_TemporaryPos = normalize(m_Pos2 - m_Pos);
	}

	m_Pos += m_TemporaryPos * (distance(m_Pos2, m_InitGrenadePos) / (float)(g_Config.m_PanicTurretMaxSize / 2));

	// Do not spawn granade if in cooldown
	if(m_ReloadTick)
		return;

	int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime);

	vec2 ProjStartPos = m_Pos + m_TemporaryPos * GetProximityRadius() * 0.75f;
	new CProjectile(
		GameWorld(),
		WEAPON_GRENADE, //Type
		m_Owner, //Owner
		ProjStartPos, //Pos
		m_TemporaryPos, //Dir
		Lifetime, //Span
		false, //Freeze
		true, //Explosive
		2, //Force
		SOUND_GRENADE_EXPLODE, //SoundImpact,
		0, // Layer
		0, // Number
		0 // Damage
	);

	GameServer()->CreateSound(m_Pos, SOUND_GRENADE_FIRE);

	m_ReloadTick = GetInitialReloadTick();
}

void CTurret::HandleHammerTurret()
{
	CCharacter *ClosestCharacter = GetClosestCharacter();
	if(!ClosestCharacter)
	{
		m_TemporaryPos = m_Pos;
		return;
	}

	// Aim turret to player
	m_Direction = normalize(ClosestCharacter->m_Pos - m_Pos);

	// Do not pull if in cooldown
	if(m_ReloadTick)
		return;

	// Found target, pull it
	m_TemporaryPos = ClosestCharacter->m_Pos;
	ClosestCharacter->Core()->m_Vel -= m_Direction * 1.5;

	// Target collided, kill it
	if(distance(ClosestCharacter->m_Pos, m_Pos) < 30)
	{
		GameServer()->CreateHammerHit(ClosestCharacter->m_Pos);
		// pTarget->TakeDamage(Direction * 2, 800 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretDmg*2, m_Owner, WEAPON_HAMMER);
		ClosestCharacter->TakeDamage(m_Direction * 2, g_Config.m_PanicHammerTurretInitialDamage, m_Owner, WEAPON_HAMMER);

		m_TemporaryPos = m_Pos;
		m_ReloadTick = GetInitialReloadTick();
	}
}

void CTurret::HandleLaserTurret()
{
	// Do not shot if in cooldown
	if(m_ReloadTick)
		return;

	// Radius: 6.0 (Wall collision radius) + 4.0 (Margin to not stuck the tee)
	for(auto &pCharacter : GameWorld()->IntersectedCharacters(m_Pos, m_Pos2, 10.0f))
	{
		if(pCharacter->GetPlayer()->GetTeam() != TEAM_RED)
			continue;

		new CLaserWall(GameWorld(), m_Pos, m_Pos2, m_Owner, g_Config.m_PanicTurretWallDuration);
		GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);

		m_ReloadTick = GetInitialReloadTick();

		break;
	}
}

void CTurret::HandleShootingTurret()
{
	CCharacter *ClosestCharacter = GetClosestCharacter();
	if(!ClosestCharacter)
		return;

	// Aim turret to player
	m_Direction = normalize(ClosestCharacter->m_Pos - m_Pos);

	// Turret is under cooldown
	if(m_ReloadTick)
		return;

	vec2 ProjStartPos = m_Pos + m_Direction * GetProximityRadius() * 0.75f;

	if(m_Type == WEAPON_GUN)
	{
		int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime);
		new CProjectile(
			GameWorld(),
			WEAPON_GUN, //Type
			m_Owner, //Owner
			ProjStartPos, //Pos
			m_Direction, //Dir
			Lifetime, //Span
			false, //Freeze
			false, //Explosive
			1, //Force
			-1, //SoundImpact
			0, // Layer
			0, // Number
			// 2+GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretDmg/5 // Damage
			g_Config.m_PanicGunInitialDamage // Damage
		);

		GameServer()->CreateSound(m_Pos, SOUND_GUN_FIRE);
	}

	if(m_Type == WEAPON_SHOTGUN)
	{
		int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime);

		int ShotSpread = g_Config.m_PanicShotgunInitialShotSpread; // int ShotSpread = 5 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretLevel / 20;
		for(int i = -ShotSpread; i <= ShotSpread; ++i)
		{
			float a = angle(m_Direction);
			a += 0.070f * i;

			float v = 1 - (absolute(i) / (float)ShotSpread);
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
			new CProjectile(
				GameWorld(),
				WEAPON_SHOTGUN, //Type
				m_Owner, //Owner
				ProjStartPos, //Pos
				vec2(cosf(a), sinf(a)) * Speed, // Dir
				Lifetime, // Span
				false, // Freeze
				false, // Explosive
				3, // Force
				-1, // SoundImpact
				0, // Layer
				0, // Number
				// 1+GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretDmg/15 // Damage
				g_Config.m_PanicShotgunInitialDamage // Damage
			);
		}

		GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);
	}

	m_ReloadTick = GetInitialReloadTick();
}

int CTurret::GetInitialReloadTick()
{
	float Time = 0.f;

	if(m_Type == WEAPON_GRENADE)
		Time = 3.00f; // m_ReloadTick = 3000 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 40);
	if(m_Type == WEAPON_HAMMER)
		Time = 20.00f;
	if(m_Type == WEAPON_LASER)
		Time = 36.00f;
	if(m_Type == WEAPON_GUN)
		Time = 1.00f; // m_ReloadTick = 3600 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 30);
	if(m_Type == WEAPON_SHOTGUN)
		Time = 2.80f; // m_ReloadTick = 3800 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 10);

	return Time * Server()->TickSpeed();
}

CCharacter *CTurret::GetClosestCharacter()
{
	CCharacter *pTarget = 0;
	CCharacter *pClosest = (CCharacter *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);

	int DetectDistance = g_Config.m_PanicTurretDetectDistance;
	while(pClosest)
	{
		int DistanceToCharacter = distance(pClosest->m_Pos, m_Pos);
		if(DistanceToCharacter <= DetectDistance && pClosest->GetPlayer()->GetTeam() == TEAM_RED)
		{
			if(!GameServer()->Collision()->IntersectLine(m_Pos, pClosest->m_Pos, 0, 0))
			{
				pTarget = pClosest;
				DetectDistance = DistanceToCharacter;
			}
		}
		pClosest = (CCharacter *)pClosest->TypeNext();
	}

	return pTarget;
}

void CTurret::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	// SHOTGUN DOTS DISPLAY
	int MaxDistance = 10;
	int ReloadDistance = 15 + ((m_ReloadTick * 100 / GetInitialReloadTick()) * MaxDistance / 100);

	int InSize = sizeof(m_inIDs) / sizeof(int);
	for(int i = 0; i < InSize; i++)
	{
		CNetObj_Projectile *pEff = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_inIDs[i], sizeof(CNetObj_Projectile)));
		if(!pEff)
			continue;

		float Radians = 2 * pi / InSize * (float)(i + 0.5);

		pEff->m_X = m_Pos.x + (cos(angle(m_Direction) + Radians) * ReloadDistance);
		pEff->m_Y = m_Pos.y + (sin(angle(m_Direction) + Radians) * ReloadDistance);

		pEff->m_StartTick = Server()->Tick() - 2;
		pEff->m_Type = WEAPON_SHOTGUN;
	}

	// PROJECTILE AMMO TYPE DISPLAY
	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDS, sizeof(CNetObj_Projectile)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_StartTick = Server()->Tick() - 3; // ???
	pObj->m_Type = m_Type;

	// LASER WALL ON POSITION 1
	CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDC, sizeof(CNetObj_Laser)));
	if(!pObj2)
		return;

	pObj2->m_X = (int)m_Pos.x;
	pObj2->m_Y = (int)m_Pos.y;

	pObj2->m_FromX = (int)m_Pos.x + m_Direction.x * 32;
	pObj2->m_FromY = (int)m_Pos.y + m_Direction.y * 32;

	if(pObj2->m_FromX < 1 && pObj2->m_FromY < 1)
	{
		pObj2->m_FromX = (int)m_Pos.x;
		pObj2->m_FromY = (int)m_Pos.y;
	}

	pObj2->m_StartTick = Server()->Tick();

	// Custom display for specific turrets
	switch(m_Type)
	{
	case WEAPON_HAMMER:
		pObj2->m_FromX = (int)m_TemporaryPos.x;
		pObj2->m_FromY = (int)m_TemporaryPos.y;

		pObj2->m_StartTick = Server()->Tick() - 5; // Thin line effect
		break;
	case WEAPON_GRENADE:
		pObj2->m_X = (int)m_InitGrenadePos.x;
		pObj2->m_Y = (int)m_InitGrenadePos.y;

		pObj2->m_FromX = (int)m_Pos2.x;
		pObj2->m_FromY = (int)m_Pos2.y;

		pObj2->m_StartTick = Server()->Tick() - 2; // Grenade bullets needs to be above the laser
		break;
	}

	// LASER DOT ON POSITION 2
	if(m_Type == WEAPON_GRENADE)
	{
		CNetObj_Laser *pObj3 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDG, sizeof(CNetObj_Laser)));
		if(!pObj3)
			return;

		pObj3->m_X = (int)m_Pos2.x;
		pObj3->m_Y = (int)m_Pos2.y;
		pObj3->m_FromX = (int)m_Pos2.x;
		pObj3->m_FromY = (int)m_Pos2.y;

		if(pObj3->m_FromX < 1 && pObj3->m_FromY < 1)
		{
			pObj3->m_FromX = (int)m_Pos2.x;
			pObj3->m_FromY = (int)m_Pos2.y;
		}

		pObj3->m_StartTick = Server()->Tick() - 2;
	}
}