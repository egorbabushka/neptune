#include "AimbotHitscan.h"

#include "../Aimbot.h"
#include "../AimbotSafety.h"
#include "../TextmodeConfig.h"
#include "../../Resolver/Resolver.h"
#include "../../Ticks/Ticks.h"
#include "../../Visuals/Visuals.h"
#include "../../Simulation/MovementSimulation/MovementSimulation.h"
#include "../../NavBot/NBheader.h"
#include "../../NavBot/NavEngine/FileReader/nav.h"
#include "../../../Utils/Math/SIMDMath.h"
#include <unordered_set>
#include <algorithm>

std::vector<Target_t> CAimbotHitscan::GetTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon || !I::EngineClient || !I::EngineClient->IsInGame())
		return {};
	
	std::vector<Target_t> vTargets;
	vTargets.reserve(32); // Pre-allocate to prevent frequent reallocations
	
	const auto iSort = Vars::Aimbot::General::TargetSelection.Value;

			Vec3 vLocalPos = F::Ticks.GetShootPos();
		Vec3 vLocalAngles = I::EngineClient->GetViewAngles();
		
#ifdef TEXTMODE
		bool bFlamethrowerFOVAdjust = false;
		float flOriginalFOV = Vars::Aimbot::General::AimFOV.Value;
		if (pWeapon && pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
		{
			bFlamethrowerFOVAdjust = true;
			// Increase FOV slightly for flamethrower to make targeting more forgiving
			Vars::Aimbot::General::AimFOV.Value = std::min(flOriginalFOV * 1.3f, 90.f);
		}
#endif

	bool bLowFPS = G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
	int iLowFPSFlags = bLowFPS ? Vars::Aimbot::Hitscan::LowFPSOptimizations.Value : 0;

	{
		auto eGroupType = EGroupType::GROUP_INVALID;
		if (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Players)
			eGroupType = EGroupType::PLAYERS_ENEMIES;

		bool bCanExtinguish = SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0;
		if (bCanExtinguish && Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ExtinguishTeam)
			eGroupType = EGroupType::PLAYERS_ALL;
		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			eGroupType = Vars::Aimbot::Healing::AutoHeal.Value ? EGroupType::PLAYERS_TEAMMATES : EGroupType::GROUP_INVALID;

		for (auto pEntity : H::Entities.GetGroup(eGroupType))
		{
			if (!pEntity || pEntity->IsDormant() || pEntity->entindex() <= 0)
				continue;
			
			bool bTeammate = pEntity->m_iTeamNum() == pLocal->m_iTeamNum();
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			if (bTeammate)
			{
				if (bCanExtinguish)
				{
					if (!pEntity->As<CTFPlayer>()->InCond(TF_COND_BURNING))
						continue;
				}
				else if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
				{
					if (pEntity->As<CTFPlayer>()->InCond(TF_COND_STEALTHED)
						|| Vars::Aimbot::Healing::FriendsOnly.Value && !H::Entities.IsFriend(pEntity->entindex()) && !H::Entities.InParty(pEntity->entindex()))
						continue;
				}
			}

			float flFOVTo; Vec3 vPos, vAngleTo;
			if (!F::AimbotGlobal.PlayerBoneInFOV(pEntity->As<CTFPlayer>(), vLocalPos, vLocalAngles, flFOVTo, vPos, vAngleTo, Vars::Aimbot::Hitscan::Hitboxes.Value))
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::Player, vPos, vAngleTo, flFOVTo, flDistTo, bTeammate ? 0 : F::AimbotGlobal.GetPriority(pEntity->entindex()));
		}

		if (pWeapon->GetWeaponID() == TF_WEAPON_MEDIGUN)
			return vTargets;
	}

	if (Vars::Aimbot::General::Target.Value)
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_ENEMIES))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, pEntity->IsSentrygun() ? TargetEnum::Sentry : pEntity->IsDispenser() ? TargetEnum::Dispenser : TargetEnum::Teleporter, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	// Check for other target types only if we have space (for low FPS)
	bool bHasSpace = !Vars::Aimbot::Hitscan::TargetEveryone.Value && (!bLowFPS || !(iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::TargetAll) || vTargets.size() < 2);

	if (bHasSpace && (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Stickies))
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_PROJECTILES))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->m_vecOrigin();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::Sticky, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (bHasSpace && (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::NPCs))
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
		{
			if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, pWeapon))
				continue;

			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::NPC, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

	if (bHasSpace && (Vars::Aimbot::General::Target.Value & Vars::Aimbot::General::TargetEnum::Bombs))
	{
		for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_BOMBS))
		{
			Vec3 vPos = pEntity->GetCenter();
			Vec3 vAngleTo = Math::CalcAngle(vLocalPos, vPos);
			float flFOVTo = Math::CalcFov(vLocalAngles, vAngleTo);
			if (flFOVTo > Vars::Aimbot::General::AimFOV.Value)
				continue;

			if (!F::AimbotGlobal.ValidBomb(pLocal, pWeapon, pEntity))
				continue;

			float flDistTo = iSort == Vars::Aimbot::General::TargetSelectionEnum::Distance ? vLocalPos.DistTo(vPos) : 0.f;
			vTargets.emplace_back(pEntity, TargetEnum::Bomb, vPos, vAngleTo, flFOVTo, flDistTo);
		}
	}

#ifdef TEXTMODE
	if (bFlamethrowerFOVAdjust)
	{
		Vars::Aimbot::General::AimFOV.Value = flOriginalFOV;
	}
#endif

	return vTargets;
}

std::vector<Target_t> CAimbotHitscan::SortTargets(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!pLocal || !pWeapon || !I::EngineClient || !I::EngineClient->IsInGame())
		return {};
	
	auto vTargets = GetTargets(pLocal, pWeapon);
	
	AimbotSafety::SafeResize(vTargets, TextmodeConfig::MAX_TARGETS);

	bool bLowFPS = G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
	int iLowFPSFlags = bLowFPS ? Vars::Aimbot::Hitscan::LowFPSOptimizations.Value : 0;
	
	static float flLastTargetProcessTime = 0.0f;
	
	if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceTargetProcessing))
	{
		float flCurrentTime = I::GlobalVars->curtime;
		if (flCurrentTime - flLastTargetProcessTime < 0.1f)
		{
			static std::vector<Target_t> vCachedTargets;
			return vCachedTargets;
		}
		flLastTargetProcessTime = I::GlobalVars->curtime;
	}
	
	if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::FOVBasedCulling))
	{
		float flReducedFOV = Vars::Aimbot::General::AimFOV.Value * 0.7f;
		vTargets.erase(std::remove_if(vTargets.begin(), vTargets.end(), 
			[flReducedFOV](const Target_t& target) { 
				return target.m_flFOVTo > flReducedFOV; 
			}), vTargets.end());
	}
	
	if (Vars::Aimbot::Hitscan::TargetEveryone.Value || (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::TargetAll)))
	{
		std::sort(vTargets.begin(), vTargets.end(), [](const Target_t& a, const Target_t& b) -> bool
		{
			return a.m_flDistTo < b.m_flDistTo;
		});
		
		// Save the targets for future use with ReduceTargetProcessing
		if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceTargetProcessing))
		{
			static std::vector<Target_t> vCachedTargets;
			vCachedTargets = vTargets;
		}
		
		return vTargets;
	}

	// If we're using "OnlyTargetClosest" optimization, only keep the closest target
	if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::OnlyTargetClosest) && !vTargets.empty())
	{
		std::sort(vTargets.begin(), vTargets.end(), [](const Target_t& a, const Target_t& b) -> bool
		{
			return a.m_flDistTo < b.m_flDistTo;
		});
		vTargets.resize(1);
		
		// Save the target for future use with ReduceTargetProcessing
		if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceTargetProcessing))
		{
			static std::vector<Target_t> vCachedTargets;
			vCachedTargets = vTargets;
		}
		
		return vTargets;
	}

	F::AimbotGlobal.SortTargets(vTargets, Vars::Aimbot::General::TargetSelection.Value);

	// Prioritize navbot target
	if (Vars::Aimbot::General::PrioritizeNavbot.Value && F::NavBot.m_iStayNearTargetIdx)
	{
		std::sort((vTargets).begin(), (vTargets).end(), [&](const Target_t& a, const Target_t& b) -> bool
				  {
					  return a.m_pEntity->entindex() == F::NavBot.m_iStayNearTargetIdx && b.m_pEntity->entindex() != F::NavBot.m_iStayNearTargetIdx;
				  });
	}

	vTargets.resize(std::min(size_t(Vars::Aimbot::General::MaxTargets.Value), vTargets.size()));
	if (!Vars::Aimbot::General::PrioritizeNavbot.Value || !F::NavBot.m_iStayNearTargetIdx)
		F::AimbotGlobal.SortPriority(vTargets);
	
	// Save the targets for future use with ReduceTargetProcessing
	if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceTargetProcessing))
	{
		static std::vector<Target_t> vCachedTargets;
		vCachedTargets = vTargets;
	}
	
	return vTargets;
}

// Optimized target sorting using SIMD operations
std::vector<Target_t> CAimbotHitscan::SortTargetsOptimized(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	const Vec3 vEyePos = pLocal->GetShootPos();
	const Vec3 vViewAngles = G::CurrentUserCmd->viewangles;
	
	// Check cache validity
	int currentFrame = I::GlobalVars->framecount;
	if (m_targetCache.lastFrameCount == currentFrame && 
		m_targetCache.lastEyePos.DistToSqr(vEyePos) < 1.0f)
	{
		return m_targetCache.cachedTargets; // Return cached results
	}
	
	auto vTargets = GetTargets(pLocal, pWeapon);
	if (vTargets.empty()) return vTargets;
	
	const size_t targetCount = vTargets.size();
	
	// Resize SIMD caches if needed
	m_SIMDTargetCache.resize(targetCount);
	m_SIMDPositionCache.resize(targetCount);
	m_FOVCache.resize(targetCount);
	
	// Prepare SIMD data structures
	SIMDMath::Vec3SIMD eyePosSIMD(vEyePos);
	SIMDMath::Vec3SIMD viewAnglesSIMD(vViewAngles);
	
	// Batch calculate positions and distances using SIMD
	for (size_t i = 0; i < targetCount; ++i) LIKELY
	{
		const Vec3& targetPos = vTargets[i].m_vPos;
		m_SIMDPositionCache[i] = SIMDMath::Vec3SIMD(targetPos);
		
		m_SIMDTargetCache[i].index = static_cast<int>(i);
		m_SIMDTargetCache[i].distanceSqr = eyePosSIMD.DistToSqr(m_SIMDPositionCache[i]);
	}
	
	// Batch calculate FOV values using optimized functions
	std::vector<Vec3> viewAnglesArray(targetCount, vViewAngles);
	std::vector<Vec3> targetAnglesArray(targetCount);
	
	for (size_t i = 0; i < targetCount; ++i) LIKELY
	{
		targetAnglesArray[i] = SIMDMath::CalcAngleSIMD(eyePosSIMD, m_SIMDPositionCache[i]).ToVec3();
	}
	
	SIMDMath::Optimized::CalculateFOVBatch(
		viewAnglesArray.data(), 
		targetAnglesArray.data(), 
		m_FOVCache.data(), 
		static_cast<int>(targetCount)
	);
	
	// Fast sorting using optimized algorithm
	SIMDMath::Optimized::SortTargetsByDistance(m_SIMDTargetCache.data(), static_cast<int>(targetCount));
	
	// Reconstruct sorted targets with priorities
	std::vector<Target_t> sortedTargets;
	sortedTargets.reserve(targetCount);
	
	for (size_t i = 0; i < targetCount; ++i) LIKELY
	{
		int originalIndex = m_SIMDTargetCache[i].index;
		Target_t& target = vTargets[originalIndex];
		
		// Apply FOV filtering with branch prediction
		if (m_FOVCache[originalIndex] <= Vars::Aimbot::General::AimFOV.Value) LIKELY
		{
			target.m_flFOVTo = m_FOVCache[originalIndex];
			target.m_flDistTo = sqrtf(m_SIMDTargetCache[i].distanceSqr);
			sortedTargets.push_back(target);
		}
	}
	
	// Update cache
	m_targetCache.lastEyePos = vEyePos;
	m_targetCache.lastFrameCount = currentFrame;
	m_targetCache.cachedTargets = sortedTargets;
	
	return sortedTargets;
}

// Fast visibility check for common cases
inline bool CAimbotHitscan::QuickVisibilityCheck(const Vec3& from, const Vec3& to, CTFPlayer* pLocal, CBaseEntity* pTarget) const
{
	// Simple ray-cast check for quick filtering
	CGameTrace trace = {};
	CTraceFilterHitscan filter = {}; 
	filter.pSkip = pLocal;
	
	SDK::Trace(from, to, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
	return (trace.m_pEnt && trace.m_pEnt == pTarget);
}

bool CAimbotHitscan::HasEnemiesInRange(CTFPlayer* pLocal, float flRange) const
{
	if (!pLocal || !I::EngineClient || !I::EngineClient->IsInGame())
		return false;
	
	Vec3 vLocalPos = pLocal->GetShootPos();
	float flRangeSqr = flRange * flRange;
	
	for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
	{
		if (!pEntity || pEntity->IsDormant() || pEntity->entindex() <= 0)
			continue;
		
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, nullptr))
			continue;
		
		float flDistSqr = vLocalPos.DistToSqr(pEntity->GetCenter());
		if (flDistSqr <= flRangeSqr)
			return true;
	}
	
	for (auto pEntity : H::Entities.GetGroup(EGroupType::BUILDINGS_ENEMIES))
	{
		if (!pEntity || pEntity->IsDormant() || pEntity->entindex() <= 0)
			continue;
		
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, nullptr))
			continue;
		
		float flDistSqr = vLocalPos.DistToSqr(pEntity->GetCenter());
		if (flDistSqr <= flRangeSqr)
			return true;
	}
	
	for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
	{
		if (!pEntity || pEntity->IsDormant() || pEntity->entindex() <= 0)
			continue;
		
		if (F::AimbotGlobal.ShouldIgnore(pEntity, pLocal, nullptr))
			continue;
		
		float flDistSqr = vLocalPos.DistToSqr(pEntity->GetCenter());
		if (flDistSqr <= flRangeSqr)
			return true;
	}
	
	return false;
}

bool CAimbotHitscan::NavBotNeedsJump(CTFPlayer* pLocal) const
{
	if (!pLocal || !F::NavEngine.isPathing())
		return false;
	
	auto crumbs = F::NavEngine.getCrumbs();
	if (!crumbs || crumbs->empty())
		return false;
	
	Vec3 vLocalPos = pLocal->GetAbsOrigin();
	Vec3 vTargetPos = (*crumbs)[0].vec;
	
	float flHeightDiff = vTargetPos.z - vLocalPos.z;
	
	const float flJumpThreshold = 36.0f;
	if (flHeightDiff > flJumpThreshold && flHeightDiff <= PLAYER_JUMP_HEIGHT)
		return true;
	
	if (F::NavEngine.isInactive(Vars::NavEng::NavEngine::StuckTime.Value / 2))
	{
		if (F::NavEngine.map)
		{
			auto pLocalNav = F::NavEngine.map->findClosestNavSquare(vLocalPos);
			if (pLocalNav && !(pLocalNav->m_attributeFlags & (NAV_MESH_NO_JUMP | NAV_MESH_STAIRS)))
				return true;
		}
	}
	
	return false;
}

// Batch distance calculation using SIMD
void CAimbotHitscan::BatchCalculateDistances(const Vec3& eyePos, const std::vector<Target_t>& targets, float* distances) const
{
	SIMDMath::Vec3SIMD eyePosSIMD(eyePos);
	
	// Process targets in batches for better cache locality
	const size_t targetCount = targets.size();
	for (size_t i = 0; i < targetCount; ++i)
	{
		SIMDMath::Vec3SIMD targetPosSIMD(targets[i].m_vPos);
		distances[i] = eyePosSIMD.DistTo(targetPosSIMD);
	}
}

// Batch FOV calculation using SIMD
void CAimbotHitscan::BatchCalculateFOV(const Vec3& viewAngles, const std::vector<Target_t>& targets, float* fovs) const
{
	std::vector<Vec3> viewAnglesArray(targets.size(), viewAngles);
	std::vector<Vec3> targetAnglesArray(targets.size());
	
	SIMDMath::Vec3SIMD viewAnglesSIMD(viewAngles);
	
	for (size_t i = 0; i < targets.size(); ++i)
	{
		SIMDMath::Vec3SIMD targetPosSIMD(targets[i].m_vPos);
		targetAnglesArray[i] = SIMDMath::CalcAngleSIMD(viewAnglesSIMD, targetPosSIMD).ToVec3();
	}
	
	SIMDMath::Optimized::CalculateFOVBatch(
		viewAnglesArray.data(), 
		targetAnglesArray.data(), 
		fovs, 
		static_cast<int>(targets.size())
	);
}

int CAimbotHitscan::GetHitboxPriority(int nHitbox, CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CBaseEntity* pTarget)
{
	bool bHeadshot = false;
	if (pTarget->IsPlayer())
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

			if (G::CanHeadshot || pLocal->InCond(TF_COND_AIMING) && (
					pSniperRifle->GetRifleType() == RIFLE_JARATE && SDK::AttribHookValue(0, "jarate_duration", pWeapon) > 0
					|| Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot
				))
				bHeadshot = true;
			break;
		}
		case TF_WEAPON_REVOLVER:
		{
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1
				&& (pWeapon->AmbassadorCanHeadshot() || Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot))
				bHeadshot = true;
		}
		}

		if (Vars::Aimbot::Hitscan::Hitboxes.Value & Vars::Aimbot::Hitscan::HitboxesEnum::BodyaimIfLethal && bHeadshot)
		{
			auto pPlayer = pTarget->As<CTFPlayer>();

			switch (pWeapon->GetWeaponID())
			{
			case TF_WEAPON_SNIPERRIFLE:
			case TF_WEAPON_SNIPERRIFLE_DECAP:
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			{
				auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

				int iDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
				if (pPlayer->m_iHealth() <= iDamage)
					bHeadshot = false;
				break;
			}
			case TF_WEAPON_REVOLVER:
			{
				if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1)
				{
					float flDistTo = pTarget->m_vecOrigin().DistTo(pLocal->m_vecOrigin());

					float flMult = SDK::AttribHookValue(1, "mult_dmg", pWeapon);
					int iDamage = std::ceil(Math::RemapVal(flDistTo, 90.f, 900.f, 60.f, 21.f) * flMult);
					if (pPlayer->m_iHealth() <= iDamage)
						bHeadshot = false;
				}
			}
			}
		}
	}

	switch (H::Entities.GetModel(pTarget->entindex()))
	{
	case FNV1A::Hash32Const("models/vsh/player/saxton_hale.mdl"):
	case FNV1A::Hash32Const("models/vsh/player/hell_hale.mdl"):
	case FNV1A::Hash32Const("models/vsh/player/santa_hale.mdl"):
	{
		switch (nHitbox)
		{
		case HITBOX_SAXTON_HEAD: return bHeadshot ? 0 : 2;
		//case HITBOX_SAXTON_NECK:
		//case HITBOX_SAXTON_PELVIS: return 2;
		case HITBOX_SAXTON_BODY:
		case HITBOX_SAXTON_THORAX:
		case HITBOX_SAXTON_CHEST:
		case HITBOX_SAXTON_UPPER_CHEST: return bHeadshot ? 1 : 0;
		/*
		case HITBOX_SAXTON_LEFT_UPPER_ARM:
		case HITBOX_SAXTON_LEFT_FOREARM:
		case HITBOX_SAXTON_LEFT_HAND:
		case HITBOX_SAXTON_RIGHT_UPPER_ARM:
		case HITBOX_SAXTON_RIGHT_FOREARM:
		case HITBOX_SAXTON_RIGHT_HAND:
		case HITBOX_SAXTON_LEFT_THIGH:
		case HITBOX_SAXTON_LEFT_CALF:
		case HITBOX_SAXTON_LEFT_FOOT:
		case HITBOX_SAXTON_RIGHT_THIGH:
		case HITBOX_SAXTON_RIGHT_CALF:
		case HITBOX_SAXTON_RIGHT_FOOT:
		*/
		}
		break;
	}
	default:
	{
		switch (nHitbox)
		{
		case HITBOX_HEAD: return bHeadshot ? 0 : 2;
		//case HITBOX_PELVIS: return 2;
		case HITBOX_BODY:
		case HITBOX_THORAX:
		case HITBOX_CHEST:
		case HITBOX_UPPER_CHEST: return bHeadshot ? 1 : 0;
		/*
		case HITBOX_LEFT_UPPER_ARM:
		case HITBOX_LEFT_FOREARM:
		case HITBOX_LEFT_HAND:
		case HITBOX_RIGHT_UPPER_ARM:
		case HITBOX_RIGHT_FOREARM:
		case HITBOX_RIGHT_HAND:
		case HITBOX_LEFT_THIGH:
		case HITBOX_LEFT_CALF:
		case HITBOX_LEFT_FOOT:
		case HITBOX_RIGHT_THIGH:
		case HITBOX_RIGHT_CALF:
		case HITBOX_RIGHT_FOOT:
		*/
		}
	}
	}

	return 2;
};

int CAimbotHitscan::CanHit(Target_t& tTarget, CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	if (!AimbotSafety::IsValidEntity(tTarget.m_pEntity) || !AimbotSafety::IsValidPlayer(pLocal) || !pWeapon)
		return false;
	
	if (!AimbotSafety::IsValidGameState())
		return false;
	
	if (Vars::Aimbot::General::Ignore.Value & Vars::Aimbot::General::IgnoreEnum::Unsimulated && H::Entities.GetChoke(tTarget.m_pEntity->entindex()) > Vars::Aimbot::General::TickTolerance.Value)
		return false;

	bool bLowFPS = G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
	int iLowFPSFlags = bLowFPS ? Vars::Aimbot::Hitscan::LowFPSOptimizations.Value : 0;
	bool bSkipNonEssentialCalcs = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SkipNonEssentialCalculations);
	bool bReduceAnimationProcessing = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceAnimationProcessing);
	bool bSimplifiedCollision = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SimplifiedCollision);
	bool bDynamicPredictionQuality = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::DynamicPredictionQuality);
	bool bReduceMemoryUsage = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceMemoryUsage);

	Vec3 vEyePos = pLocal->GetShootPos(), vPeekPos = {};
	float flMaxRange = powf(pWeapon->GetRange(), 2.f);
	
#ifdef TEXTMODE
	// Special handling for flamethrower in textmode - it has limited range
	if (pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
	{
		flMaxRange = powf(230.f, 2.f); // Flamethrower has ~230 unit range
	}
#endif

	auto pModel = tTarget.m_pEntity->GetModel();
	if (!pModel) return false;
	
	if (!I::ModelInfoClient) return false;
	auto pHDR = I::ModelInfoClient->GetStudiomodel(pModel);
	if (!pHDR) return false;
	
	auto pAnimating = tTarget.m_pEntity->As<CBaseAnimating>();
	if (!pAnimating) return false;
	
	int nHitboxSet = pAnimating->m_nHitboxSet();
	if (nHitboxSet < 0 || nHitboxSet >= pHDR->numhitboxsets) return false;
	
	auto pSet = pHDR->pHitboxSet(nHitboxSet);
	if (!pSet || pSet->numhitboxes <= 0 || pSet->numhitboxes > MAXSTUDIOBONES) return false;

	std::vector<TickRecord*> vRecords = {};
	if (F::Backtrack.GetRecords(tTarget.m_pEntity, vRecords))
	{
		vRecords = F::Backtrack.GetValidRecords(vRecords, pLocal);
		if (vRecords.empty())
			return false;
		
		// Optimize backtracking records
		if (bLowFPS)
		{
			// Skip backtracking completely or use only 1 record if low FPS
			if ((iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SkipBacktracking) || 
				bSkipNonEssentialCalcs || bDynamicPredictionQuality || bReduceAnimationProcessing)
			{
				if (vRecords.size() > 1)
					vRecords.resize(1);
			}
			
			// Use fewer backtrack records based on how low FPS is
			else if (bReduceMemoryUsage && vRecords.size() > 2) 
			{
				float fpsRatio = G::GetFPS() / Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
				int recordLimit = std::max(1, int(vRecords.size() * fpsRatio));
				vRecords.resize(recordLimit);
			}
		}
	}
	else
	{
		matrix3x4 aBones[MAXSTUDIOBONES];
		
		// Initialize bone matrix to prevent crashes from uninitialized data
		memset(aBones, 0, sizeof(aBones));
		
		// Use simplified animation processing if enabled
		if (bReduceAnimationProcessing)
		{
			// Use a simplified setup bones call that skips non-essential bones
			if (!tTarget.m_pEntity->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_HITBOX, tTarget.m_pEntity->m_flSimulationTime()))
				return false;
		}
		else
		{
			// Use the full setup bones call
			if (!tTarget.m_pEntity->SetupBones(aBones, MAXSTUDIOBONES, BONE_USED_BY_ANYTHING, tTarget.m_pEntity->m_flSimulationTime()))
				return false;
		}

		std::vector<HitboxInfo> vHitboxInfos{};
		
		// Use simplified collision for entity bounds
		if (bSimplifiedCollision)
		{
			// Only process essential hitboxes instead of all of them
			int essentialHitboxes[] = { HITBOX_HEAD, HITBOX_BODY, HITBOX_CHEST };
			
			for (int i = 0; i < sizeof(essentialHitboxes) / sizeof(int); i++) 
			{
				int nHitbox = essentialHitboxes[i];
				// Validate hitbox index before accessing
				if (nHitbox < 0 || nHitbox >= pSet->numhitboxes)
					continue;
				auto pBox = pSet->pHitbox(nHitbox);
				if (!pBox) continue;

				// Use simpler collision bounds
				Vec3 iMin = pBox->bbmin, iMax = pBox->bbmax;
				int iBone = pBox->bone;
				
				if (iBone < 0 || iBone >= MAXSTUDIOBONES)
					continue;
				
				Vec3 vCenter{};
				Math::VectorTransform((iMin + iMax) / 2, aBones[iBone], vCenter);
				vHitboxInfos.emplace_back(iBone, nHitbox, vCenter, iMin, iMax);
			}
		}
		else
		{
			// Process all hitboxes normally with enhanced safety checks
			int maxHitboxes = std::min(pSet->numhitboxes, 32); // Limit to reasonable number
			for (int nHitbox = 0; nHitbox < maxHitboxes; nHitbox++)
			{
				auto pBox = pSet->pHitbox(nHitbox);
				if (!pBox) continue;

				const Vec3 iMin = pBox->bbmin, iMax = pBox->bbmax;
				const int iBone = pBox->bone;
				
				// Enhanced bone validation using safety utilities
				if (!AimbotSafety::IsValidBoneIndex(iBone, pHDR->numbones))
					continue;
				
				// Validate bone matrix is not null/corrupted
				if (!AimbotSafety::IsValidBoneMatrix(aBones[iBone]))
					continue;
				
				Vec3 vCenter{};
				Math::VectorTransform((iMin + iMax) / 2, aBones[iBone], vCenter);
				
				// Validate the resulting position using safety utilities
				if (AimbotSafety::IsValidPosition(vCenter))
					vHitboxInfos.emplace_back(iBone, nHitbox, vCenter, iMin, iMax);
			}
		}
		
		F::Backtrack.m_tRecord = { tTarget.m_pEntity->m_flSimulationTime(), tTarget.m_pEntity->m_vecOrigin(), Vec3(), Vec3(), *reinterpret_cast<BoneMatrix*>(&aBones), vHitboxInfos };
		vRecords = { &F::Backtrack.m_tRecord };
	}

	float flSpread = pWeapon->GetWeaponSpread();
	if (flSpread && Vars::Aimbot::General::HitscanPeek.Value)
		vPeekPos = vEyePos + pLocal->m_vecVelocity() * TICKS_TO_TIME(-Vars::Aimbot::General::HitscanPeek.Value);

	// if we're doubletapping, we can't change viewangles so work around that
	static int iTargetBone = 0;
	Vec3* pDoubletapAngle = F::Ticks.GetShootAngle();
	if (pDoubletapAngle && tTarget.m_iTargetType == TargetEnum::Player)
	{
		std::sort(vRecords.begin(), vRecords.end(), [&](const TickRecord* a, const TickRecord* b) -> bool
			{
				if (!a || !b || !a->m_BoneMatrix.m_aBones || !b->m_BoneMatrix.m_aBones)
					return false;
				
				if (iTargetBone < 0 || iTargetBone >= MAXSTUDIOBONES)
					return false;
				
				// Fix: vPosB should use 'b', not 'a'
				Vec3 vPosA = { a->m_BoneMatrix.m_aBones[iTargetBone][0][3], a->m_BoneMatrix.m_aBones[iTargetBone][1][3], a->m_BoneMatrix.m_aBones[iTargetBone][2][3] };
				Vec3 vPosB = { b->m_BoneMatrix.m_aBones[iTargetBone][0][3], b->m_BoneMatrix.m_aBones[iTargetBone][1][3], b->m_BoneMatrix.m_aBones[iTargetBone][2][3] };
				Vec3 vAnglesA = Math::CalcAngle(vEyePos, vPosA);
				Vec3 vAnglesB = Math::CalcAngle(vEyePos, vPosB);
				return pDoubletapAngle->DeltaAngle(vAnglesA).Length2D() < pDoubletapAngle->DeltaAngle(vAnglesB).Length2D();
			});
	}

	int iReturn = false;
	for (auto pRecord : vRecords)
	{
		bool bRunPeekCheck = flSpread && (Vars::Aimbot::General::PeekDTOnly.Value ? F::Ticks.GetTicks(pWeapon) : true) && Vars::Aimbot::General::HitscanPeek.Value;

		if (pWeapon->GetWeaponID() == TF_WEAPON_LASER_POINTER)
		{
			tTarget.m_vPos = tTarget.m_pEntity->m_vecOrigin();

			// not lag compensated (i assume) so run movesim based on ping
			PlayerStorage tStorage;
			F::MoveSim.Initialize(tTarget.m_pEntity, tStorage);
			if (!tStorage.m_bFailed)
			{
				for (int i = 1 - TIME_TO_TICKS(F::Backtrack.GetReal()); i <= 0; i++)
				{
					F::MoveSim.RunTick(tStorage);
					tTarget.m_vPos = tStorage.m_vPredictedOrigin;
				}
			}
			F::MoveSim.Restore(tStorage);

			float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::PointScale.Value / 100.f);
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			Vec3 vMins = tTarget.m_pEntity->m_vecMins();
			Vec3 vMaxs = tTarget.m_pEntity->m_vecMaxs();
			Vec3 vCheckMins = (vMins + flBoneSubtract) * flBoneScale;
			Vec3 vCheckMaxs = (vMaxs - flBoneSubtract) * flBoneScale;

			const matrix3x4 mTransform = { { 1, 0, 0, tTarget.m_vPos.x }, { 0, 1, 0, tTarget.m_vPos.y }, { 0, 0, 1, tTarget.m_vPos.z } };

			tTarget.m_vPos += tTarget.m_pEntity->GetOffset() / 2;
			if (vEyePos.DistToSqr(tTarget.m_vPos) > flMaxRange)
				break;

			bool bVisCheck = true;
			if ((bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::FastVisChecks)) || bSkipNonEssentialCalcs)
			{
				CGameTrace trace = {};
				CTraceFilterHitscan filter = {}; filter.pSkip = pLocal;
				SDK::Trace(vEyePos, tTarget.m_vPos, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
				bVisCheck = (trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity);
			}
			else
			{
				bVisCheck = SDK::VisPosWorld(pLocal, tTarget.m_pEntity, vEyePos, tTarget.m_vPos);
			}

			if (bVisCheck)
			{
				Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(vEyePos, tTarget.m_vPos), vAngles);
				Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
				float flDist = vEyePos.DistTo(tTarget.m_vPos);

				if (!bChanged || Math::RayToOBB(vEyePos, vForward, vCheckMins, vCheckMaxs, mTransform) && SDK::VisPos(pLocal, tTarget.m_pEntity, vEyePos, vEyePos + vForward * flDist))
				{
					tTarget.m_vAngleTo = vAngles;
					tTarget.m_pRecord = pRecord;
					return true;
				}
				else if (iReturn == 2 ? vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() : true)
					tTarget.m_vAngleTo = vAngles;
				iReturn = 2;
			}

			break;
		}

		if (tTarget.m_iTargetType == TargetEnum::Player)
		{
			auto aBones = pRecord->m_BoneMatrix.m_aBones;
			if (!aBones)
				continue;

			std::vector<std::tuple<HitboxInfo, int, int>> vHitboxes;
			for (int i = 0; i < pRecord->m_vHitboxInfos.size(); i++)
			{
				auto HitboxInfo = pRecord->m_vHitboxInfos[i];
				const int nHitbox = HitboxInfo.m_nHitbox;

				if (bLowFPS)
				{
					if (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceHitboxes)
					{
						if (nHitbox != HITBOX_HEAD && nHitbox != HITBOX_BODY && nHitbox != HITBOX_CHEST)
							continue;
					}
					
					if (bSkipNonEssentialCalcs)
					{
						if (pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE || 
							pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_DECAP || 
							pWeapon->GetWeaponID() == TF_WEAPON_SNIPERRIFLE_CLASSIC)
						{
							if (G::CanHeadshot) {
								if (nHitbox != HITBOX_HEAD)
									continue;
							}
							else if (nHitbox != HITBOX_BODY)
								continue;
						}
						else if (nHitbox != HITBOX_BODY)
							continue;
					}
				}

				if (!F::AimbotGlobal.IsHitboxValid(H::Entities.GetModel(tTarget.m_pEntity->entindex()), nHitbox, Vars::Aimbot::Hitscan::Hitboxes.Value))
					continue;

				int iPriority = GetHitboxPriority(nHitbox, pLocal, pWeapon, tTarget.m_pEntity);
				vHitboxes.emplace_back(HitboxInfo, nHitbox, iPriority);
			}
			std::sort(vHitboxes.begin(), vHitboxes.end(), [&](const auto& a, const auto& b) -> bool
					  {
						  return std::get<2>(a) < std::get<2>(b);
					  });

			float flModelScale = tTarget.m_pEntity->As<CBaseAnimating>()->m_flModelScale();
			float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::PointScale.Value / 100.f);
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			auto pGameRules = I::TFGameRules();
			auto pViewVectors = pGameRules ? pGameRules->GetViewVectors() : nullptr;
			Vec3 vHullMins = (pViewVectors ? pViewVectors->m_vHullMin : Vec3(-24, -24, 0)) * flModelScale;
			Vec3 vHullMaxs = (pViewVectors ? pViewVectors->m_vHullMax : Vec3(24, 24, 82)) * flModelScale;

			const matrix3x4 mTransform = { { 1, 0, 0, pRecord->m_vOrigin.x }, { 0, 1, 0, pRecord->m_vOrigin.y }, { 0, 0, 1, pRecord->m_vOrigin.z } };

			for (auto& [tHitboxInfo, iHitbox, _] : vHitboxes)
			{
				// Validate bone index to prevent access violation
				if (tHitboxInfo.m_iBone < 0 || tHitboxInfo.m_iBone >= MAXSTUDIOBONES)
					continue;
				
				Vec3 vAngle; Math::MatrixAngles(aBones[tHitboxInfo.m_iBone], vAngle);
				Vec3 vMins = tHitboxInfo.m_iMin;
				Vec3 vMaxs = tHitboxInfo.m_iMax;
				Vec3 vCheckMins = (vMins + flBoneSubtract / flModelScale) * flBoneScale;
				Vec3 vCheckMaxs = (vMaxs - flBoneSubtract / flModelScale) * flBoneScale;

				Vec3 vOffset;
				{
					Vec3 vOrigin;
					Math::VectorTransform({}, aBones[tHitboxInfo.m_iBone], vOrigin);
					vOffset = tHitboxInfo.m_vCenter - vOrigin;
				}

				std::vector<Vec3> vPoints = { Vec3() };
				if (Vars::Aimbot::Hitscan::PointScale.Value > 0.f)
				{
					bool bTriggerbot = (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
						|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive)
						&& !Vars::Aimbot::General::AssistStrength.Value;

					if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SimplifyScans))
					{
						vPoints = { Vec3() };
					}
					else if (!bTriggerbot)
					{
						float flScale = Vars::Aimbot::Hitscan::PointScale.Value / 100;
						Vec3 vMinsS = (vMins - vMaxs) / 2 * flScale;
						Vec3 vMaxsS = (vMaxs - vMins) / 2 * flScale;

						vPoints = {
							Vec3(),
							Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
							Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
							Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
							Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
							Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
							Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
							Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
							Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
						};
					}
				}

				for (auto& vPoint : vPoints)
				{
					if (tHitboxInfo.m_iBone < 0 || tHitboxInfo.m_iBone >= MAXSTUDIOBONES)
						break;
					
					Vec3 vOrigin; Math::VectorTransform(vPoint, aBones[tHitboxInfo.m_iBone], vOrigin); vOrigin += vOffset;

					if (vEyePos.DistToSqr(vOrigin) > flMaxRange)
						continue;

					if (bRunPeekCheck)
					{
						bRunPeekCheck = false;
						if (!SDK::VisPos(pLocal, tTarget.m_pEntity, vPeekPos, vOrigin))
							goto nextTick; // if we can't hit our primary hitbox, don't bother
					}

					bool bVisCheck = true;
					if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::FastVisChecks))
					{
						CGameTrace trace = {};
						CTraceFilterHitscan filter = {}; filter.pSkip = pLocal;
						SDK::Trace(vEyePos, vOrigin, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
						bVisCheck = (trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity);
					}
					else
					{
						bVisCheck = SDK::VisPos(pLocal, tTarget.m_pEntity, vEyePos, vOrigin);
					}

					Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(vEyePos, vOrigin), vAngles);
					Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
					float flDist = vEyePos.DistTo(vOrigin);

					if (bChanged || bVisCheck)
					{
						// for the time being, no vischecks against other hitboxes
						if ((!bChanged || (tHitboxInfo.m_iBone >= 0 && tHitboxInfo.m_iBone < MAXSTUDIOBONES && Math::RayToOBB(vEyePos, vForward, vCheckMins, vCheckMaxs, aBones[tHitboxInfo.m_iBone], flModelScale)) && SDK::VisPos(pLocal, tTarget.m_pEntity, vEyePos, vEyePos + vForward * flDist))
							&& Math::RayToOBB(vEyePos, vForward, vHullMins, vHullMaxs, mTransform))
						{
							iTargetBone = tHitboxInfo.m_iBone;

							tTarget.m_vAngleTo = vAngles;
							tTarget.m_pRecord = pRecord;
							tTarget.m_vPos = vOrigin;
							tTarget.m_nAimedHitbox = iHitbox;
							tTarget.m_bBacktrack = true;
							return true;
						}
						else if (bChanged && bVisCheck)
						{
							if (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D())
								tTarget.m_vAngleTo = vAngles;
							iReturn = 2;
						}
					}
				}
			}
		}
		else
		{
			float flBoneScale = std::max(Vars::Aimbot::Hitscan::BoneSizeMinimumScale.Value, Vars::Aimbot::Hitscan::PointScale.Value / 100.f);
			float flBoneSubtract = Vars::Aimbot::Hitscan::BoneSizeSubtract.Value;

			Vec3 vMins = tTarget.m_pEntity->m_vecMins();
			Vec3 vMaxs = tTarget.m_pEntity->m_vecMaxs();
			Vec3 vCheckMins = (vMins + flBoneSubtract) * flBoneScale;
			Vec3 vCheckMaxs = (vMaxs - flBoneSubtract) * flBoneScale;

			auto pCollideable = tTarget.m_pEntity->GetCollideable();
			const matrix3x4& mTransform = pCollideable ? pCollideable->CollisionToWorldTransform() : tTarget.m_pEntity->RenderableToWorldTransform();

			std::vector<Vec3> vPoints = { Vec3() };
			
			if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SimplifyScans))
			{
				vPoints = { Vec3() };
			}
			else
			{
				bool bTriggerbot = (Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Smooth
					|| Vars::Aimbot::General::AimType.Value == Vars::Aimbot::General::AimTypeEnum::Assistive)
					&& !Vars::Aimbot::General::AssistStrength.Value;

				if (!bTriggerbot)
				{
					float flScale = 0.5f; //Vars::Aimbot::Hitscan::PointScale.Value / 100;
					Vec3 vMinsS = (vMins - vMaxs) / 2 * flScale;
					Vec3 vMaxsS = (vMaxs - vMins) / 2 * flScale;

					vPoints = {
						Vec3(),
						Vec3(vMinsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMaxsS.z),
						Vec3(vMinsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMinsS.y, vMinsS.z),
						Vec3(vMinsS.x, vMaxsS.y, vMinsS.z),
						Vec3(vMaxsS.x, vMaxsS.y, vMinsS.z)
					};
				}
			}

			for (auto& vPoint : vPoints)
			{
				Vec3 vOrigin = tTarget.m_pEntity->GetCenter() + vPoint;

				if (vEyePos.DistToSqr(vOrigin) > flMaxRange)
					continue;

				bool bVisCheck = true;
				if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::FastVisChecks))
				{
					CGameTrace trace = {};
					CTraceFilterHitscan filter = {}; filter.pSkip = pLocal;
					SDK::Trace(vEyePos, vOrigin, MASK_SHOT | CONTENTS_GRATE, &filter, &trace);
					bVisCheck = (trace.m_pEnt && trace.m_pEnt == tTarget.m_pEntity);
				}
				else
				{
					bVisCheck = SDK::VisPos(pLocal, tTarget.m_pEntity, vEyePos, vOrigin);
				}

				Vec3 vAngles; bool bChanged = Aim(G::CurrentUserCmd->viewangles, Math::CalcAngle(vEyePos, vOrigin), vAngles);
				Vec3 vForward; Math::AngleVectors(vAngles, &vForward);
				float flDist = vEyePos.DistTo(vOrigin);

				if (bChanged || bVisCheck)
				{
					if (!bChanged || Math::RayToOBB(vEyePos, vForward, vCheckMins, vCheckMaxs, mTransform) && SDK::VisPos(pLocal, tTarget.m_pEntity, vEyePos, vEyePos + vForward * flDist))
					{
						tTarget.m_vAngleTo = vAngles;
						tTarget.m_pRecord = pRecord;
						tTarget.m_vPos = vOrigin;
						return true;
					}
					else if (bChanged && bVisCheck)
					{
						if (iReturn != 2 || vAngles.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D() < tTarget.m_vAngleTo.DeltaAngle(G::CurrentUserCmd->viewangles).Length2D())
							tTarget.m_vAngleTo = vAngles;
						iReturn = 2;
					}
				}
			}
		}

		nextTick:
		continue;
	}

	return iReturn;
}

/* Returns whether AutoShoot should fire */
bool CAimbotHitscan::ShouldFire(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd, const Target_t& tTarget)
{
	if (!Vars::Aimbot::General::AutoShoot.Value) return false;

	if (Vars::Aimbot::Hitscan::ShootingDelayEnabled.Value)
	{
		float flCurrentTime = I::GlobalVars->curtime;
		
#ifdef TEXTMODE
		// Flamethrower needs continuous fire - reduce delay significantly
		if (pWeapon->GetWeaponID() == TF_WEAPON_FLAMETHROWER)
		{
			if (flCurrentTime - m_flLastShotTime < 0.05f) // Very short delay for flamethrower
				return false;
		}
		else
#endif
		{
			if (flCurrentTime - m_flLastShotTime < Vars::Aimbot::Hitscan::ShootingDelay.Value)
				return false;
		}
	}
	
	bool bLowFPS = G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
	int iLowFPSFlags = bLowFPS ? Vars::Aimbot::Hitscan::LowFPSOptimizations.Value : 0;
	
	if (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::LimitShootingRate))
	{
		float flCurrentTime = I::GlobalVars->curtime;
		if (flCurrentTime - m_flLastShotTime < Vars::Aimbot::Hitscan::LowFPSShootingDelay.Value)
			return false;
	}

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForHeadshot)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
			if (!G::CanHeadshot && pLocal->InCond(TF_COND_AIMING) && pWeapon->As<CTFSniperRifle>()->GetRifleType() != RIFLE_JARATE)
				return false;
			break;
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
			if (!G::CanHeadshot)
				return false;
			break;
		case TF_WEAPON_REVOLVER:
			if (SDK::AttribHookValue(0, "set_weapon_mode", pWeapon) == 1 && !pWeapon->AmbassadorCanHeadshot())
				return false;
		}
	}

	if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::WaitForCharge)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_SNIPERRIFLE:
		case TF_WEAPON_SNIPERRIFLE_DECAP:
		case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		{
			auto pPlayer = tTarget.m_pEntity->As<CTFPlayer>();
			auto pSniperRifle = pWeapon->As<CTFSniperRifle>();

			if (!pLocal->InCond(TF_COND_AIMING) || pSniperRifle->m_flChargedDamage() == 150.f)
				break;

			if (tTarget.m_nAimedHitbox == HITBOX_HEAD && (pWeapon->GetWeaponID() != TF_WEAPON_SNIPERRIFLE_CLASSIC ? true : pSniperRifle->m_flChargedDamage() == 150.f))
			{
				int iHeadDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetHeadshotMult(pPlayer));
				if (pPlayer->m_iHealth() <= iHeadDamage && (G::CanHeadshot || pLocal->IsCritBoosted()))
					break;
			}
			else
			{
				int iBodyDamage = std::ceil(std::max(pSniperRifle->m_flChargedDamage(), 50.f) * pSniperRifle->GetBodyshotMult(pPlayer));
				if (pPlayer->m_iHealth() <= iBodyDamage)
					break;
			}

			return false;
		}
		}
	}

	return true;
}

bool CAimbotHitscan::Aim(Vec3 vCurAngle, Vec3 vToAngle, Vec3& vOut, int iMethod)
{
	auto pLocal = H::Entities.GetLocal();
	Vec3 vPunch = pLocal ? pLocal->m_vecPunchAngle() : Vec3();

	if (Vec3* pDoubletapAngle = F::Ticks.GetShootAngle())
	{
		vOut = *pDoubletapAngle - vPunch;
		return true;
	}

	bool bReturn = false;
	vToAngle -= vPunch;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
	case Vars::Aimbot::General::AimTypeEnum::Silent:
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		vOut = vToAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	{
		// Replicate seoaim style incremental smoothing.
		const float flSmoothFactor = std::clamp<float>(Vars::Aimbot::General::AssistStrength.Value, 0.f, 100.f);
		// If the user sets 100 we snap directly to target (no smoothing), same behaviour as seoaim.
		if (flSmoothFactor >= 100.f)
		{
			vOut = vToAngle;
			return true; // no smoothing, but we still changed the angle
		}

		// Calculate angular delta and clamp to valid range.
		Vec3 vDelta = vToAngle - vCurAngle;
		Math::ClampAngles(vDelta);

		// Remap the user supplied value so low values are fast and high values are slow.
		const float flSmoothDiv = Math::RemapVal(flSmoothFactor, 1.f, 100.f, 1.5f, 30.f);
		vOut = vCurAngle + vDelta / flSmoothDiv;
		Math::ClampAngles(vOut);
		return true;
	}
	case Vars::Aimbot::General::AimTypeEnum::Assistive: {
		Vec3 vMouseDelta = G::CurrentUserCmd->viewangles.DeltaAngle(G::LastUserCmd->viewangles);
		Vec3 vTargetDelta = vToAngle.DeltaAngle(G::LastUserCmd->viewangles);
		float flMouseDelta = vMouseDelta.Length2D(), flTargetDelta = vTargetDelta.Length2D();
		vTargetDelta = vTargetDelta.Normalized() * std::min(flMouseDelta, flTargetDelta);
		vOut = vCurAngle - vMouseDelta + vMouseDelta.LerpAngle(vTargetDelta, Vars::Aimbot::General::AssistStrength.Value / 100.f);
		bReturn = true;
		break;
	}
	case Vars::Aimbot::General::AimTypeEnum::Legit:
	{
		Vec3 vDelta = vToAngle - vCurAngle;
		Math::ClampAngles(vDelta);

		float flLen = vDelta.Length2D();
		float flDiv = std::clamp(Math::RemapVal(flLen, 0.f, 90.f, 8.f, 2.f), 2.f, 8.f);

		vOut = vCurAngle + vDelta / flDiv;
		Math::ClampAngles(vOut);
		return true;
	}
	}

	Math::ClampAngles(vOut);
	return bReturn;
}

// assume angle calculated outside with other overload
void CAimbotHitscan::Aim(CUserCmd* pCmd, Vec3& vAngle, int iMethod)
{
	bool bDoubleTap = F::Ticks.m_bDoubletap || F::Ticks.GetTicks(H::Entities.GetWeapon()) || F::Ticks.m_bSpeedhack;
	switch (iMethod)
	{
	case Vars::Aimbot::General::AimTypeEnum::Plain:
		if (G::Attacking != 1 && !bDoubleTap)
			break;
		[[fallthrough]];
	case Vars::Aimbot::General::AimTypeEnum::Smooth:
	case Vars::Aimbot::General::AimTypeEnum::Assistive:
		pCmd->viewangles = vAngle;
		I::EngineClient->SetViewAngles(vAngle);
		break;
	case Vars::Aimbot::General::AimTypeEnum::Silent:
		if (G::Attacking == 1 || bDoubleTap)
		{
			SDK::FixMovement(pCmd, vAngle);
			pCmd->viewangles = vAngle;
			G::SilentAngles = true;
		}
		break;
	case Vars::Aimbot::General::AimTypeEnum::Locking:
		SDK::FixMovement(pCmd, vAngle);
		pCmd->viewangles = vAngle;
		break;
	case Vars::Aimbot::General::AimTypeEnum::Legit:
		pCmd->viewangles = vAngle;
		I::EngineClient->SetViewAngles(vAngle);
		break;
	}
}

static inline void DrawVisuals(CTFPlayer* pLocal, Target_t& tTarget, int nWeaponID)
{
	if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER)
	{
		bool bLine = Vars::Visuals::Line::Enabled.Value;
		bool bBoxes = Vars::Visuals::Hitbox::BonesEnabled.Value & Vars::Visuals::Hitbox::BonesEnabledEnum::OnShot;
		if (G::CanPrimaryAttack && (bLine || bBoxes))
		{
			G::LineStorage.clear();
			G::BoxStorage.clear();
			G::PathStorage.clear();

			if (bLine)
			{
				Vec3 vEyePos = pLocal->GetShootPos();
				float flDist = vEyePos.DistTo(tTarget.m_vPos);
				Vec3 vForward; Math::AngleVectors(tTarget.m_vAngleTo + pLocal->m_vecPunchAngle(), &vForward);

				if (Vars::Colors::LineIgnoreZ.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::LineIgnoreZ.Value);
				if (Vars::Colors::Line.Value.a)
					G::LineStorage.emplace_back(std::pair<Vec3, Vec3>(vEyePos, vEyePos + vForward * flDist), I::GlobalVars->curtime + Vars::Visuals::Line::DrawDuration.Value, Vars::Colors::Line.Value, true);
			}
			if (bBoxes)
			{
				auto vBoxes = F::Visuals.GetHitboxes(tTarget.m_pRecord->m_BoneMatrix.m_aBones, tTarget.m_pEntity->As<CBaseAnimating>(), {}, tTarget.m_nAimedHitbox);
				G::BoxStorage.insert(G::BoxStorage.end(), vBoxes.begin(), vBoxes.end());
			}
		}
	}
}

void CAimbotHitscan::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	// Critical safety checks to prevent crashes during map loading/transitions
	if (!pLocal || !pWeapon || !pCmd || !I::EngineClient || !I::EngineClient->IsInGame() || !I::GlobalVars)
		return;
	
	// Additional validation for textmode stability
	if (pLocal->IsDormant() || pLocal->entindex() <= 0)
		return;
	
	const int nWeaponID = pWeapon->GetWeaponID();

	static int iStaticAimType = Vars::Aimbot::General::AimType.Value;
	const int iLastAimType = iStaticAimType;
	const int iRealAimType = Vars::Aimbot::General::AimType.Value;

	bool bLowFPS = G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value;
	int iLowFPSFlags = bLowFPS ? Vars::Aimbot::Hitscan::LowFPSOptimizations.Value : 0;
	bool bTargetAll = Vars::Aimbot::Hitscan::TargetEveryone.Value || (bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::TargetAll));

	// Apply additional optimizations for low FPS
	bool bDisableBulletTracers = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::DisableBulletTracers);
	bool bReduceViewModelEffects = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::ReduceViewModelEffects);
	bool bSkipNonEssentialCalcs = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::SkipNonEssentialCalculations);
	bool bDisableExtraVisualProcessing = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::DisableExtraVisualProcessing);
	bool bAdaptiveTickProcessing = bLowFPS && (iLowFPSFlags & Vars::Aimbot::Hitscan::LowFPSOptimizationsEnum::AdaptiveTickProcessing);

	// Implement Adaptive Tick Processing - skip processing on some ticks when FPS is very low
	// This significantly reduces CPU load while maintaining functionality
	if (bAdaptiveTickProcessing)
	{
		static int iTickCounter = 0;
		iTickCounter++;
		
		// Skip every other tick when FPS is very low
		if (G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value * 0.5f && iTickCounter % 2 != 0)
		{
			return;
		}
		
		// Skip 2 out of 3 ticks when FPS is extremely low
		if (G::GetFPS() < Vars::Aimbot::Hitscan::LowFPSThreshold.Value * 0.3f && iTickCounter % 3 != 0)
		{
			return;
		}
	}

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (G::Attacking && !iRealAimType && iLastAimType)
			Vars::Aimbot::General::AimType.Value = iLastAimType;
	}
	iStaticAimType = Vars::Aimbot::General::AimType.Value;

	if (F::AimbotGlobal.ShouldHoldAttack(pWeapon))
		pCmd->buttons |= IN_ATTACK;
	if (!Vars::Aimbot::General::AimType.Value
		|| !F::AimbotGlobal.ShouldAim() && (nWeaponID != TF_WEAPON_MINIGUN || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_FIRING || pWeapon->As<CTFMinigun>()->m_iWeaponState() == AC_STATE_SPINNING))
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_MINIGUN:
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoRev)
			pCmd->buttons |= IN_ATTACK2;
		if (pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_FIRING && pWeapon->As<CTFMinigun>()->m_iWeaponState() != AC_STATE_SPINNING)
			return;
		break;
#ifdef TEXTMODE
	case TF_WEAPON_FLAMETHROWER:
		// Flamethrower needs continuous fire to be effective
		// Don't apply strict timing restrictions like other weapons
		break;
#endif
	}

	auto vTargets = SortTargets(pLocal, pWeapon);
	if (vTargets.empty())
		return;

	switch (nWeaponID)
	{
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	{
		bool bScoped = pLocal->InCond(TF_COND_ZOOMED);
		bool bShouldScope = false;
		bool bShouldUnscope = false;
		
		if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::DistanceAutoscope)
		{
			bool bHasEnemiesInRange = HasEnemiesInRange(pLocal, Vars::Aimbot::Hitscan::DistanceAutoscopeRange.Value);
			bool bNavBotNeedsJump = NavBotNeedsJump(pLocal);
			
			if (bHasEnemiesInRange && !bNavBotNeedsJump && !bScoped)
			{
				bShouldScope = true;
			}
			else if ((!bHasEnemiesInRange || bNavBotNeedsJump) && bScoped)
			{
				bShouldUnscope = true;
			}
		}
		// Original autoscope behavior
		else if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::AutoScope && !bScoped)
		{
			bShouldScope = true;
		}
		
		if (bShouldScope)
		{
			pCmd->buttons |= IN_ATTACK2;
			return;
		}
		else if (bShouldUnscope)
		{
			pCmd->buttons |= IN_ATTACK2;
			return;
		}
		else if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::ScopedOnly && !bScoped)
			return;
		else if (!bScoped && SDK::AttribHookValue(0, "sniper_only_fire_zoomed", pWeapon))
			return;
		break;
	}
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
		if (iRealAimType)
			pCmd->buttons |= IN_ATTACK;
	}

	if (!G::AimTarget.m_iEntIndex)
		G::AimTarget = { vTargets.front().m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };

	static std::unordered_set<int> targetedEntities;
	targetedEntities.clear();

	for (auto& tTarget : vTargets)
	{
		// Enhanced target validation
		if (!tTarget.m_pEntity || tTarget.m_pEntity->IsDormant() || tTarget.m_pEntity->entindex() <= 0)
			continue;
		
		if (bTargetAll && targetedEntities.find(tTarget.m_pEntity->entindex()) != targetedEntities.end())
			continue;

		if (nWeaponID == TF_WEAPON_MEDIGUN && pWeapon->As<CWeaponMedigun>()->m_hHealingTarget().Get() == tTarget.m_pEntity)
		{
			if (G::LastUserCmd && G::LastUserCmd->buttons & IN_ATTACK)
				pCmd->buttons |= IN_ATTACK;
			return;
		}

		// Wrap critical operations in try-catch for stability
		int iResult = 0;
		try {
			iResult = CanHit(tTarget, pLocal, pWeapon);
		} catch (...) {
			continue; // Skip this target if it causes issues
		}
		
		if (!iResult) continue;
		if (iResult == 2)
		{
			G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount, 0 };
			Aim(pCmd, tTarget.m_vAngleTo);
			
			if (bTargetAll)
				targetedEntities.insert(tTarget.m_pEntity->entindex());
				
			if (!bTargetAll)
				break;
			continue;
		}

		G::AimTarget = { tTarget.m_pEntity->entindex(), I::GlobalVars->tickcount };
		G::AimPoint = { tTarget.m_vPos, I::GlobalVars->tickcount };

		if (ShouldFire(pLocal, pWeapon, pCmd, tTarget))
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_MEDIGUN:
				if (!(G::LastUserCmd->buttons & IN_ATTACK))
					pCmd->buttons |= IN_ATTACK;
				break;
			case TF_WEAPON_SNIPERRIFLE_CLASSIC:
				if (pWeapon->As<CTFSniperRifle>()->m_flChargedDamage() && pLocal->m_hGroundEntity())
					pCmd->buttons &= ~IN_ATTACK;
				break;
			case TF_WEAPON_LASER_POINTER:
				pCmd->buttons |= IN_ATTACK | IN_ATTACK2;
				break;
			default:
				pCmd->buttons |= IN_ATTACK;
			}

			if (Vars::Aimbot::Hitscan::Modifiers.Value & Vars::Aimbot::Hitscan::ModifiersEnum::Tapfire && pWeapon->GetWeaponSpread() != 0.f && !pLocal->InCond(TF_COND_RUNE_PRECISION)
				&& pLocal->GetShootPos().DistTo(tTarget.m_vPos) > Vars::Aimbot::Hitscan::TapFireDist.Value)
			{
				const float flTimeSinceLastShot = (pLocal->m_nTickBase() * TICK_INTERVAL) - pWeapon->m_flLastFireTime();
				if (flTimeSinceLastShot <= (pWeapon->GetBulletsPerShot() > 1 ? 0.25f : 1.25f))
					pCmd->buttons &= ~IN_ATTACK;
			}
		}

		G::Attacking = SDK::IsAttacking(pLocal, pWeapon, pCmd, true);
		if (G::Attacking == 1 && nWeaponID != TF_WEAPON_LASER_POINTER)
		{
			m_flLastShotTime = I::GlobalVars->curtime;
			
			if (tTarget.m_pEntity->IsPlayer())
				F::Resolver.HitscanRan(pLocal, tTarget.m_pEntity->As<CTFPlayer>(), pWeapon, tTarget.m_nAimedHitbox);

			if (tTarget.m_bBacktrack)
				pCmd->tick_count = TIME_TO_TICKS(tTarget.m_pRecord->m_flSimTime + F::Backtrack.GetFakeInterp());
		}
		DrawVisuals(pLocal, tTarget, nWeaponID);

		Aim(pCmd, tTarget.m_vAngleTo);
		if (G::SilentAngles)
		{
			switch (nWeaponID)
			{
			case TF_WEAPON_MEDIGUN:
			//case TF_WEAPON_LASER_POINTER: // we can psilent with the wrangler though probably with some hacks
				G::SilentAngles = false, G::PSilentAngles = true;
			}
		}
		break;
	}
}