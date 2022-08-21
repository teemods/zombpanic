/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <base/math.h>
#include <base/system.h>
#include <ctime>

#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include "skins.h"

static const char *VANILLA_SKINS[] = {"bluekitty", "bluestripe", "brownbear",
	"cammo", "cammostripes", "coala", "default", "limekitty",
	"pinky", "redbopp", "redstripe", "saddo", "toptri",
	"twinbop", "twintri", "warpaint", "x_ninja", "x_spec"};

static bool IsVanillaSkin(const char *pName)
{
	return std::any_of(std::begin(VANILLA_SKINS), std::end(VANILLA_SKINS), [pName](const char *pVanillaSkin) { return str_comp(pName, pVanillaSkin) == 0; });
}

int CSkins::CGetPngFile::OnCompletion(int State)
{
	State = CHttpRequest::OnCompletion(State);

	if(State != HTTP_ERROR && State != HTTP_ABORTED && !m_pSkins->LoadSkinPNG(m_Info, Dest(), Dest(), IStorage::TYPE_SAVE))
	{
		State = HTTP_ERROR;
	}
	return State;
}

CSkins::CGetPngFile::CGetPngFile(CSkins *pSkins, const char *pUrl, IStorage *pStorage, const char *pDest) :
	CHttpRequest(pUrl),
	m_pSkins(pSkins)
{
	WriteToFile(pStorage, pDest, IStorage::TYPE_SAVE);
	Timeout(CTimeout{0, 0, 0, 0});
	LogProgress(HTTPLOG::NONE);
}

struct SSkinScanUser
{
	CSkins *m_pThis;
	CSkins::TSkinLoadedCBFunc m_SkinLoadedFunc;
};

int CSkins::SkinScan(const char *pName, int IsDir, int DirType, void *pUser)
{
	auto *pUserReal = (SSkinScanUser *)pUser;
	CSkins *pSelf = pUserReal->m_pThis;

	if(IsDir || !str_endswith(pName, ".png"))
		return 0;

	char aNameWithoutPng[128];
	str_copy(aNameWithoutPng, pName);
	aNameWithoutPng[str_length(aNameWithoutPng) - 4] = 0;

	if(g_Config.m_ClVanillaSkinsOnly && !IsVanillaSkin(aNameWithoutPng))
		return 0;

	// Don't add duplicate skins (one from user's config directory, other from
	// client itself)
	for(int i = 0; i < pSelf->Num(); i++)
	{
		const char *pExName = pSelf->Get(i)->m_aName;
		if(str_comp(pExName, aNameWithoutPng) == 0)
			return 0;
	}

	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(aBuf, sizeof(aBuf), "skins/%s", pName);
	auto SkinID = pSelf->LoadSkin(aNameWithoutPng, aBuf, DirType);
	pUserReal->m_SkinLoadedFunc(SkinID);
	return SkinID;
}

static void CheckMetrics(CSkin::SSkinMetricVariable &Metrics, uint8_t *pImg, int ImgWidth, int ImgX, int ImgY, int CheckWidth, int CheckHeight)
{
	int MaxY = -1;
	int MinY = CheckHeight + 1;
	int MaxX = -1;
	int MinX = CheckWidth + 1;

	for(int y = 0; y < CheckHeight; y++)
	{
		for(int x = 0; x < CheckWidth; x++)
		{
			int OffsetAlpha = (y + ImgY) * ImgWidth + (x + ImgX) * 4 + 3;
			uint8_t AlphaValue = pImg[OffsetAlpha];
			if(AlphaValue > 0)
			{
				if(MaxY < y)
					MaxY = y;
				if(MinY > y)
					MinY = y;
				if(MaxX < x)
					MaxX = x;
				if(MinX > x)
					MinX = x;
			}
		}
	}

	Metrics.m_Width = clamp((MaxX - MinX) + 1, 1, CheckWidth);
	Metrics.m_Height = clamp((MaxY - MinY) + 1, 1, CheckHeight);
	Metrics.m_OffsetX = clamp(MinX, 0, CheckWidth - 1);
	Metrics.m_OffsetY = clamp(MinY, 0, CheckHeight - 1);
	Metrics.m_MaxWidth = CheckWidth;
	Metrics.m_MaxHeight = CheckHeight;
}

int CSkins::LoadSkin(const char *pName, const char *pPath, int DirType)
{
	CImageInfo Info;
	if(!LoadSkinPNG(Info, pName, pPath, DirType))
		return 0;
	return LoadSkin(pName, Info);
}

bool CSkins::LoadSkinPNG(CImageInfo &Info, const char *pName, const char *pPath, int DirType)
{
	char aBuf[512];
	if(!Graphics()->LoadPNG(&Info, pPath, DirType))
	{
		str_format(aBuf, sizeof(aBuf), "failed to load skin from %s", pName);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
		return false;
	}
	return true;
}

int CSkins::LoadSkin(const char *pName, CImageInfo &Info)
{
	char aBuf[512];

	if(!Graphics()->CheckImageDivisibility(pName, Info, g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridy, true))
	{
		str_format(aBuf, sizeof(aBuf), "skin failed image divisibility: %s", pName);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
		return 0;
	}
	if(!Graphics()->IsImageFormatRGBA(pName, Info))
	{
		str_format(aBuf, sizeof(aBuf), "skin format is not RGBA: %s", pName);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
		return 0;
	}

	CSkin Skin;
	Skin.m_OriginalSkin.m_Body = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	Skin.m_OriginalSkin.m_BodyOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	Skin.m_OriginalSkin.m_Feet = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	Skin.m_OriginalSkin.m_FeetOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	Skin.m_OriginalSkin.m_Hands = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	Skin.m_OriginalSkin.m_HandsOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);

	for(int i = 0; i < 6; ++i)
		Skin.m_OriginalSkin.m_aEyes[i] = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);

	int FeetGridPixelsWidth = (Info.m_Width / g_pData->m_aSprites[SPRITE_TEE_FOOT].m_pSet->m_Gridx);
	int FeetGridPixelsHeight = (Info.m_Height / g_pData->m_aSprites[SPRITE_TEE_FOOT].m_pSet->m_Gridy);
	int FeetWidth = g_pData->m_aSprites[SPRITE_TEE_FOOT].m_W * FeetGridPixelsWidth;
	int FeetHeight = g_pData->m_aSprites[SPRITE_TEE_FOOT].m_H * FeetGridPixelsHeight;

	int FeetOffsetX = g_pData->m_aSprites[SPRITE_TEE_FOOT].m_X * FeetGridPixelsWidth;
	int FeetOffsetY = g_pData->m_aSprites[SPRITE_TEE_FOOT].m_Y * FeetGridPixelsHeight;

	int FeetOutlineGridPixelsWidth = (Info.m_Width / g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_pSet->m_Gridx);
	int FeetOutlineGridPixelsHeight = (Info.m_Height / g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_pSet->m_Gridy);
	int FeetOutlineWidth = g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_W * FeetOutlineGridPixelsWidth;
	int FeetOutlineHeight = g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_H * FeetOutlineGridPixelsHeight;

	int FeetOutlineOffsetX = g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_X * FeetOutlineGridPixelsWidth;
	int FeetOutlineOffsetY = g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE].m_Y * FeetOutlineGridPixelsHeight;

	int BodyOutlineGridPixelsWidth = (Info.m_Width / g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_pSet->m_Gridx);
	int BodyOutlineGridPixelsHeight = (Info.m_Height / g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_pSet->m_Gridy);
	int BodyOutlineWidth = g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_W * BodyOutlineGridPixelsWidth;
	int BodyOutlineHeight = g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_H * BodyOutlineGridPixelsHeight;

	int BodyOutlineOffsetX = g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_X * BodyOutlineGridPixelsWidth;
	int BodyOutlineOffsetY = g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE].m_Y * BodyOutlineGridPixelsHeight;

	int BodyWidth = g_pData->m_aSprites[SPRITE_TEE_BODY].m_W * (Info.m_Width / g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridx); // body width
	int BodyHeight = g_pData->m_aSprites[SPRITE_TEE_BODY].m_H * (Info.m_Height / g_pData->m_aSprites[SPRITE_TEE_BODY].m_pSet->m_Gridy); // body height
	if(BodyWidth > Info.m_Width || BodyHeight > Info.m_Height)
		return 0;
	unsigned char *pData = (unsigned char *)Info.m_pData;
	const int PixelStep = 4;
	int Pitch = Info.m_Width * PixelStep;

	// dig out blood color
	{
		int aColors[3] = {0};
		for(int y = 0; y < BodyHeight; y++)
			for(int x = 0; x < BodyWidth; x++)
			{
				uint8_t AlphaValue = pData[y * Pitch + x * PixelStep + 3];
				if(AlphaValue > 128)
				{
					aColors[0] += pData[y * Pitch + x * PixelStep + 0];
					aColors[1] += pData[y * Pitch + x * PixelStep + 1];
					aColors[2] += pData[y * Pitch + x * PixelStep + 2];
				}
			}
		if(aColors[0] != 0 && aColors[1] != 0 && aColors[2] != 0)
			Skin.m_BloodColor = ColorRGBA(normalize(vec3(aColors[0], aColors[1], aColors[2])));
		else
			Skin.m_BloodColor = ColorRGBA(0, 0, 0, 1);
	}

	CheckMetrics(Skin.m_Metrics.m_Body, pData, Pitch, 0, 0, BodyWidth, BodyHeight);

	// body outline metrics
	CheckMetrics(Skin.m_Metrics.m_Body, pData, Pitch, BodyOutlineOffsetX, BodyOutlineOffsetY, BodyOutlineWidth, BodyOutlineHeight);

	// get feet size
	CheckMetrics(Skin.m_Metrics.m_Feet, pData, Pitch, FeetOffsetX, FeetOffsetY, FeetWidth, FeetHeight);

	// get feet outline size
	CheckMetrics(Skin.m_Metrics.m_Feet, pData, Pitch, FeetOutlineOffsetX, FeetOutlineOffsetY, FeetOutlineWidth, FeetOutlineHeight);

	// make the texture gray scale
	for(int i = 0; i < Info.m_Width * Info.m_Height; i++)
	{
		int v = (pData[i * PixelStep] + pData[i * PixelStep + 1] + pData[i * PixelStep + 2]) / 3;
		pData[i * PixelStep] = v;
		pData[i * PixelStep + 1] = v;
		pData[i * PixelStep + 2] = v;
	}

	int aFreq[256] = {0};
	int OrgWeight = 0;
	int NewWeight = 192;

	// find most common frequence
	for(int y = 0; y < BodyHeight; y++)
		for(int x = 0; x < BodyWidth; x++)
		{
			if(pData[y * Pitch + x * PixelStep + 3] > 128)
				aFreq[pData[y * Pitch + x * PixelStep]]++;
		}

	for(int i = 1; i < 256; i++)
	{
		if(aFreq[OrgWeight] < aFreq[i])
			OrgWeight = i;
	}

	// reorder
	int InvOrgWeight = 255 - OrgWeight;
	int InvNewWeight = 255 - NewWeight;
	for(int y = 0; y < BodyHeight; y++)
		for(int x = 0; x < BodyWidth; x++)
		{
			int v = pData[y * Pitch + x * PixelStep];
			if(v <= OrgWeight && OrgWeight == 0)
				v = 0;
			else if(v <= OrgWeight)
				v = (int)(((v / (float)OrgWeight) * NewWeight));
			else if(InvOrgWeight == 0)
				v = NewWeight;
			else
				v = (int)(((v - OrgWeight) / (float)InvOrgWeight) * InvNewWeight + NewWeight);
			pData[y * Pitch + x * PixelStep] = v;
			pData[y * Pitch + x * PixelStep + 1] = v;
			pData[y * Pitch + x * PixelStep + 2] = v;
		}

	Skin.m_ColorableSkin.m_Body = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_BODY]);
	Skin.m_ColorableSkin.m_BodyOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_BODY_OUTLINE]);
	Skin.m_ColorableSkin.m_Feet = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_FOOT]);
	Skin.m_ColorableSkin.m_FeetOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_FOOT_OUTLINE]);
	Skin.m_ColorableSkin.m_Hands = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_HAND]);
	Skin.m_ColorableSkin.m_HandsOutline = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_HAND_OUTLINE]);

	for(int i = 0; i < 6; ++i)
		Skin.m_ColorableSkin.m_aEyes[i] = Graphics()->LoadSpriteTexture(Info, &g_pData->m_aSprites[SPRITE_TEE_EYE_NORMAL + i]);

	Graphics()->FreePNG(&Info);

	// set skin data
	str_copy(Skin.m_aName, pName);
	if(g_Config.m_Debug)
	{
		str_format(aBuf, sizeof(aBuf), "load skin %s", Skin.m_aName);
		Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "game", aBuf);
	}

	m_vSkins.insert(std::lower_bound(m_vSkins.begin(), m_vSkins.end(), Skin), Skin);

	return 0;
}

void CSkins::OnInit()
{
	m_aEventSkinPrefix[0] = '\0';

	if(g_Config.m_Events)
	{
		time_t RawTime;
		struct tm *pTimeInfo;
		std::time(&RawTime);
		pTimeInfo = localtime(&RawTime);
		if(pTimeInfo->tm_mon == 11 && pTimeInfo->tm_mday >= 24 && pTimeInfo->tm_mday <= 26)
		{ // Christmas
			str_copy(m_aEventSkinPrefix, "santa");
		}
	}

	// load skins;
	Refresh([this](int SkinID) {
		GameClient()->m_Menus.RenderLoading(Localize("Loading DDNet Client"), Localize("Loading skin files"), 0);
	});
}

void CSkins::Refresh(TSkinLoadedCBFunc &&SkinLoadedFunc)
{
	for(auto &Skin : m_vSkins)
	{
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_Body);
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_BodyOutline);
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_Feet);
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_FeetOutline);
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_Hands);
		Graphics()->UnloadTexture(&Skin.m_OriginalSkin.m_HandsOutline);
		for(auto &Eye : Skin.m_OriginalSkin.m_aEyes)
			Graphics()->UnloadTexture(&Eye);

		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_Body);
		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_BodyOutline);
		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_Feet);
		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_FeetOutline);
		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_Hands);
		Graphics()->UnloadTexture(&Skin.m_ColorableSkin.m_HandsOutline);
		for(auto &Eye : Skin.m_ColorableSkin.m_aEyes)
			Graphics()->UnloadTexture(&Eye);
	}

	m_vSkins.clear();
	m_vDownloadSkins.clear();
	SSkinScanUser SkinScanUser;
	SkinScanUser.m_pThis = this;
	SkinScanUser.m_SkinLoadedFunc = SkinLoadedFunc;
	Storage()->ListDirectory(IStorage::TYPE_ALL, "skins", SkinScan, &SkinScanUser);
	if(m_vSkins.empty())
	{
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "gameclient", "failed to load skins. folder='skins/'");
		CSkin DummySkin;
		str_copy(DummySkin.m_aName, "dummy");
		DummySkin.m_BloodColor = ColorRGBA(1.0f, 1.0f, 1.0f);
		m_vSkins.push_back(DummySkin);
	}
}

int CSkins::Num()
{
	return m_vSkins.size();
}

const CSkin *CSkins::Get(int Index)
{
	if(Index < 0)
	{
		Index = Find("default");

		if(Index < 0)
			Index = 0;
	}
	return &m_vSkins[Index % m_vSkins.size()];
}

int CSkins::Find(const char *pName)
{
	const char *pSkinPrefix = m_aEventSkinPrefix[0] ? m_aEventSkinPrefix : g_Config.m_ClSkinPrefix;
	if(g_Config.m_ClVanillaSkinsOnly && !IsVanillaSkin(pName))
	{
		return -1;
	}
	else if(pSkinPrefix && pSkinPrefix[0])
	{
		char aBuf[24];
		str_format(aBuf, sizeof(aBuf), "%s_%s", pSkinPrefix, pName);
		// If we find something, use it, otherwise fall back to normal skins.
		int Result = FindImpl(aBuf);
		if(Result != -1)
		{
			return Result;
		}
	}
	return FindImpl(pName);
}

int CSkins::FindImpl(const char *pName)
{
	CSkin Needle;
	mem_zero(&Needle, sizeof(Needle));
	str_copy(Needle.m_aName, pName);
	auto Range = std::equal_range(m_vSkins.begin(), m_vSkins.end(), Needle);
	if(std::distance(Range.first, Range.second) == 1)
		return Range.first - m_vSkins.begin();

	if(str_comp(pName, "default") == 0)
		return -1;

	if(!g_Config.m_ClDownloadSkins)
		return -1;

	if(str_find(pName, "/") != 0)
		return -1;

	CDownloadSkin DownloadNeedle;
	mem_zero(&DownloadNeedle, sizeof(DownloadNeedle));
	str_copy(DownloadNeedle.m_aName, pName);
	const auto &[RangeBegin, RangeEnd] = std::equal_range(m_vDownloadSkins.begin(), m_vDownloadSkins.end(), DownloadNeedle);
	if(std::distance(RangeBegin, RangeEnd) == 1)
	{
		if(RangeBegin->m_pTask && RangeBegin->m_pTask->State() == HTTP_DONE)
		{
			char aPath[IO_MAX_PATH_LENGTH];
			str_format(aPath, sizeof(aPath), "downloadedskins/%s.png", RangeBegin->m_aName);
			Storage()->RenameFile(RangeBegin->m_aPath, aPath, IStorage::TYPE_SAVE);
			LoadSkin(RangeBegin->m_aName, RangeBegin->m_pTask->m_Info);
			RangeBegin->m_pTask = nullptr;
		}
		if(RangeBegin->m_pTask && (RangeBegin->m_pTask->State() == HTTP_ERROR || RangeBegin->m_pTask->State() == HTTP_ABORTED))
		{
			RangeBegin->m_pTask = nullptr;
		}
		return -1;
	}

	CDownloadSkin Skin;
	str_copy(Skin.m_aName, pName);

	char aUrl[IO_MAX_PATH_LENGTH];
	char aEscapedName[256];
	EscapeUrl(aEscapedName, sizeof(aEscapedName), pName);
	str_format(aUrl, sizeof(aUrl), "%s%s.png", g_Config.m_ClDownloadCommunitySkins != 0 ? g_Config.m_ClSkinCommunityDownloadUrl : g_Config.m_ClSkinDownloadUrl, aEscapedName);
	char aBuf[IO_MAX_PATH_LENGTH];
	str_format(Skin.m_aPath, sizeof(Skin.m_aPath), "downloadedskins/%s", IStorage::FormatTmpPath(aBuf, sizeof(aBuf), pName));
	Skin.m_pTask = std::make_shared<CGetPngFile>(this, aUrl, Storage(), Skin.m_aPath);
	m_pClient->Engine()->AddJob(Skin.m_pTask);
	m_vDownloadSkins.insert(std::lower_bound(m_vDownloadSkins.begin(), m_vDownloadSkins.end(), Skin), std::move(Skin));
	return -1;
}
