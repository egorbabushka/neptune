#include "Radar.h"

#include "../../Players/PlayerUtils.h"
#include "../../ImGui/Menu/Menu.h"

bool CRadar::GetDrawPosition(CTFPlayer* pLocal, CBaseEntity* pEntity, int& x, int& y, int& z)
{
	const float flRange = Vars::Radar::Main::Range.Value;
	const float flYaw = -DEG2RAD(I::EngineClient->GetViewAngles().y);
	const float flSin = sinf(flYaw), flCos = cosf(flYaw);

	Vec3 vDelta = pLocal->GetAbsOrigin() - pEntity->GetAbsOrigin();
	Vec2 vPos = { vDelta.x * flSin + vDelta.y * flCos, vDelta.x * flCos - vDelta.y * flSin };

	switch (Vars::Radar::Main::Style.Value)
	{
	case Vars::Radar::Main::StyleEnum::Circle:
	{
		const float flDist = vDelta.Length2D();
		if (flDist > flRange)
		{
			if (!Vars::Radar::Main::DrawOutOfRange.Value)
				return false;

			vPos *= flRange / flDist;
		}
		break;
	}
	case Vars::Radar::Main::StyleEnum::Rectangle:
		if (fabs(vPos.x) > flRange || fabs(vPos.y) > flRange)
		{
			if (!Vars::Radar::Main::DrawOutOfRange.Value)
				return false;

			Vec2 a = { -flRange / vPos.x, -flRange / vPos.y };
			Vec2 b = { flRange / vPos.x, flRange / vPos.y };
			Vec2 c = { std::min(a.x, b.x), std::min(a.y, b.y) };
			vPos *= fabsf(std::max(c.x, c.y));
		}
	}

	const WindowBox_t& info = Vars::Radar::Main::Window.Value;
	const int titleHeight = H::Draw.Scale(40);
	const int padding = H::Draw.Scale(12);
	const int radarAreaHeight = info.w - titleHeight - padding;
	const int radarAreaWidth = info.w - (padding * 2);

	// Calculate position within the radar area (accounting for title and padding)
	// Add extra padding to keep icons fully within bounds
	const int iconPadding = H::Draw.Scale(8);
	x = info.x + padding + std::clamp(vPos.x / flRange * radarAreaWidth / 2 + float(radarAreaWidth) / 2,
		float(iconPadding), float(radarAreaWidth - iconPadding));
	y = info.y + titleHeight + std::clamp(vPos.y / flRange * radarAreaHeight / 2 + float(radarAreaHeight) / 2,
		float(iconPadding), float(radarAreaHeight - iconPadding));

	return true;
}

void CRadar::DrawBackground()
{
	const WindowBox_t& info = Vars::Radar::Main::Window.Value;

	Color_t backgroundColor = Vars::Menu::Theme::Background.Value;
	backgroundColor = backgroundColor.Lerp({ 127, 127, 127, backgroundColor.a }, 1.f / 9);
	backgroundColor.a = byte(Vars::Radar::Main::BackgroundAlpha.Value);
	Color_t accentColor = Vars::Menu::Theme::Accent.Value;
	Color_t activeColor = Vars::Menu::Theme::Active.Value;

	const int cornerRadius = H::Draw.Scale(3);
	H::Draw.FillRoundRect(info.x, info.y, info.w, info.w, cornerRadius, backgroundColor);
	const float headerHeight = H::Draw.Scale(24);
	Color_t headerBgColor = backgroundColor;
	headerBgColor = { 
		static_cast<byte>(backgroundColor.r * 0.9f), 
		static_cast<byte>(backgroundColor.g * 0.9f), 
		static_cast<byte>(backgroundColor.b * 0.9f), 
		backgroundColor.a 
	};
	
	H::Draw.FillRoundRect(info.x, info.y, info.w, headerHeight, cornerRadius, headerBgColor);

	const auto& indicatorFont = H::Fonts.GetFont(FONT_INDICATORS);
	H::Draw.String(indicatorFont, info.x + H::Draw.Scale(16), info.y + H::Draw.Scale(5), activeColor, ALIGN_TOPLEFT, "Ra");
	int radarWidth = 0, radarHeight = 0;
	I::MatSystemSurface->GetTextSize(indicatorFont.m_dwFont, L"Ra", radarWidth, radarHeight);
	H::Draw.String(indicatorFont, info.x + H::Draw.Scale(16) + radarWidth, info.y + H::Draw.Scale(5), accentColor, ALIGN_TOPLEFT, "dar");

	const int titleHeight = H::Draw.Scale(40);
	const int padding = H::Draw.Scale(12);
	const int radarAreaHeight = info.w - titleHeight - padding;
	const int radarAreaWidth = info.w - (padding * 2);
	const int centerX = info.x + info.w / 2;
	const int centerY = info.y + titleHeight + (radarAreaHeight / 2);
	const int lineLength = std::min(radarAreaWidth, radarAreaHeight) / 2 - H::Draw.Scale(4);

	H::Draw.Line(centerX - lineLength, centerY, centerX + lineLength, centerY, accentColor);
	H::Draw.Line(centerX, centerY - lineLength, centerX, centerY + lineLength, accentColor);

	if (Vars::Radar::Main::Style.Value == Vars::Radar::Main::StyleEnum::Rectangle)
	{
		H::Draw.LineRect(
			info.x + padding,
			info.y + titleHeight,
			radarAreaWidth,
			radarAreaHeight,
			accentColor
		);
	}
	else if (Vars::Radar::Main::Style.Value == Vars::Radar::Main::StyleEnum::Circle)
	{
		const float flRadius = std::min(radarAreaWidth, radarAreaHeight) / 2.0f - H::Draw.Scale(2);
		H::Draw.LineCircle(centerX, centerY, flRadius, 100, accentColor);
	}
}

void CRadar::DrawPoints(CTFPlayer* pLocal)
{
	if (Vars::Radar::World::Enabled.Value)
	{
		const int iSize = Vars::Radar::World::Size.Value;

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Gargoyle)
		{
			for (auto pGargy : H::Entities.GetGroup(EGroupType::WORLD_GARGOYLE))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pGargy, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Halloween.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 39);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Spellbook)
		{
			for (auto pBook : H::Entities.GetGroup(EGroupType::PICKUPS_SPELLBOOK))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pBook, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Halloween.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 38);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Powerup)
		{
			for (auto pPower : H::Entities.GetGroup(EGroupType::PICKUPS_POWERUP))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pPower, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Powerup.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 37);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Bombs)
		{
			for (auto bBomb : H::Entities.GetGroup(EGroupType::WORLD_BOMBS))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, bBomb, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Halloween.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 36);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Money)
		{
			for (auto pBook : H::Entities.GetGroup(EGroupType::PICKUPS_MONEY))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pBook, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Money.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 35);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Ammo)
		{
			for (auto pAmmo : H::Entities.GetGroup(EGroupType::PICKUPS_AMMO))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pAmmo, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Ammo.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 34);
				}
			}
		}

		if (Vars::Radar::World::Draw.Value & Vars::Radar::World::DrawEnum::Health)
		{
			for (auto pHealth : H::Entities.GetGroup(EGroupType::PICKUPS_HEALTH))
			{
				int x, y, z;
				if (GetDrawPosition(pLocal, pHealth, x, y, z))
				{
					if (Vars::Radar::World::Background.Value)
					{
						const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
						H::Draw.FillCircle(x, y, flRadius, 20, Vars::Colors::Health.Value);
					}

					H::Draw.Texture(x, y, iSize, iSize, 33);
				}
			}
		}
	}

	if (Vars::Radar::Building::Enabled.Value)
	{
		const int iSize = Vars::Radar::Building::Size.Value;

		for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_ALL))
		{
			auto pBuilding = pEntity->As<CBaseObject>();

			if (!pBuilding->m_bWasMapPlaced())
			{
				auto pOwner = pBuilding->m_hBuilder().Get();
				if (pOwner)
				{
					const int nIndex = pOwner->entindex();
					if (pLocal->m_iObserverMode() == OBS_MODE_FIRSTPERSON ? pLocal->m_hObserverTarget().Get() == pOwner : nIndex == I::EngineClient->GetLocalPlayer())
					{
						if (!(Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Local))
							continue;
					}
					else
					{
						if (!(Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Prioritized && F::PlayerUtils.IsPrioritized(nIndex))
							&& !(Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Friends && H::Entities.IsFriend(nIndex))
							&& !(Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Party && H::Entities.InParty(nIndex))
							&& !(pOwner->As<CTFPlayer>()->m_iTeamNum() != pLocal->m_iTeamNum() ? Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Enemy : Vars::Radar::Building::Draw.Value & Vars::Radar::Building::DrawEnum::Team))
							continue;
					}
				}
			}

			int x, y, z;
			if (GetDrawPosition(pLocal, pBuilding, x, y, z))
			{
				const Color_t tColor = H::Color.GetEntityDrawColor(pLocal, pBuilding, Vars::Colors::Relative.Value);

				int iBounds = iSize;
				if (Vars::Radar::Building::Background.Value)
				{
					const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
					H::Draw.FillCircle(x, y, flRadius, 20, tColor);
					iBounds = flRadius * 2;
				}

				switch (pBuilding->GetClassID())
				{
				case ETFClassID::CObjectSentrygun:
					H::Draw.Texture(x, y, iSize, iSize, 26 + pBuilding->m_iUpgradeLevel());
					break;
				case ETFClassID::CObjectDispenser:
					H::Draw.Texture(x, y, iSize, iSize, 30);
					break;
				case ETFClassID::CObjectTeleporter:
					H::Draw.Texture(x, y, iSize, iSize, pBuilding->m_iObjectMode() ? 32 : 31);
					break;
				}

				if (Vars::Radar::Building::Health.Value)
				{
					const int iMaxHealth = pBuilding->m_iMaxHealth(), iHealth = pBuilding->m_iHealth();

					float flRatio = std::clamp(float(iHealth) / iMaxHealth, 0.f, 1.f);
					Color_t cColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, flRatio);
					H::Draw.FillRectPercent(x - iBounds / 2, y - iBounds / 2, 2, iBounds, flRatio, cColor, { 0, 0, 0, 255 }, ALIGN_BOTTOM, true);
				}
			}
		}
	}

	if (Vars::Radar::Player::Enabled.Value)
	{
		const int iSize = Vars::Radar::Player::Size.Value;

		for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
		{
			auto pPlayer = pEntity->As<CTFPlayer>();
			if (pPlayer->IsDormant() && !H::Entities.GetDormancy(pPlayer->entindex()) || !pPlayer->IsAlive() || pPlayer->IsAGhost())
				continue;

			const int nIndex = pPlayer->entindex();
			if (pLocal->m_iObserverMode() == OBS_MODE_FIRSTPERSON ? pLocal->m_hObserverTarget().Get() == pPlayer : nIndex == I::EngineClient->GetLocalPlayer())
			{
				if (!(Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Local))
					continue;
			}
			else
			{
				if (!(Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Prioritized && F::PlayerUtils.IsPrioritized(nIndex))
					&& !(Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Friends && H::Entities.IsFriend(nIndex))
					&& !(Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Party && H::Entities.InParty(nIndex))
					&& !(pPlayer->m_iTeamNum() != pLocal->m_iTeamNum() ? Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Enemy : Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Team))
					continue;
			}
			if (!(Vars::Radar::Player::Draw.Value & Vars::Radar::Player::DrawEnum::Cloaked) && pPlayer->m_flInvisibility() >= 1.f)
				continue;

			int x, y, z;
			if (GetDrawPosition(pLocal, pPlayer, x, y, z))
			{
				const Color_t tColor = H::Color.GetEntityDrawColor(pLocal, pPlayer, Vars::Colors::Relative.Value);

				int iBounds = iSize;
				if (Vars::Radar::Player::Background.Value)
				{
					const float flRadius = sqrtf(pow(iSize, 2) * 2) / 2;
					H::Draw.FillCircle(x, y, flRadius, 20, tColor);
					iBounds = flRadius * 2;
				}

				switch (Vars::Radar::Player::Icon.Value)
				{
				case Vars::Radar::Player::IconEnum::Avatars:
				{
					PlayerInfo_t pi{};
					if (I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &pi) && !pi.fakeplayer)
					{
						int iType = 0; F::PlayerUtils.GetPlayerName(pPlayer->entindex(), nullptr, &iType);
						if (iType != 1)
						{
							H::Draw.Avatar(x, y, iSize, iSize, pi.friendsID);
							break;
						}
					}
					[[fallthrough]];
				}
				case Vars::Radar::Player::IconEnum::Portraits:
					if (int iTeam = pPlayer->IsInValidTeam())
					{
						H::Draw.Texture(x, y, iSize, iSize, pPlayer->m_iClass() + (iTeam == TF_TEAM_RED ? 9 : 18) - 1);
						break;
					}
					[[fallthrough]];
				case Vars::Radar::Player::IconEnum::Icons:
					H::Draw.Texture(x, y, iSize, iSize, pPlayer->m_iClass() - 1);
					break;
				}

				if (Vars::Radar::Player::Health.Value)
				{
					const int iMaxHealth = pPlayer->GetMaxHealth(), iHealth = pPlayer->m_iHealth();

					float flRatio = std::clamp(float(iHealth) / iMaxHealth, 0.f, 1.f);
					Color_t cColor = Vars::Colors::IndicatorBad.Value.Lerp(Vars::Colors::IndicatorGood.Value, flRatio);
					H::Draw.FillRectPercent(x - iBounds / 2, y - iBounds / 2, 2, iBounds, flRatio, cColor, { 0, 0, 0, 255 }, ALIGN_BOTTOM, true);

					if (iHealth > iMaxHealth)
					{
						const float flMaxOverheal = floorf(iMaxHealth / 10.f) * 5;
						flRatio = std::clamp((iHealth - iMaxHealth) / flMaxOverheal, 0.f, 1.f);
						cColor = Vars::Colors::IndicatorMisc.Value;
						H::Draw.FillRectPercent(x - iBounds / 2, y - iBounds / 2, 2, iBounds, flRatio, cColor, { 0, 0, 0, 0 }, ALIGN_BOTTOM, true);
					}
				}

				if (Vars::Radar::Player::Height.Value && std::abs(z) > 80.f)
				{
					const int m = x - iSize / 2;
					const int iOffset = z < 0 ? -5 : 5;
					const int yPos = z < 0 ? y - iBounds / 2 - 2 : y + iBounds / 2 + 2;

					H::Draw.FillPolygon({ Vec2(m, yPos), Vec2(m + iSize * 0.5f, yPos + iOffset), Vec2(m + iSize, yPos) }, tColor);
				}
			}
		}
	}
}

void CRadar::Run(CTFPlayer* pLocal)
{
	if (!Vars::Radar::Main::Enabled.Value || I::MatSystemSurface->IsCursorVisible() && !I::EngineClient->IsPlayingDemo() && !F::Menu.m_bIsOpen)
		return;

	DrawBackground();
	DrawPoints(pLocal);
}