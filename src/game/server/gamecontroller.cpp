/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/shared/config.h>

#include <game/generated/protocol.h>
#include <game/mapitems.h>
#include <game/teamscore.h>
#include <game/version.h>

#include "gamecontext.h"
#include "gamecontroller.h"
#include "player.h"

#include "entities/character.h"
#include "entities/door.h"
#include "entities/dragger.h"
#include "entities/gun.h"
#include "entities/light.h"
#include "entities/pickup.h"
#include "entities/projectile.h"

#include "entities/panicdoor.h"

IGameController::IGameController(class CGameContext *pGameServer)
{
	m_pGameServer = pGameServer;
	m_pConfig = m_pGameServer->Config();
	m_pServer = m_pGameServer->Server();
	m_pGameType = "unknown";

	//
	DoWarmup(g_Config.m_SvWarmup);
	m_GameOverTick = -1;
	m_SuddenDeath = 0;
	m_RoundStartTick = Server()->Tick();
	m_RoundCount = 0;
	m_GameFlags = GAMEFLAG_TEAMS;
	m_aMapWish[0] = 0;

	m_UnbalancedTick = -1;
	m_ForceBalanced = false;

	m_aNumSpawnPoints[0] = 0;
	m_aNumSpawnPoints[1] = 0;
	m_aNumSpawnPoints[2] = 0;

	m_CurrentRecord = 0;

	// DDNet-Skeleton
	m_aQueuedMap[0] = 0;
	m_aPreviousMap[0] = 0;

	if(IsTeamplay())
	{
		m_aTeamscore[TEAM_RED] = m_aTeamscore[TEAM_BLUE] = 0;
	}

	// ZombPanic
	m_ZombiesSelected = false;
	for(int i = 0; i < MAX_DOORS; i++)
	{
		m_Door[i].m_State = (i > MAX_DOORS / 2) ? DOOR_ZOMBIE_OPEN : DOOR_CLOSED;
		m_Door[i].m_Tick = 0;
		m_Door[i].m_OpenTime = g_Config.m_PanicDoorTime;
		m_Door[i].m_CloseTime = g_Config.m_PanicZombieDoorDelay;
		m_Door[i].m_ReopenTime = g_Config.m_PanicZombieDoorTime;
	}
}

IGameController::~IGameController() = default;

void IGameController::DoActivityCheck()
{
	if(g_Config.m_SvInactiveKickTime == 0)
		return;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
#ifdef CONF_DEBUG
		if(g_Config.m_DbgDummies)
		{
			if(i >= MAX_CLIENTS - g_Config.m_DbgDummies)
				break;
		}
#endif
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS && Server()->GetAuthedState(i) == AUTHED_NO)
		{
			if(Server()->Tick() > GameServer()->m_apPlayers[i]->m_LastActionTick + g_Config.m_SvInactiveKickTime * Server()->TickSpeed() * 60)
			{
				switch(g_Config.m_SvInactiveKick)
				{
				case 0:
				{
					// move player to spectator
					DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 1:
				{
					// move player to spectator if the reserved slots aren't filled yet, kick him otherwise
					int Spectators = 0;
					for(auto &pPlayer : GameServer()->m_apPlayers)
						if(pPlayer && pPlayer->GetTeam() == TEAM_SPECTATORS)
							++Spectators;
					if(Spectators >= g_Config.m_SvSpectatorSlots)
						Server()->Kick(i, "Kicked for inactivity");
					else
						DoTeamChange(GameServer()->m_apPlayers[i], TEAM_SPECTATORS);
				}
				break;
				case 2:
				{
					// kick the player
					Server()->Kick(i, "Kicked for inactivity");
				}
				}
			}
		}
	}
}

float IGameController::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos, int DDTeam)
{
	float Score = 0.0f;
	CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
	for(; pC; pC = (CCharacter *)pC->TypeNext())
	{
		// ignore players in other teams
		if(GameServer()->GetDDRaceTeam(pC->GetPlayer()->GetCID()) != DDTeam)
			continue;

		float d = distance(Pos, pC->m_Pos);
		Score += d == 0 ? 1000000000.0f : 1.0f / d;
	}

	return Score;
}

void IGameController::EvaluateSpawnType(CSpawnEval *pEval, int Type, int DDTeam)
{
	// j == 0: Find an empty slot, j == 1: Take any slot if no empty one found
	for(int j = 0; j < 2 && !pEval->m_Got; j++)
	{
		// get spawn point
		for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
		{
			vec2 P = m_aaSpawnPoints[Type][i];

			if(j == 0)
			{
				// check if the position is occupado
				CCharacter *apEnts[MAX_CLIENTS];
				int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity **)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
				vec2 aPositions[5] = {vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f)}; // start, left, up, right, down
				int Result = -1;
				for(int Index = 0; Index < 5 && Result == -1; ++Index)
				{
					Result = Index;
					if(!GameServer()->m_World.m_Core.m_aTuning[0].m_PlayerCollision)
						break;
					for(int c = 0; c < Num; ++c)
						if(GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i] + aPositions[Index]) ||
							distance(apEnts[c]->m_Pos, m_aaSpawnPoints[Type][i] + aPositions[Index]) <= apEnts[c]->GetProximityRadius())
						{
							Result = -1;
							break;
						}
				}
				if(Result == -1)
					continue; // try next spawn point

				P += aPositions[Result];
			}

			float S = EvaluateSpawnPos(pEval, P, DDTeam);
			if(!pEval->m_Got || (j == 0 && pEval->m_Score > S))
			{
				pEval->m_Got = true;
				pEval->m_Score = S;
				pEval->m_Pos = P;
			}
		}
	}
}

bool IGameController::CanSpawn(int Team, vec2 *pOutPos, int DDTeam)
{
	CSpawnEval Eval;

	// spectators can't spawn
	if(Team == TEAM_SPECTATORS)
		return false;

	if(IsTeamplay())
	{
		Eval.m_FriendlyTeam = Team;

		// first try own team spawn, then normal spawn and then enemy
		EvaluateSpawnType(&Eval, 1 + Team, DDTeam);
		if(!Eval.m_Got)
		{
			EvaluateSpawnType(&Eval, 0, DDTeam);
			if(!Eval.m_Got)
				EvaluateSpawnType(&Eval, 1 + (1 - Team), DDTeam);
		}
	}
	else
	{
		EvaluateSpawnType(&Eval, 0, DDTeam);
		EvaluateSpawnType(&Eval, 1, DDTeam);
		EvaluateSpawnType(&Eval, 2, DDTeam);
	}

	*pOutPos = Eval.m_Pos;
	return Eval.m_Got;
}

bool IGameController::OnEntity(int Index, vec2 Pos, int Layer, int Flags, int Number)
{
	if(Index < 0)
		return false;

	int x, y;
	x = (Pos.x - 16.0f) / 32.0f;
	y = (Pos.y - 16.0f) / 32.0f;
	int aSides[8];
	aSides[0] = GameServer()->Collision()->Entity(x, y + 1, Layer);
	aSides[1] = GameServer()->Collision()->Entity(x + 1, y + 1, Layer);
	aSides[2] = GameServer()->Collision()->Entity(x + 1, y, Layer);
	aSides[3] = GameServer()->Collision()->Entity(x + 1, y - 1, Layer);
	aSides[4] = GameServer()->Collision()->Entity(x, y - 1, Layer);
	aSides[5] = GameServer()->Collision()->Entity(x - 1, y - 1, Layer);
	aSides[6] = GameServer()->Collision()->Entity(x - 1, y, Layer);
	aSides[7] = GameServer()->Collision()->Entity(x - 1, y + 1, Layer);

	// ZombPanic
	// Tele Layer first

	if(Layer == LAYER_TELE)
	{
		if(Index == TILE_TELEINEVIL)
		{
			CPanicDoor *pDoor = new CPanicDoor(&GameServer()->m_World, Number);
			pDoor->m_Pos = Pos;

			return true;
		}

		if(Index == TILE_TELEIN)
		{
			CPanicDoor *pDoor = new CPanicDoor(&GameServer()->m_World, Number + (MAX_DOORS / 2));
			pDoor->m_Pos = Pos;

			return true;
		}
	}

	if(Index >= ENTITY_SPAWN && Index <= ENTITY_SPAWN_BLUE)
	{
		int Type = Index - ENTITY_SPAWN;
		m_aaSpawnPoints[Type][m_aNumSpawnPoints[Type]] = Pos;
		m_aNumSpawnPoints[Type] = minimum(m_aNumSpawnPoints[Type] + 1, (int)std::size(m_aaSpawnPoints[0]));
	}

	else if(Index == ENTITY_DOOR)
	{
		for(int i = 0; i < 8; i++)
		{
			if(aSides[i] >= ENTITY_LASER_SHORT && aSides[i] <= ENTITY_LASER_LONG)
			{
				new CDoor(
					&GameServer()->m_World, //GameWorld
					Pos, //Pos
					pi / 4 * i, //Rotation
					32 * 3 + 32 * (aSides[i] - ENTITY_LASER_SHORT) * 3, //Length
					Number //Number
				);
			}
		}
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN_EX)
	{
		int Dir;
		if(!Flags)
			Dir = 0;
		else if(Flags == ROTATION_90)
			Dir = 1;
		else if(Flags == ROTATION_180)
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * (pi / 2);
		CProjectile *pBullet = new CProjectile(
			&GameServer()->m_World,
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			true, //Explosive
			0, //Force
			(g_Config.m_SvShotgunBulletSound) ? SOUND_GRENADE_EXPLODE : -1, //SoundImpact
			Layer,
			Number);
		pBullet->SetBouncing(2 - (Dir % 2));
	}
	else if(Index == ENTITY_CRAZY_SHOTGUN)
	{
		int Dir;
		if(!Flags)
			Dir = 0;
		else if(Flags == (TILEFLAG_ROTATE))
			Dir = 1;
		else if(Flags == (TILEFLAG_VFLIP | TILEFLAG_HFLIP))
			Dir = 2;
		else
			Dir = 3;
		float Deg = Dir * (pi / 2);
		CProjectile *pBullet = new CProjectile(
			&GameServer()->m_World,
			WEAPON_SHOTGUN, //Type
			-1, //Owner
			Pos, //Pos
			vec2(sin(Deg), cos(Deg)), //Dir
			-2, //Span
			true, //Freeze
			false, //Explosive
			0,
			SOUND_GRENADE_EXPLODE,
			Layer,
			Number);
		pBullet->SetBouncing(2 - (Dir % 2));
	}

	int Type = -1;
	int SubType = 0;

	if(Index == ENTITY_ARMOR_1)
		Type = POWERUP_ARMOR;
	else if(Index == ENTITY_ARMOR_SHOTGUN)
		Type = POWERUP_ARMOR_SHOTGUN;
	else if(Index == ENTITY_ARMOR_GRENADE)
		Type = POWERUP_ARMOR_GRENADE;
	else if(Index == ENTITY_ARMOR_NINJA)
		Type = POWERUP_ARMOR_NINJA;
	else if(Index == ENTITY_ARMOR_LASER)
		Type = POWERUP_ARMOR_LASER;
	else if(Index == ENTITY_HEALTH_1)
		Type = POWERUP_HEALTH;
	else if(Index == ENTITY_WEAPON_SHOTGUN)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_SHOTGUN;
	}
	else if(Index == ENTITY_WEAPON_GRENADE)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_GRENADE;
	}
	else if(Index == ENTITY_WEAPON_LASER)
	{
		Type = POWERUP_WEAPON;
		SubType = WEAPON_LASER;
	}
	else if(Index == ENTITY_POWERUP_NINJA)
	{
		Type = POWERUP_NINJA;
		SubType = WEAPON_NINJA;
	}
	else if(Index >= ENTITY_LASER_FAST_CCW && Index <= ENTITY_LASER_FAST_CW)
	{
		int aSides2[8];
		aSides2[0] = GameServer()->Collision()->Entity(x, y + 2, Layer);
		aSides2[1] = GameServer()->Collision()->Entity(x + 2, y + 2, Layer);
		aSides2[2] = GameServer()->Collision()->Entity(x + 2, y, Layer);
		aSides2[3] = GameServer()->Collision()->Entity(x + 2, y - 2, Layer);
		aSides2[4] = GameServer()->Collision()->Entity(x, y - 2, Layer);
		aSides2[5] = GameServer()->Collision()->Entity(x - 2, y - 2, Layer);
		aSides2[6] = GameServer()->Collision()->Entity(x - 2, y, Layer);
		aSides2[7] = GameServer()->Collision()->Entity(x - 2, y + 2, Layer);

		float AngularSpeed = 0.0f;
		int Ind = Index - ENTITY_LASER_STOP;
		int M;
		if(Ind < 0)
		{
			Ind = -Ind;
			M = 1;
		}
		else if(Ind == 0)
			M = 0;
		else
			M = -1;

		if(Ind == 0)
			AngularSpeed = 0.0f;
		else if(Ind == 1)
			AngularSpeed = pi / 360;
		else if(Ind == 2)
			AngularSpeed = pi / 180;
		else if(Ind == 3)
			AngularSpeed = pi / 90;
		AngularSpeed *= M;

		for(int i = 0; i < 8; i++)
		{
			if(aSides[i] >= ENTITY_LASER_SHORT && aSides[i] <= ENTITY_LASER_LONG)
			{
				CLight *pLight = new CLight(&GameServer()->m_World, Pos, pi / 4 * i, 32 * 3 + 32 * (aSides[i] - ENTITY_LASER_SHORT) * 3, Layer, Number);
				pLight->m_AngularSpeed = AngularSpeed;
				if(aSides2[i] >= ENTITY_LASER_C_SLOW && aSides2[i] <= ENTITY_LASER_C_FAST)
				{
					pLight->m_Speed = 1 + (aSides2[i] - ENTITY_LASER_C_SLOW) * 2;
					pLight->m_CurveLength = pLight->m_Length;
				}
				else if(aSides2[i] >= ENTITY_LASER_O_SLOW && aSides2[i] <= ENTITY_LASER_O_FAST)
				{
					pLight->m_Speed = 1 + (aSides2[i] - ENTITY_LASER_O_SLOW) * 2;
					pLight->m_CurveLength = 0;
				}
				else
					pLight->m_CurveLength = pLight->m_Length;
			}
		}
	}
	else if(Index >= ENTITY_DRAGGER_WEAK && Index <= ENTITY_DRAGGER_STRONG)
	{
		new CDragger(&GameServer()->m_World, Pos, Index - ENTITY_DRAGGER_WEAK + 1, false, Layer, Number);
	}
	else if(Index >= ENTITY_DRAGGER_WEAK_NW && Index <= ENTITY_DRAGGER_STRONG_NW)
	{
		new CDragger(&GameServer()->m_World, Pos, Index - ENTITY_DRAGGER_WEAK_NW + 1, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAE)
	{
		new CGun(&GameServer()->m_World, Pos, false, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAF)
	{
		new CGun(&GameServer()->m_World, Pos, true, false, Layer, Number);
	}
	else if(Index == ENTITY_PLASMA)
	{
		new CGun(&GameServer()->m_World, Pos, true, true, Layer, Number);
	}
	else if(Index == ENTITY_PLASMAU)
	{
		new CGun(&GameServer()->m_World, Pos, false, false, Layer, Number);
	}

	if(Type != -1)
	{
		CPickup *pPickup = new CPickup(&GameServer()->m_World, Type, SubType, Layer, Number);
		pPickup->m_Pos = Pos;
		return true;
	}

	return false;
}

void IGameController::OnPlayerConnect(CPlayer *pPlayer)
{
	int ClientID = pPlayer->GetCID();
	pPlayer->Respawn();

	if(!Server()->ClientPrevIngame(ClientID))
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), pPlayer->GetTeam());
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

		// DDNet-Skeleton
		str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), GetTeamName(pPlayer->GetTeam()));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);

		GameServer()->SendChatTarget(ClientID, GAME_MOD_NAME " Version: " GAME_MOD_VERSION);
		GameServer()->SendChatTarget(ClientID, "Say /info and make sure to read our /rules");
	}
}

void IGameController::OnPlayerDisconnect(class CPlayer *pPlayer, const char *pReason)
{
	pPlayer->OnDisconnect();
	int ClientID = pPlayer->GetCID();
	if(Server()->ClientIngame(ClientID))
	{
		char aBuf[512];
		if(pReason && *pReason)
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game (%s)", Server()->ClientName(ClientID), pReason);
		else
			str_format(aBuf, sizeof(aBuf), "'%s' has left the game", Server()->ClientName(ClientID));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf, -1, CGameContext::CHAT_SIX);

		str_format(aBuf, sizeof(aBuf), "leave player='%d:%s'", ClientID, Server()->ClientName(ClientID));
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "game", aBuf);
	}
}

void IGameController::EndRound()
{
	if(m_Warmup || m_GameOverTick != -1) // game can't end when we are running warmup
		return;

	// ZOMBIES WIN
	if(!NumHumans())
	{
		m_aTeamscore[TEAM_RED] = 999;

		// ZOMBPANIC-TODO: MIGRATE THIS PART

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameServer()->GetPlayerChar(i) && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_RED)
			{
				GameServer()->m_apPlayers[i]->m_Score += 2;
			}
		}
	}

	// HUMANS WIN
	if(
		!NumZombies() ||
		(NumHumans() && (g_Config.m_SvTimeLimit > 0 && (Server()->Tick() - m_RoundStartTick) >= g_Config.m_SvTimeLimit * Server()->TickSpeed()) && !m_SuddenDeath))
	{
		m_aTeamscore[TEAM_BLUE] = 999;

		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			// ZOMBPANIC-TODO: MIGRATE THIS PART

			if(GameServer()->GetPlayerChar(i) && GameServer()->m_apPlayers[i]->GetTeam() == TEAM_BLUE)
			{
				GameServer()->m_apPlayers[i]->m_Score += 10;
			}
		}
	}

	GameServer()->m_World.m_Paused = true;
	m_GameOverTick = Server()->Tick();
}

void IGameController::ResetGame()
{
	GameServer()->m_World.m_ResetRequested = true;
}

const char *IGameController::GetTeamName(int Team)
{
	// DDNet-Skeleton
	if(IsTeamplay())
	{
		if(Team == TEAM_RED)
			return "Zombies";
		else if(Team == TEAM_BLUE)
			return "Humans";
	}
	else
	{
		if(Team == 0)
			return "Game";
	}

	return "Spectators";
}

//static bool IsSeparator(char c) { return c == ';' || c == ' ' || c == ',' || c == '\t'; }

void IGameController::StartRound()
{
	ResetGame();

	m_RoundStartTick = Server()->Tick();
	m_GameOverTick = -1;
	GameServer()->m_World.m_Paused = false;
	m_ForceBalanced = false;
	Server()->DemoRecorder_HandleAutoStart();

	// Do warmup before start round or the round will finish every time since there is no zombies
	DoWarmup(g_Config.m_SvWarmup);

	// Reset doors && Reset all players to human and reset team score
	ResetDoors();
	ResetZombies();
	m_aTeamscore[TEAM_RED] = m_aTeamscore[TEAM_BLUE] = 0;
	m_ZombiesSelected = false;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags & GAMEFLAG_TEAMS);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void IGameController::ChangeMap(const char *pToMap)
{
	Server()->ChangeMap(pToMap);
}

void IGameController::OnReset()
{
	for(auto &pPlayer : GameServer()->m_apPlayers)
		if(pPlayer)
			pPlayer->Respawn();
}

int IGameController::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
	if(pVictim->GetPlayer()->GetTeam() == TEAM_BLUE && !m_Warmup)
	{
		pVictim->GetPlayer()->SetZombie();
		GameServer()->SendChatTarget(pVictim->GetPlayer()->GetCID(), "You are now a zombie! Eat some brains!");
	}

	return 0;
}

void IGameController::OnCharacterSpawn(class CCharacter *pChr)
{
	pChr->GiveWeapon(WEAPON_HAMMER);

	// Define maximum health
	pChr->SetAutoHealthLimit();
	pChr->IncreaseHealth(9999);

	// Give gun to human
	// OR
	// Zombie will spawn sleeping if there is warmup
	if(pChr->GetPlayer()->GetTeam() == TEAM_BLUE)
		pChr->GiveWeapon(WEAPON_GUN, false, 10);
	else if(m_Warmup)
		pChr->GetPlayer()->Pause(1, true);
}

void IGameController::HandleCharacterTiles(CCharacter *pChr, int MapIndex)
{
	// Do nothing by default
}

void IGameController::DoWarmup(int Seconds)
{
	if(Seconds < 0)
		m_LastWarmup = m_Warmup = 0;
	else
		m_LastWarmup = m_Warmup = Seconds * Server()->TickSpeed();
}

bool IGameController::IsForceBalanced()
{
	return false;
}

bool IGameController::CanBeMovedOnBalance(int ClientID)
{
	return true;
}

void IGameController::Tick()
{
	// do warmup
	if(!GameServer()->m_World.m_Paused && m_Warmup)
	{
		// Only countdown warmup with players || Reset everything when there is no players
		if(NumPlayers() > 1)
		{
			m_Warmup--;

			// Select initial zombies
			if(!m_ZombiesSelected)
			{
				// Kill humans to avoid door not opening since the player already crossed the trigger before zombie
				// Reset doors && Reset the weapons respawn
				// Reset everything...
				StartRound();

				int ZAtStart = (int)NumPlayers() / (int)g_Config.m_PanicZombieRatio;
				if(!ZAtStart)
					ZAtStart = 1;

				for(; ZAtStart; ZAtStart--)
				{
					RandomZombie();
				}

				m_ZombiesSelected = true;
			}
		}
		else
		{
			// Reset lasts zombies and round count
			// It will reset completely the context of the server
			m_LastZombie = m_LastZombie2 = -1;

			if(m_RoundCount != 0)
				m_RoundCount = 0;

			// This mean that the warm up happened
			// So its needs to start a new round to reset the warm up, zombie doors, zombie selected, players and etc
			if(m_Warmup != m_LastWarmup)
				StartRound();

			for(auto &pPlayer : GameServer()->m_apPlayers)
			{
				if(pPlayer && pPlayer->GetCharacter())
					pPlayer->GetCharacter()->SendPersonalBroadcast("At least 2 players are needed to start the round");
			}
		}

		// Round started. Wake up the initial zombies!
		if (NumPlayers() > 1 && !m_Warmup)
		{
			if (GameServer()->m_apPlayers[m_LastZombie]) { // Prevent crash if the zombie disconnected
				GameServer()->m_apPlayers[m_LastZombie]->Pause(0, true);
			}
		}
	}

	if(m_GameOverTick != -1)
	{
		// game over.. wait for restart
		if(Server()->Tick() > m_GameOverTick + Server()->TickSpeed() * 10)
		{
			StartRound();
			m_RoundCount++;

			if(m_RoundCount >= g_Config.m_SvRoundsPerMap)
			{
				CycleMap();
			}
		}
	}

	// game is Paused
	if(GameServer()->m_World.m_Paused)
		++m_RoundStartTick;

	// do team-balancing
	DoTeamBalancingCheck();

	DoActivityCheck();

	DoWinCheck();

	for(int i = 0; i < MAX_DOORS; i++)
	{
		if(m_Door[i].m_Tick <= 0)
		{
			continue;
		}

		if(m_Door[i].m_State == DOOR_CLOSED)
		{
			if(m_Door[i].m_Tick <= Server()->Tick())
			{
				SetDoorState(i, DOOR_OPEN);
				m_Door[i].m_Tick = 0;
			}
		}
		else if(m_Door[i].m_Tick <= Server()->Tick())
		{
			if(m_Door[i].m_State == DOOR_ZOMBIE_CLOSING)
			{
				SetDoorState(i, DOOR_ZOMBIE_CLOSED);
				m_Door[i].m_Tick = Server()->Tick() + Server()->TickSpeed() * GetDoorTime(i);
			}
			else if(m_Door[i].m_State == DOOR_ZOMBIE_CLOSED)
			{
				SetDoorState(i, DOOR_ZOMBIE_REOPENED);
				m_Door[i].m_Tick = 0;
			}
		}
	}
}

void IGameController::Snap(int SnappingClient)
{
	CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
	if(!pGameInfoObj)
		return;

	pGameInfoObj->m_GameFlags = m_GameFlags;
	pGameInfoObj->m_GameStateFlags = 0;
	if(m_GameOverTick != -1)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
	if(m_SuddenDeath)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
	if(GameServer()->m_World.m_Paused)
		pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
	pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
	pGameInfoObj->m_WarmupTimer = m_Warmup;

	pGameInfoObj->m_ScoreLimit = g_Config.m_SvScoreLimit;
	pGameInfoObj->m_TimeLimit = g_Config.m_SvTimeLimit;

	pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMapRotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
	pGameInfoObj->m_RoundCurrent = m_RoundCount + 1;

	// DDNet-Skeleton
	// The SixUp client can't receive this 0.6 message or it will not connect
	if(!Server()->IsSixup(SnappingClient) && IsTeamplay())
	{
		CNetObj_GameData *pGameDataObj = (CNetObj_GameData *)Server()->SnapNewItem(NETOBJTYPE_GAMEDATA, 0, sizeof(CNetObj_GameData));
		if(!pGameDataObj)
			return;
		pGameDataObj->m_TeamscoreRed = (m_aTeamscore[TEAM_RED] == 999) ? m_aTeamscore[TEAM_RED] : NumZombies();
		pGameDataObj->m_TeamscoreBlue = (m_aTeamscore[TEAM_BLUE] == 999) ? m_aTeamscore[TEAM_BLUE] : NumHumans();
		pGameDataObj->m_FlagCarrierRed = -1;
		pGameDataObj->m_FlagCarrierBlue = -1;
	}
	// DDNet-Skeleton Finish

	CCharacter *pChr;
	CPlayer *pPlayer = SnappingClient != SERVER_DEMO_CLIENT ? GameServer()->m_apPlayers[SnappingClient] : 0;
	CPlayer *pPlayer2;

	if(pPlayer && (pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER || pPlayer->m_TimerType == CPlayer::TIMERTYPE_GAMETIMER_AND_BROADCAST) && pPlayer->GetClientVersion() >= VERSION_DDNET_GAMETICK)
	{
		if((pPlayer->GetTeam() == TEAM_SPECTATORS || pPlayer->IsPaused()) && pPlayer->m_SpectatorID != SPEC_FREEVIEW && (pPlayer2 = GameServer()->m_apPlayers[pPlayer->m_SpectatorID]))
		{
			if((pChr = pPlayer2->GetCharacter()) && pChr->m_DDRaceState == DDRACE_STARTED)
			{
				pGameInfoObj->m_WarmupTimer = -pChr->m_StartTime;
				pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_RACETIME;
			}
		}
		else if((pChr = pPlayer->GetCharacter()) && pChr->m_DDRaceState == DDRACE_STARTED)
		{
			pGameInfoObj->m_WarmupTimer = -pChr->m_StartTime;
			pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_RACETIME;
		}
	}

	CNetObj_GameInfoEx *pGameInfoEx = (CNetObj_GameInfoEx *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFOEX, 0, sizeof(CNetObj_GameInfoEx));
	if(!pGameInfoEx)
		return;

	pGameInfoEx->m_Flags =
		// GAMEINFOFLAG_TIMESCORE |
		// GAMEINFOFLAG_GAMETYPE_RACE |
		// GAMEINFOFLAG_GAMETYPE_DDRACE |
		// GAMEINFOFLAG_GAMETYPE_DDNET |
		// GAMEINFOFLAG_UNLIMITED_AMMO |
		// GAMEINFOFLAG_RACE_RECORD_MESSAGE |
		GAMEINFOFLAG_ALLOW_EYE_WHEEL |
		GAMEINFOFLAG_ALLOW_HOOK_COLL |
		// GAMEINFOFLAG_ALLOW_ZOOM |
		// GAMEINFOFLAG_BUG_DDRACE_GHOST |
		GAMEINFOFLAG_BUG_DDRACE_INPUT |
		// GAMEINFOFLAG_PREDICT_DDRACE |
		GAMEINFOFLAG_PREDICT_DDRACE_TILES |
		GAMEINFOFLAG_ENTITIES_DDNET |
		GAMEINFOFLAG_ENTITIES_DDRACE |
		GAMEINFOFLAG_ENTITIES_RACE |
		// GAMEINFOFLAG_RACE |
		GAMEINFOFLAG_GAMETYPE_PLUS;
	pGameInfoEx->m_Flags2 = GAMEINFOFLAG2_HUD_DDRACE;
	if(g_Config.m_SvNoWeakHookAndBounce)
		pGameInfoEx->m_Flags2 |= GAMEINFOFLAG2_NO_WEAK_HOOK_AND_BOUNCE;
	pGameInfoEx->m_Version = GAMEINFO_CURVERSION;

	if(Server()->IsSixup(SnappingClient))
	{
		protocol7::CNetObj_GameData *pGameData = static_cast<protocol7::CNetObj_GameData *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATA, 0, sizeof(protocol7::CNetObj_GameData)));
		if(!pGameData)
			return;

		pGameData->m_GameStartTick = m_RoundStartTick;
		pGameData->m_GameStateFlags = 0;
		if(m_GameOverTick != -1)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_GAMEOVER;
		if(m_SuddenDeath)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_SUDDENDEATH;
		if(GameServer()->m_World.m_Paused)
			pGameData->m_GameStateFlags |= protocol7::GAMESTATEFLAG_PAUSED;

		pGameData->m_GameStateEndTick = 0;

		// protocol7::CNetObj_GameDataRace *pRaceData = static_cast<protocol7::CNetObj_GameDataRace *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATARACE, 0, sizeof(protocol7::CNetObj_GameDataRace)));
		// if(!pRaceData)
		// 	return;

		// pRaceData->m_BestTime = round_to_int(m_CurrentRecord * 1000);
		// pRaceData->m_Precision = 0;
		// pRaceData->m_RaceFlags = protocol7::RACEFLAG_HIDE_KILLMSG | protocol7::RACEFLAG_KEEP_WANTED_WEAPON;

		// DDNet-Skeleton
		if(IsTeamplay())
		{
			protocol7::CNetObj_GameDataTeam *pGameDataTeam = static_cast<protocol7::CNetObj_GameDataTeam *>(Server()->SnapNewItem(-protocol7::NETOBJTYPE_GAMEDATATEAM, 0, sizeof(protocol7::CNetObj_GameDataTeam)));
			if(!pGameDataTeam)
				return;
			pGameDataTeam->m_TeamscoreRed = (m_aTeamscore[TEAM_RED] == 999) ? m_aTeamscore[TEAM_RED] : NumZombies();
			pGameDataTeam->m_TeamscoreBlue = (m_aTeamscore[TEAM_BLUE] == 999) ? m_aTeamscore[TEAM_BLUE] : NumHumans();
		}

		// This pack message can be executed every snap? It doesn't look like it can, but it works.
		protocol7::CNetMsg_Sv_GameInfo GameInfoMsg;
		GameInfoMsg.m_GameFlags = m_GameFlags;
		GameInfoMsg.m_ScoreLimit = g_Config.m_SvScoreLimit;
		GameInfoMsg.m_TimeLimit = g_Config.m_SvTimeLimit;
		GameInfoMsg.m_MatchNum = (str_length(g_Config.m_SvMapRotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;

		GameInfoMsg.m_MatchCurrent = m_RoundCount + 1;

		// protocol7::CNetMsg_Sv_GameInfo GameInfoMsgNoRace = GameInfoMsg;
		// GameInfoMsgNoRace.m_GameFlags &= ~protocol7::GAMEFLAG_RACE;

		Server()->SendPackMsg(&GameInfoMsg, MSGFLAG_VITAL | MSGFLAG_NORECORD, SnappingClient);
	}

	if(!GameServer()->Switchers().empty())
	{
		int Team = pPlayer && pPlayer->GetCharacter() ? pPlayer->GetCharacter()->Team() : 0;

		if(pPlayer && (pPlayer->GetTeam() == TEAM_SPECTATORS || pPlayer->IsPaused()) && pPlayer->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPlayer->m_SpectatorID] && GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetCharacter())
			Team = GameServer()->m_apPlayers[pPlayer->m_SpectatorID]->GetCharacter()->Team();

		if(Team == TEAM_SUPER)
			return;

		CNetObj_SwitchState *pSwitchState = static_cast<CNetObj_SwitchState *>(Server()->SnapNewItem(NETOBJTYPE_SWITCHSTATE, Team, sizeof(CNetObj_SwitchState)));
		if(!pSwitchState)
			return;

		pSwitchState->m_HighestSwitchNumber = clamp((int)GameServer()->Switchers().size() - 1, 0, 255);
		mem_zero(pSwitchState->m_aStatus, sizeof(pSwitchState->m_aStatus));

		std::vector<std::pair<int, int>> vEndTicks; // <EndTick, SwitchNumber>

		for(int i = 0; i <= pSwitchState->m_HighestSwitchNumber; i++)
		{
			int Status = (int)GameServer()->Switchers()[i].m_aStatus[Team];
			pSwitchState->m_aStatus[i / 32] |= (Status << (i % 32));

			int EndTick = GameServer()->Switchers()[i].m_aEndTick[Team];
			if(EndTick > 0 && EndTick < Server()->Tick() + 3 * Server()->TickSpeed() && GameServer()->Switchers()[i].m_aLastUpdateTick[Team] < Server()->Tick())
			{
				// only keep track of EndTicks that have less than three second left and are not currently being updated by a player being present on a switch tile, to limit how often these are sent
				vEndTicks.emplace_back(std::pair<int, int>(GameServer()->Switchers()[i].m_aEndTick[Team], i));
			}
		}

		// send the endtick of switchers that are about to toggle back (up to four, prioritizing those with the earliest endticks)
		mem_zero(pSwitchState->m_aSwitchNumbers, sizeof(pSwitchState->m_aSwitchNumbers));
		mem_zero(pSwitchState->m_aEndTicks, sizeof(pSwitchState->m_aEndTicks));

		std::sort(vEndTicks.begin(), vEndTicks.end());
		const int NumTimedSwitchers = minimum((int)vEndTicks.size(), (int)std::size(pSwitchState->m_aEndTicks));

		for(int i = 0; i < NumTimedSwitchers; i++)
		{
			pSwitchState->m_aSwitchNumbers[i] = vEndTicks[i].second;
			pSwitchState->m_aEndTicks[i] = vEndTicks[i].first;
		}
	}
}

int IGameController::GetAutoTeam(int NotThisID)
{
	// this will force the auto balancer to work overtime as well
#ifdef CONF_DEBUG
	if(g_Config.m_DbgStress)
		return 0;
#endif

	if(!m_Warmup)
		return TEAM_RED;
	else
		return TEAM_BLUE;
}

bool IGameController::CanJoinTeam(int Team, int NotThisID)
{
	if(Team == TEAM_SPECTATORS || (GameServer()->m_apPlayers[NotThisID] && GameServer()->m_apPlayers[NotThisID]->GetTeam() != TEAM_SPECTATORS))
		return true;

	int aNumplayers[2] = {0, 0};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && i != NotThisID)
		{
			if(GameServer()->m_apPlayers[i]->GetTeam() >= TEAM_RED && GameServer()->m_apPlayers[i]->GetTeam() <= TEAM_BLUE)
				aNumplayers[GameServer()->m_apPlayers[i]->GetTeam()]++;
		}
	}

	return (aNumplayers[0] + aNumplayers[1]) < Server()->MaxClients() - g_Config.m_SvSpectatorSlots;
}

int IGameController::ClampTeam(int Team)
{
	if(Team < 0)
		return TEAM_SPECTATORS;
	if(IsTeamplay())
		return Team & 1;
	return Team;
}

int64_t IGameController::GetMaskForPlayerWorldEvent(int Asker, int ExceptID)
{
	// Send all world events to everyone by default
	return CmaskAllExceptOne(ExceptID);
}

void IGameController::DoTeamChange(CPlayer *pPlayer, int Team, bool DoChatMsg)
{
	Team = ClampTeam(Team);
	if(Team == pPlayer->GetTeam())
		return;

	pPlayer->SetTeam(Team);
	int ClientID = pPlayer->GetCID();

	char aBuf[128];
	DoChatMsg = false;
	if(DoChatMsg)
	{
		str_format(aBuf, sizeof(aBuf), "'%s' joined the %s", Server()->ClientName(ClientID), GameServer()->m_pController->GetTeamName(Team));
		GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
	}

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' m_Team=%d", ClientID, Server()->ClientName(ClientID), Team);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	// OnPlayerInfoChange(pPlayer);
}

// DDNet-Skeleton
bool IGameController::IsFriendlyFire(int ClientID1, int ClientID2)
{
	if(ClientID1 == ClientID2)
		return false;

	if(IsTeamplay())
	{
		if(!GameServer()->m_apPlayers[ClientID1] || !GameServer()->m_apPlayers[ClientID2])
			return false;

		if(GameServer()->m_apPlayers[ClientID1]->GetTeam() == GameServer()->m_apPlayers[ClientID2]->GetTeam())
			return true;
	}
	return false;
}

bool IGameController::IsTeamplay()
{
	return m_GameFlags & GAMEFLAG_TEAMS;
}

void IGameController::DoWinCheck()
{
	if(m_GameOverTick != -1 || m_Warmup || GameServer()->m_World.m_ResetRequested)
		return;

	// Time Limit check
	if(g_Config.m_SvTimeLimit > 0 && (Server()->Tick() - m_RoundStartTick) >= g_Config.m_SvTimeLimit * Server()->TickSpeed() * 60)
		EndRound();

	// Zombie or Human check
	if(!NumHumans() || !NumZombies())
		EndRound();
}

void IGameController::DoTeamBalancingCheck()
{
	if(!IsTeamplay() && !g_Config.m_SvAutoTeamBalance)
		return;

	if(m_UnbalancedTick == -1 || Server()->Tick() <= m_UnbalancedTick + g_Config.m_SvAutoTeamBalanceTime * Server()->TickSpeed() * 60)
		return;

	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", "auto balancing teams");

	int aT[2] = {0, 0};
	float aTScore[2] = {0, 0};
	float aPScore[MAX_CLIENTS] = {0.0f};
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			aT[GameServer()->m_apPlayers[i]->GetTeam()]++;
			aPScore[i] = GameServer()->m_apPlayers[i]->m_Score * Server()->TickSpeed() * 60.0f /
				     (Server()->Tick() - GameServer()->m_apPlayers[i]->m_ScoreStartTick);
			aTScore[GameServer()->m_apPlayers[i]->GetTeam()] += aPScore[i];
		}
	}

	// are teams unbalanced?
	if(absolute(aT[0] - aT[1]) >= 2)
	{
		int M = (aT[0] > aT[1]) ? 0 : 1;
		int NumBalance = absolute(aT[0] - aT[1]) / 2;

		do
		{
			CPlayer *pP = 0;
			float PD = aTScore[M];
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(!GameServer()->m_apPlayers[i] || !CanBeMovedOnBalance(i))
					continue;
				// remember the player who would cause lowest score-difference
				if(GameServer()->m_apPlayers[i]->GetTeam() == M && (!pP || absolute((aTScore[M ^ 1] + aPScore[i]) - (aTScore[M] - aPScore[i])) < PD))
				{
					pP = GameServer()->m_apPlayers[i];
					PD = absolute((aTScore[M ^ 1] + aPScore[i]) - (aTScore[M] - aPScore[i]));
				}
			}

			// move the player to the other team
			int Temp = pP->m_LastActionTick;
			pP->SetTeam(M ^ 1);
			pP->m_LastActionTick = Temp;

			pP->Respawn();
			pP->m_ForceBalanced = true;
		} while(--NumBalance);

		m_ForceBalanced = true;
	}

	m_UnbalancedTick = -1;
}

void IGameController::QueueMap(const char *pToMap)
{
	str_copy(m_aQueuedMap, pToMap, sizeof(m_aQueuedMap));
}

bool IGameController::IsWordSeparator(char c)
{
	return c == ';' || c == ',' || c == '\t';
}

void IGameController::GetWordFromList(char *pNextWord, const char *pList, int ListIndex)
{
	pList += ListIndex;
	int i = 0;
	while(*pList)
	{
		if(IsWordSeparator(*pList))
			break;
		pNextWord[i] = *pList;
		pList++;
		i++;
	}
	pNextWord[i] = 0;
}

void IGameController::GetMapRotationInfo(CMapRotationInfo *pMapRotationInfo)
{
	pMapRotationInfo->m_MapCount = 0;

	if(!str_length(g_Config.m_SvMapRotation))
		return;

	int PreviousMapNumber = -1;
	const char *pNextMap = g_Config.m_SvMapRotation;
	const char *pCurrentMap = g_Config.m_SvMap;
	const char *pPreviousMap = m_aPreviousMap;
	bool insideWord = false;
	char aBuf[128];
	int i = 0;
	while(*pNextMap)
	{
		if(IsWordSeparator(*pNextMap))
		{
			if(insideWord)
				insideWord = false;
		}
		else // current char is not a seperator
		{
			if(!insideWord)
			{
				insideWord = true;
				pMapRotationInfo->m_MapNameIndices[pMapRotationInfo->m_MapCount] = i;
				GetWordFromList(aBuf, g_Config.m_SvMapRotation, i);
				if(str_comp(aBuf, pCurrentMap) == 0)
					pMapRotationInfo->m_CurrentMapNumber = pMapRotationInfo->m_MapCount;
				if(pPreviousMap[0] && str_comp(aBuf, pPreviousMap) == 0)
					PreviousMapNumber = pMapRotationInfo->m_MapCount;
				pMapRotationInfo->m_MapCount++;
			}
		}
		pNextMap++;
		i++;
	}
	if((pMapRotationInfo->m_CurrentMapNumber < 0) && (PreviousMapNumber >= 0))
	{
		// The current map not found in the list (probably because this map is a custom one)
		// Try to restore the rotation using the name of the previous map
		pMapRotationInfo->m_CurrentMapNumber = PreviousMapNumber;
	}
}

void IGameController::CycleMap()
{
	if(m_aQueuedMap[0] != 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "rotating to a queued map %s", m_aQueuedMap);
		GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
		ChangeMap(m_aQueuedMap);
		m_aQueuedMap[0] = 0;
		m_RoundCount = 0;
		return;
	}

	if(!str_length(g_Config.m_SvMapRotation))
		return;

	// int PlayerCount = Server()->GetActivePlayerCount();

	CMapRotationInfo pMapRotationInfo;
	GetMapRotationInfo(&pMapRotationInfo);

	if(pMapRotationInfo.m_MapCount == 0)
		return;

	char aBuf[256] = {0};
	int i = 0;
	if(g_Config.m_SvMapRotationRandom)
	{
		// handle random maprotation
		int RandInt;
		for(; i < 32; i++)
		{
			RandInt = random_int(0, pMapRotationInfo.m_MapCount - 1);
			GetWordFromList(aBuf, g_Config.m_SvMapRotation, pMapRotationInfo.m_MapNameIndices[RandInt]);
			// int MinPlayers = Server()->GetMinPlayersForMap(aBuf);
			// if (RandInt != pMapRotationInfo.m_CurrentMapNumber && PlayerCount >= MinPlayers)
			if(RandInt != pMapRotationInfo.m_CurrentMapNumber)
				break;
		}
		i = RandInt;
	}
	else
	{
		// handle normal maprotation
		i = pMapRotationInfo.m_CurrentMapNumber + 1;
		for(; i != pMapRotationInfo.m_CurrentMapNumber; i++)
		{
			if(i >= pMapRotationInfo.m_MapCount)
			{
				i = 0;
				if(i == pMapRotationInfo.m_CurrentMapNumber)
					break;
			}
			GetWordFromList(aBuf, g_Config.m_SvMapRotation, pMapRotationInfo.m_MapNameIndices[i]);
			break;
			// int MinPlayers = Server()->GetMinPlayersForMap(aBuf);
			// if (PlayerCount >= MinPlayers)
			// 	break;
		}
	}

	if(i == pMapRotationInfo.m_CurrentMapNumber)
	{
		// couldnt find map with small enough minplayers number
		i++;
		if(i >= pMapRotationInfo.m_MapCount)
			i = 0;
		GetWordFromList(aBuf, g_Config.m_SvMapRotation, pMapRotationInfo.m_MapNameIndices[i]);
	}

	m_RoundCount = 0;

	str_copy(m_aPreviousMap, g_Config.m_SvMap, sizeof(g_Config.m_SvMap));

	char aBufMsg[256];
	str_format(aBufMsg, sizeof(aBufMsg), "rotating map to %s", aBuf);
	GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBufMsg);

	ChangeMap(aBuf);
}

void IGameController::SkipMap()
{
	CycleMap();
}

// ZombPanic
int IGameController::NumPlayers()
{
	int NumPlayers = 0;

	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer && pPlayer->GetTeam() != TEAM_SPECTATORS)
			NumPlayers++;
	}

	return NumPlayers;
}

int IGameController::NumZombies()
{
	int NumZombies = 0;

	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer && pPlayer->GetTeam() == TEAM_RED)
			NumZombies++;
	}

	return NumZombies;
}

int IGameController::NumHumans()
{
	int NumHumans = 0;

	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer && pPlayer->GetTeam() == TEAM_BLUE)
			NumHumans++;
	}

	return NumHumans;
}

void IGameController::ResetZombies()
{
	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(pPlayer)
			pPlayer->ResetZombie();

		if(pPlayer && pPlayer->GetCharacter() && pPlayer->IsPaused())
			pPlayer->Pause(0, true);
	}
}

void IGameController::RandomZombie()
{
	// Count eligible players
	int EligibleAmount = 0;
	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer || m_LastZombie == pPlayer->GetCID() || (NumPlayers() > 2 && m_LastZombie2 == pPlayer->GetCID()))
			continue;

		EligibleAmount++;
	}

	// Select random number between 1 and EligibleAmount
	int ZombieCID = 0;
	int ZombieNumber = random_int(1, EligibleAmount);

	// Match player with selected number
	int EligibleNumber = 1;
	for(auto &pPlayer : GameServer()->m_apPlayers)
	{
		if(!pPlayer || m_LastZombie == pPlayer->GetCID() || (NumPlayers() > 2 && m_LastZombie2 == pPlayer->GetCID()))
			continue;

		// Found player
		if(EligibleNumber == ZombieNumber)
			ZombieCID = pPlayer->GetCID();

		EligibleNumber++;
	}

	m_LastZombie2 = m_LastZombie;
	m_LastZombie = ZombieCID;

	// Convert player to zombie
	// The zombie will be chosen before character spawn. So there is no need to kill
	GameServer()->m_apPlayers[ZombieCID]->SetZombie();
}
void IGameController::OnDoorHoldPoint(int Index)
{
	if(m_Door[Index].m_Tick || !(m_Door[Index].m_State == DOOR_CLOSED) || GetDoorTime(Index) == -1)
		return;

	int const doorTime = GetDoorTime(Index);
	// -1: if doortime is 5 we get a doublemessage (but a 5 seconds door???)
	m_Door[Index].m_Tick = Server()->Tick() + Server()->TickSpeed() * doorTime - 1;

	char aBuf[64];
	str_format(aBuf, sizeof(aBuf), "%d seconds before door opens", doorTime);
	GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
}

void IGameController::OnZombieDoorHoldPoint(int Index)
{
	if(m_Door[Index].m_State > DOOR_ZOMBIE_OPEN || GetDoorTime(Index) == -1)
		return;

	SetDoorState(Index, DOOR_ZOMBIE_CLOSING);

	int const doorTime = GetDoorTime(Index);
	// -1: if doortime is 5 we get a doublemessage (but a 5 seconds door???)
	m_Door[Index].m_Tick = Server()->Tick() + Server()->TickSpeed() * doorTime - 1;

	// char aBuf[64];
	// str_format(aBuf, sizeof(aBuf), "%d seconds before zombie door closes", doorTime);
	// GameServer()->SendChat(-1, CGameContext::CHAT_ALL, aBuf);
}

void IGameController::ResetDoors()
{
	for(int i = 0; i < MAX_DOORS; i++)
	{
		m_Door[i].m_State = (i > MAX_DOORS / 2) ? DOOR_ZOMBIE_OPEN : DOOR_CLOSED;
		m_Door[i].m_Tick = 0;
	}
}

int IGameController::DoorState(int Index)
{
	return m_Door[Index].m_State;
}

void IGameController::SetDoorState(int Index, int State)
{
	m_Door[Index].m_State = State;
}

int IGameController::GetDoorTime(int Index)
{
	if(m_Door[Index].m_State == DOOR_CLOSED)
		return m_Door[Index].m_OpenTime;

	if(m_Door[Index].m_State == DOOR_ZOMBIE_CLOSING || m_Door[Index].m_State == DOOR_ZOMBIE_OPEN || m_Door[Index].m_State == DOOR_OPEN)
		return m_Door[Index].m_CloseTime;

	if(m_Door[Index].m_State == DOOR_ZOMBIE_CLOSED)
		return m_Door[Index].m_ReopenTime;

	return 0;
}