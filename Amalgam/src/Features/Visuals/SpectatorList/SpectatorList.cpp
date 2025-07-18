#include "SpectatorList.h"

#include "../../Players/PlayerUtils.h"
#include "../../Spectate/Spectate.h"

static float s_flCurrentHeight = 0.0f;

bool CSpectatorList::GetSpectators(CTFPlayer* pTarget)
{
	m_vSpectators.clear();

	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		int iIndex = pPlayer->entindex();
		bool bLocal = pEntity->entindex() == I::EngineClient->GetLocalPlayer();

		auto pObserverTarget = pPlayer->m_hObserverTarget().Get();
		int iObserverMode = pPlayer->m_iObserverMode();
		if (bLocal && F::Spectate.m_iTarget != -1)
		{
			pObserverTarget = F::Spectate.m_pOriginalTarget;
			iObserverMode = F::Spectate.m_iOriginalMode;
		}

		if (pPlayer->IsAlive() || pObserverTarget != pTarget
			|| bLocal && !I::EngineClient->IsPlayingDemo() && F::Spectate.m_iTarget == -1)
		{
			pObserverTarget = F::Spectate.m_pOriginalTarget;
			iObserverMode = F::Spectate.m_iOriginalMode;
		}

		if (pPlayer->IsAlive() || pObserverTarget != pTarget
			|| bLocal && !I::EngineClient->IsPlayingDemo() && F::Spectate.m_iTarget == -1)
		{
			if (m_mRespawnCache.contains(iIndex))
				m_mRespawnCache.erase(iIndex);
			continue;
		}

		std::string sMode;
		switch (iObserverMode)
		{
		case OBS_MODE_FIRSTPERSON: sMode = "1st"; break;
		case OBS_MODE_THIRDPERSON: sMode = "3rd"; break;
		default: continue;
		}

		float flRespawnTime = 0.f, flRespawnIn = 0.f;
		bool bRespawnTimeIncreased = false; // theoretically the respawn times could be changed by the map but oh well
		if (auto pResource = H::Entities.GetPR())
		{
			flRespawnTime = pResource->m_flNextRespawnTime(iIndex);
			flRespawnIn = std::max(floorf(flRespawnTime - TICKS_TO_TIME(I::ClientState->m_ClockDriftMgr.m_nServerTick)), 0.f);
		}
		if (!m_mRespawnCache.contains(iIndex))
			m_mRespawnCache[iIndex] = flRespawnTime;
		else if (m_mRespawnCache[iIndex] + 0.5f < flRespawnTime)
			bRespawnTimeIncreased = true;

		PlayerInfo_t pi{};
		if (I::EngineClient->GetPlayerInfo(iIndex, &pi))
		{
			std::string sName = F::PlayerUtils.GetPlayerName(iIndex, pi.name);
			m_vSpectators.emplace_back(sName, sMode, flRespawnIn, bRespawnTimeIncreased, iIndex);
		}
	}

	return !m_vSpectators.empty();
}

void CSpectatorList::Draw(CTFPlayer* pLocal)
{
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::Spectators))
	{
		m_mRespawnCache.clear();
		s_flCurrentHeight = 0.0f;  // Reset height when disabled
		return;
	}

	auto pTarget = pLocal;
	switch (pLocal->m_iObserverMode())
	{
	case OBS_MODE_FIRSTPERSON:
	case OBS_MODE_THIRDPERSON:
		pTarget = pLocal->m_hObserverTarget()->As<CTFPlayer>();
	}
	PlayerInfo_t pi{};
	if (!pTarget || pTarget != pLocal && !I::EngineClient->GetPlayerInfo(pTarget->entindex(), &pi))
		return;

	if (!GetSpectators(pTarget))
		return;

	int x = Vars::Menu::SpectatorsDisplay.Value.x;
	int y = Vars::Menu::SpectatorsDisplay.Value.y;
	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);
	const int nTall = fFont.m_nTall + H::Draw.Scale(3);

	int maxTextWidth = 0;
	for (auto& Spectator : m_vSpectators)
	{
		int w = 0, h = 0;
		std::string text = std::format("{} - {} ({}s)", Spectator.m_sName, Spectator.m_sMode, static_cast<int>(Spectator.m_flRespawnIn));
		I::MatSystemSurface->GetTextSize(fFont.m_dwFont, SDK::ConvertUtf8ToWide(text).c_str(), w, h);
		maxTextWidth = std::max(maxTextWidth, w);
	}

	int totalHeight = H::Draw.Scale(48);
	totalHeight += static_cast<int>(m_vSpectators.size()) * nTall;
	totalHeight += H::Draw.Scale(4); 

	s_flCurrentHeight = std::lerp(s_flCurrentHeight, static_cast<float>(totalHeight), I::GlobalVars->frametime * 10.0f);
	totalHeight = static_cast<int>(std::round(s_flCurrentHeight));

	const int boxWidth = std::max(H::Draw.Scale(220), maxTextWidth + H::Draw.Scale(40)); 
	const int cornerRadius = H::Draw.Scale(2); 
	
	Color_t tBackgroundColor = Vars::Menu::Theme::Background.Value;
	tBackgroundColor = tBackgroundColor.Lerp({ 127, 127, 127, tBackgroundColor.a }, 1.f / 9);
	tBackgroundColor.a = 255; // Make fully opaque
	
	Color_t tAccentColor = Vars::Menu::Theme::Accent.Value;
	Color_t tActiveColor = Vars::Menu::Theme::Active.Value;

	H::Draw.FillRoundRect(x, y, boxWidth, totalHeight, cornerRadius, tBackgroundColor);

	const float headerHeight = H::Draw.Scale(24);
	Color_t tHeaderBgColor = tBackgroundColor;
	tHeaderBgColor = { 
		static_cast<byte>(tBackgroundColor.r * 0.9f), 
		static_cast<byte>(tBackgroundColor.g * 0.9f), 
		static_cast<byte>(tBackgroundColor.b * 0.9f), 
		tBackgroundColor.a 
	};
	
	H::Draw.FillRoundRect(x, y, boxWidth, headerHeight, cornerRadius, tHeaderBgColor);
	H::Draw.String(fFont, x + H::Draw.Scale(16), y + H::Draw.Scale(5), tActiveColor, ALIGN_TOPLEFT, "Spec");
	int specWidth = 0, specHeight = 0;
	I::MatSystemSurface->GetTextSize(fFont.m_dwFont, L"Spec", specWidth, specHeight);
	H::Draw.String(fFont, x + H::Draw.Scale(16) + specWidth, y + H::Draw.Scale(5), tAccentColor, ALIGN_TOPLEFT, "tators");

	y += H::Draw.Scale(32);
	for (auto& Spectator : m_vSpectators)
	{
		Color_t tColor = tActiveColor;
		if (Spectator.m_bIsFriend)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(FRIEND_TAG)].m_tColor;
		else if (Spectator.m_bInParty)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(PARTY_TAG)].m_tColor;
		else if (Spectator.m_bRespawnTimeIncreased)
			tColor = F::PlayerUtils.m_vTags[F::PlayerUtils.TagToIndex(CHEATER_TAG)].m_tColor;
		else if (FNV1A::Hash32(Spectator.m_sMode.c_str()) == FNV1A::Hash32Const("1st"))
			tColor = tColor.Lerp({ 255, 150, 0, 255 }, 0.5f);

		if (Spectator.m_bRespawnTimeIncreased || FNV1A::Hash32(Spectator.m_sMode.c_str()) == FNV1A::Hash32Const("1st"))
		{
			Color_t tHighlightColor = tBackgroundColor;
			tHighlightColor = tHighlightColor.Lerp({ 255, 255, 255, tBackgroundColor.a }, 0.05f);
			H::Draw.FillRoundRect(x + H::Draw.Scale(12), y - H::Draw.Scale(2), boxWidth - H::Draw.Scale(24), nTall, H::Draw.Scale(2), tHighlightColor);
		}

		H::Draw.String(fFont, x + H::Draw.Scale(16), y, tColor, ALIGN_TOPLEFT, std::format("{} - {} ({}s)", Spectator.m_sName.c_str(), Spectator.m_sMode.c_str(), static_cast<int>(Spectator.m_flRespawnIn)).c_str());
		y += nTall;
	}
}