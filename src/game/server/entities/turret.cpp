#include <base/math.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>

#include <game/server/player.h>

#include "character.h"

#include "projectile.h"
#include "turret.h"
#include "wall.h"

CTurret::CTurret(CGameWorld *pGameWorld, vec2 Pos, int Owner, int Type, vec2 Pos2) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_TURRET)
{
	m_Pos = Pos;
	m_Pos2 = Pos2;
	m_Owner = Owner;
	m_Type = Type;
	SavePosion = vec2(0, 0);
	InitGrenadePos = Pos;
	Direction = GameServer()->GetPlayerChar(m_Owner)->GetVec2LastestInput();

	m_ReloadTick = 0;

	GameServer()->GetPlayerChar(m_Owner)->m_TurretActive[m_Type] = true;

	for(unsigned i = 0; i < sizeof(m_inIDs) / sizeof(int); i++)
		m_inIDs[i] = Server()->SnapNewID();

	m_IDC = Server()->SnapNewID();
	m_IDS = Server()->SnapNewID();
	m_IDG = Server()->SnapNewID();

	GameWorld()->InsertEntity(this);
}

void CTurret::Reset()
{
	m_MarkedForDestroy = true;

	for(unsigned i = 0; i < sizeof(m_inIDs) / sizeof(int); i++)
		if(m_inIDs[i] >= 0)
		{
			Server()->SnapFreeID(m_inIDs[i]);
			m_inIDs[i] = -1;
		}

	if(m_IDS >= 0)
	{
		Server()->SnapFreeID(m_IDS);
		m_IDS = -1;
	}

	if(m_IDC >= 0)
	{
		Server()->SnapFreeID(m_IDC);
		m_IDC = -1;
	}

	if(m_IDG >= 0)
	{
		Server()->SnapFreeID(m_IDG);
		m_IDG = -1;
	}

	if(GameServer()->GetPlayerChar(m_Owner))
	{
		if(m_Type == WEAPON_GUN)
			GameServer()->GetPlayerChar(m_Owner)->m_TurretActive[WEAPON_GUN] = false;
		else if(m_Type == WEAPON_SHOTGUN)
			GameServer()->GetPlayerChar(m_Owner)->m_TurretActive[WEAPON_SHOTGUN] = false;
	}
}

void CTurret::Tick()
{
	// This is to prevent from human turret keep alive after the human turn into zombie
	if(!GameServer()->GetPlayerChar(m_Owner) ||
		!GameServer()->GetPlayerChar(m_Owner)->IsAlive() ||
		GameServer()->m_apPlayers[m_Owner]->GetTeam() != TEAM_BLUE)
		return Reset();

	if(m_Type == WEAPON_GRENADE)
	{
		if(distance(m_Pos2, m_Pos) < 5)
			BackSpeed = true;
		if(BackSpeed)
		{
			SavePosion = normalize(InitGrenadePos - m_Pos);
			if(distance(InitGrenadePos, m_Pos) < 5)
				BackSpeed = false;
		}
		else
			SavePosion = normalize(m_Pos2 - m_Pos);

		m_Pos += SavePosion * 1.5f;

		if(m_ReloadTick)
		{
			m_ReloadTick--;
			return;
		}

		int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GrenadeLifetime);

		vec2 ProjStartPos = m_Pos + SavePosion * GetProximityRadius() * 0.75f;
		new CProjectile(
			GameWorld(),
			WEAPON_GRENADE, //Type
			m_Owner, //Owner
			ProjStartPos, //Pos
			SavePosion, //Dir
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

		// m_ReloadTick = 3000 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 40);
		m_ReloadTick = 3 * Server()->TickSpeed();

		return;
	}

	Fire();
}

void CTurret::Fire()
{
	// Turret is under cooldown
	if(m_ReloadTick)
	{
		m_ReloadTick--;
		return;
	}

	CCharacter *pTarget = 0;
	CCharacter *pClosest = (CCharacter *)GameWorld()->FindFirst(CGameWorld::ENTTYPE_CHARACTER);

	// int D = 400.0f+GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretRange;
	int D = 400.f;
	while(pClosest)
	{
		int Dis = distance(pClosest->m_Pos, m_Pos);
		if(Dis <= D && pClosest->GetPlayer()->GetTeam() == TEAM_RED)
		{
			if(!GameServer()->Collision()->IntersectLine(m_Pos, pClosest->m_Pos, 0, 0))
			{
				pTarget = pClosest;
				D = Dis;
			}
		}
		pClosest = (CCharacter *)pClosest->TypeNext();
	}

	if(!pTarget)
	{
		if(m_Type == WEAPON_HAMMER && SavePosion != m_Pos)
			SavePosion = m_Pos;

		return;
	}

	Direction = normalize(pTarget->m_Pos - m_Pos);
	vec2 ProjStartPos = m_Pos + Direction * GetProximityRadius() * 0.75f;

	switch(m_Type)
	{
	case WEAPON_GUN:
	{
		AddTurretExperience();

		int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_GunLifetime);
		new CProjectile(
			GameWorld(),
			WEAPON_GUN, //Type
			m_Owner, //Owner
			ProjStartPos, //Pos
			Direction, //Dir
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

		// m_ReloadTick = 3600 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 30);
		m_ReloadTick = 1.0f * Server()->TickSpeed();

		break;
	}
	case WEAPON_HAMMER:
	{
		if(distance(pTarget->m_Pos, m_Pos) > 30 && distance(pTarget->m_Pos, m_Pos) < 300)
		{
			SavePosion = pTarget->m_Pos;
			pTarget->Core()->m_Vel -= Direction * 1.5;
			return;
		}
		else if(distance(pTarget->m_Pos, m_Pos) < 30)
		{
			GameServer()->CreateHammerHit(pTarget->m_Pos);

			// pTarget->TakeDamage(Direction * 2, 800 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretDmg*2, m_Owner, WEAPON_HAMMER);
			pTarget->TakeDamage(Direction * 2, g_Config.m_PanicHammerTurretInitialDamage, m_Owner, WEAPON_HAMMER);

			SavePosion = m_Pos;
			m_ReloadTick = 20 * Server()->TickSpeed();
			return;
		}
		SavePosion = m_Pos;

		break;
	}
	case WEAPON_LASER:
	{
		vec2 Intersection;
		CCharacter *pTargetChr = GameServer()->m_World.IntersectCharacter(m_Pos, m_Pos2, 1.0f, Intersection, 0x0);

		if(pTargetChr)
			if(pTargetChr->GetPlayer()->GetTeam() == TEAM_RED)
			{
				new CWall(GameWorld(), m_Pos, m_Pos2, m_Owner, 7, 1);
				GameServer()->CreateSound(m_Pos, SOUND_LASER_FIRE);

				// m_ReloadTick = 1800;
				m_ReloadTick = 36 * Server()->TickSpeed();
			}

		break;
	}
	case WEAPON_SHOTGUN:
	{
		// int ShotSpread = 5 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretLevel / 20;
		int ShotSpread = 5;

		float Spreading[16 * 2 + 1];
		for(int i = 0; i < 16 * 2 + 1; i++)
			Spreading[i] = -0.8f + 0.05f * i;

		for(int i = -ShotSpread / 2; i <= ShotSpread / 2; ++i)
		{
			float a = angle(Direction);
			a += Spreading[i + 16];
			float v = 1 - (absolute(i) / (float)ShotSpread) / 2;

			// float Speed = GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretLevel > 25 ? 1.0f : mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);
			float Speed = mix((float)GameServer()->Tuning()->m_ShotgunSpeeddiff, 1.0f, v);

			int Lifetime = (int)(Server()->TickSpeed() * GameServer()->Tuning()->m_ShotgunLifetime);
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

		AddTurretExperience();
		GameServer()->CreateSound(m_Pos, SOUND_SHOTGUN_FIRE);

		// m_ReloadTick = 3800 * Server()->TickSpeed() / (1000 + GameServer()->m_apPlayers[m_Owner]->m_AccData.m_TurretSpeed * 10);
		m_ReloadTick = 2.8f * Server()->TickSpeed();

		break;
	}
	}
}

void CTurret::AddTurretExperience()
{
	// CPlayer* pPlayer = GameServer()->m_apPlayers[m_Owner];
	// pPlayer->m_AccData.m_TurretExp += rand()%3+1;
	// if (pPlayer && pPlayer->m_AccData.m_TurretExp >= pPlayer->m_AccData.m_TurretLevel)
	// {
	// 	pPlayer->m_AccData.m_TurretLevel++;
	// 	pPlayer->m_AccData.m_TurretExp = 0;
	// 	pPlayer->m_AccData.m_TurretMoney++;

	// 	if(pPlayer->m_AccData.m_UserID)
	// 		pPlayer->m_pAccount->Apply();

	// 	char SendExp[64];
	// 	str_format(SendExp, sizeof(SendExp), "Turret's Level-Up! Your turret's level now is: %d", pPlayer->m_AccData.m_TurretLevel);
	// 	GameServer()->SendChatTarget(m_Owner, SendExp);
	// }
}

void CTurret::Snap(int SnappingClient)
{
	if(NetworkClipped(SnappingClient))
		return;

	// SHOTGUN DOST DISPLAY
	int InSize = sizeof(m_inIDs) / sizeof(int);
	for(int i = 0; i < InSize; i++)
	{
		CNetObj_Projectile *pEff = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_inIDs[i], sizeof(CNetObj_Projectile)));
		if(!pEff)
			continue;

		// Less aggressive effect for Hammer & Laser that are slower
		if(m_Type == WEAPON_HAMMER || m_Type == WEAPON_LASER)
		{
			pEff->m_X = (int)(cos(angle(Direction) + pi / 9 * i * 4) * (16.0 + m_ReloadTick / 65) + m_Pos.x);
			pEff->m_Y = (int)(sin(angle(Direction) + pi / 9 * i * 4) * (16.0 + m_ReloadTick / 65) + m_Pos.y);
		}
		else
		{
			pEff->m_X = (int)(cos(angle(Direction) + pi / 9 * i * 4) * (16.0 + m_ReloadTick / 10) + m_Pos.x);
			pEff->m_Y = (int)(sin(angle(Direction) + pi / 9 * i * 4) * (16.0 + m_ReloadTick / 10) + m_Pos.y);
		}

		pEff->m_StartTick = Server()->Tick() - 2;
		pEff->m_Type = WEAPON_SHOTGUN;
	}

	// PROJECTILE AMMO TYPE DISPLAY
	CNetObj_Projectile *pObj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_IDS, sizeof(CNetObj_Projectile)));
	if(!pObj)
		return;

	pObj->m_X = (int)m_Pos.x;
	pObj->m_Y = (int)m_Pos.y;
	pObj->m_StartTick = Server()->Tick() - 3;
	pObj->m_Type = m_Type;

	// LASER WALL ON POSITION 1
	CNetObj_Laser *pObj2 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_IDC, sizeof(CNetObj_Laser)));
	if(!pObj2)
		return;

	pObj2->m_X = (int)m_Pos.x;
	pObj2->m_Y = (int)m_Pos.y;

	pObj2->m_FromX = (int)m_Pos.x + Direction.x * 32;
	pObj2->m_FromY = (int)m_Pos.y + Direction.y * 32;

	if(pObj2->m_FromX < 1 && pObj2->m_FromY < 1)
	{
		pObj2->m_FromX = (int)m_Pos.x;
		pObj2->m_FromY = (int)m_Pos.y;
	}

	pObj2->m_StartTick = Server()->Tick();

	switch(m_Type)
	{
	case WEAPON_HAMMER:
		pObj2->m_FromX = (int)SavePosion.x;
		pObj2->m_FromY = (int)SavePosion.y;

		pObj2->m_StartTick = Server()->Tick() - 5;
		break;
	case WEAPON_GUN:
	case WEAPON_SHOTGUN:
		break;
	case WEAPON_GRENADE:
		pObj2->m_X = (int)InitGrenadePos.x;
		pObj2->m_Y = (int)InitGrenadePos.y;

		pObj2->m_FromX = (int)m_Pos2.x;
		pObj2->m_FromY = (int)m_Pos2.y;

		pObj2->m_StartTick = Server()->Tick() - 2;
		break;
	case WEAPON_LASER:
		Direction = normalize(m_Pos2 - m_Pos);
		pObj2->m_FromX = (int)m_Pos.x + Direction.x * 32;
		pObj2->m_FromY = (int)m_Pos.y + Direction.y * 32;
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