#include "NBheader.h"
#include "../../SDK/SDK.h"
#include "../Players/PlayerUtils.h"
#include <algorithm>
#include <vector>
#include <optional>
#include <cmath>

bool CNavBot::IsMvMMode() const
{
    return SDK::IsMvM();
}

bool CNavBot::IsMvMTank(CBaseEntity* pEntity) const
{
    if (!pEntity || !IsMvMMode())
        return false;
    
    auto iClassID = pEntity->GetClassID();
    return iClassID == ETFClassID::CTFTankBoss;
}

bool CNavBot::IsMvMBot(CTFPlayer* pPlayer) const
{
    if (!pPlayer || !IsMvMMode())
        return false;
    
    PlayerInfo_t pi{};
    if (!I::EngineClient->GetPlayerInfo(pPlayer->entindex(), &pi))
        return false;
    
    return pPlayer->m_iTeamNum() == TF_TEAM_BLUE && pi.fakeplayer;
}

bool CNavBot::IsBombCarrier(CTFPlayer* pPlayer) const
{
    if (!pPlayer || !IsMvMMode())
        return false;

    if (pPlayer->m_bCarryingObject())
        return true;

    if (pPlayer->IsMarked())
        return true;

    return false;
}

int CNavBot::GetMvMTargetPriority(CTFPlayer* pPlayer) const
{
    if (!pPlayer || !IsMvMMode())
        return 0;
    
    if (IsBombCarrier(pPlayer))
        return 10;
    
    if (pPlayer->m_bIsMiniBoss())
        return 7;
    
    if (pPlayer->m_iClass() == TF_CLASS_MEDIC)
        return 6;
    
    if (IsMvMBot(pPlayer))
        return 3;
    
    return 0;
}

CNavBot::bot_class_config CNavBot::GetMvMClassConfig(CTFPlayer* pLocal) const
{
    if (!pLocal || !IsMvMMode())
        return m_tSelectedConfig;
    
    switch (pLocal->m_iClass())
    {
    case TF_CLASS_SCOUT:
    case TF_CLASS_PYRO:
        // Very close range for scout and pyro - almost melee distance
        return { 25.0f, 50.0f, 200.0f, false };
        
    case TF_CLASS_SNIPER:
        // Medium-long range for sniper - around 1300 units
        return { 1300.0f, 1400.0f, 2100.0f, true };
        
    case TF_CLASS_ENGINEER:
        // Engineers stay at medium range
        return { 1000.0f, 1100.0f, 1200.0f, false };
        
    case TF_CLASS_HEAVY:
    case TF_CLASS_SOLDIER:
    case TF_CLASS_DEMOMAN:
    case TF_CLASS_MEDIC:
    case TF_CLASS_SPY:
    default:
        // Medium range for other classes - 800-900 units
        return { 700.0f, 850.0f, 1500.0f, false };
    }
}

bool CNavBot::ShouldIgnoreDangerInMvM(CTFPlayer* pLocal) const
{
    if (!pLocal || !IsMvMMode())
        return false;
    
    int iClass = pLocal->m_iClass();
    return (iClass == TF_CLASS_PYRO || iClass == TF_CLASS_SCOUT);
}

slots CNavBot::GetMvMBestSlot(CTFPlayer* pLocal, slots eActiveSlot, ClosestEnemy_t tClosestEnemy)
{
    if (!pLocal || !IsMvMMode())
        return eActiveSlot;
    
    if (pLocal->m_iClass() == TF_CLASS_SCOUT)
        return GetBestSlot(pLocal, eActiveSlot, tClosestEnemy);
    
    auto pPrimaryWeapon = pLocal->GetWeaponFromSlot(SLOT_PRIMARY);
    if (pPrimaryWeapon && G::AmmoInSlot[SLOT_PRIMARY])
        return primary;
    
    auto pSecondaryWeapon = pLocal->GetWeaponFromSlot(SLOT_SECONDARY);
    if (pSecondaryWeapon && G::AmmoInSlot[SLOT_SECONDARY])
        return secondary;
    
    return melee;
}

bool CNavBot::MvMPyroTankLogic(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
    if (!pLocal || !pWeapon || !IsMvMMode() || pLocal->m_iClass() != TF_CLASS_PYRO)
        return false;
    
    CBaseEntity* pClosestTank = nullptr;
    float flClosestDist = FLT_MAX;
    
    for (auto pEntity : H::Entities.GetGroup(EGroupType::WORLD_NPC))
    {
        if (!IsMvMTank(pEntity))
            continue;
        
        float flDist = pEntity->GetAbsOrigin().DistToSqr(pLocal->GetAbsOrigin());
        if (flDist < flClosestDist)
        {
            flClosestDist = flDist;
            pClosestTank = pEntity;
        }
    }
    
    // If we found a tank, prioritize it
    if (pClosestTank)
    {
        // Navigate to the tank
        if (F::NavEngine.navTo(pClosestTank->GetAbsOrigin(), staynear, true, !F::NavEngine.isPathing()))
        {
            // If we're close enough, attack it
            if (flClosestDist <= pow(400.0f, 2))
            {
                auto vAngTo = Math::CalcAngle(pLocal->GetEyePosition(), pClosestTank->GetCenter());
                pCmd->viewangles = vAngTo;
                pCmd->buttons |= IN_ATTACK;
            }
            return true;
        }
    }
    
    return false;
}

std::vector<CTFPlayer*> CNavBot::GetMvMTargetsByPriority(CTFPlayer* pLocal) const
{
    if (!pLocal || !IsMvMMode())
        return {};
    
    std::vector<std::pair<CTFPlayer*, int>> vTargetsWithPriority;
    
    for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
    {
        if (!pEntity->IsPlayer())
            continue;
        
        auto pPlayer = pEntity->As<CTFPlayer>();
        if (!pPlayer->IsAlive() || !IsMvMBot(pPlayer))
            continue;
        
        int iPriority = GetMvMTargetPriority(pPlayer);
        if (iPriority > 0)
        {
            vTargetsWithPriority.push_back({ pPlayer, iPriority });
        }
    }
    
    // Sort by priority (highest first)
    std::sort(vTargetsWithPriority.begin(), vTargetsWithPriority.end(),
        [](const std::pair<CTFPlayer*, int>& a, const std::pair<CTFPlayer*, int>& b) {
            return a.second > b.second;
        });
    
    // Extract just the players
    std::vector<CTFPlayer*> vTargets;
    for (const auto& pair : vTargetsWithPriority)
    {
        vTargets.push_back(pair.first);
    }
    
    return vTargets;
}

// -----------------------------------------------------------------------------
// Bot cluster helper
// -----------------------------------------------------------------------------

std::optional<Vector> CNavBot::GetMvMBotClusterPos(CTFPlayer* pLocal) const
{
    if (!pLocal || !IsMvMMode())
        return std::nullopt;

    struct BotInfo
    {
        CTFPlayer* pBot;
        Vector vPos;
        int iNeighbors;
    };

    std::vector<BotInfo> vBots;

    for (auto pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ENEMIES))
    {
        if (!pEntity->IsPlayer())
            continue;

        auto pBot = pEntity->As<CTFPlayer>();
        if (!pBot->IsAlive() || !IsMvMBot(pBot))
            continue;

        vBots.push_back({ pBot, pBot->GetAbsOrigin(), 0 });
    }

    if (vBots.empty())
        return std::nullopt;

    const float flClusterRadiusSqr = 700.0f * 700.0f; // ~700 HU radius
    for (size_t i = 0; i < vBots.size(); ++i)
    {
        int count = 0;
        for (size_t j = 0; j < vBots.size(); ++j)
        {
            if (i == j)
                continue;
            if (vBots[i].vPos.DistToSqr(vBots[j].vPos) <= flClusterRadiusSqr)
                ++count;
        }
        vBots[i].iNeighbors = count;
    }

    // Find bot with most neighbours (i.e. cluster centre)
    auto itBest = std::max_element(vBots.begin(), vBots.end(), [](const BotInfo& a, const BotInfo& b)
    {
        return a.iNeighbors < b.iNeighbors;
    });

    if (itBest == vBots.end() || itBest->iNeighbors < 2) // Require at least 3 bots total in cluster
        return std::nullopt;

    return itBest->vPos;
}

bool CNavBot::MvMStayNear(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
    if (!pLocal || !IsMvMMode())
        return false;
    
    static Timer tMvMStayNearCooldown{};
    if (!tMvMStayNearCooldown.Run(0.5f))
        return F::NavEngine.current_priority == staynear;
    
    auto vTargets = GetMvMTargetsByPriority(pLocal);
    if (vTargets.empty())
        return false;
    
    auto pTarget = vTargets.front();
    if (!pTarget || pTarget->IsDormant())
        return false;
    
    auto mvmConfig = GetMvMClassConfig(pLocal);
    Vector vTargetPos = pTarget->GetAbsOrigin();
    Vector vLocalPos = pLocal->GetAbsOrigin();
    
    float flDist = vTargetPos.DistToSqr(vLocalPos);
    float flOptimalDist = mvmConfig.m_flMinSlightDanger;
    
    if (flDist < pow(mvmConfig.m_flMinFullDanger, 2) || flDist > pow(mvmConfig.m_flMax, 2))
    {
        Vector vDirection = vLocalPos - vTargetPos;
        vDirection.z = 0;
        float len = vDirection.Length();
        if (len > 0.001f)
        {
            vDirection.x /= len;
            vDirection.y /= len;
            vDirection.z /= len;
        }
        
        Vector vDesiredPos = vTargetPos + (vDirection * flOptimalDist);
        
        if (F::NavEngine.navTo(vDesiredPos, staynear, true, !F::NavEngine.isPathing()))
        {
            m_iStayNearTargetIdx = pTarget->entindex();
            return true;
        }
    }
    
    // At this point we failed to path directly around our top-priority target.  As a
    // fallback, move towards the densest group of robots so we stay in the action.

    if (auto vClusterPos = GetMvMBotClusterPos(pLocal))
    {
        if (F::NavEngine.navTo(*vClusterPos, staynear, true, !F::NavEngine.isPathing()))
        {
            m_iStayNearTargetIdx = -1; // we're targeting an area, not a single bot
            return true;
        }
    }

    if (!m_vMvMSpawnSpots.empty())
    {
        Vector vLocalPos = pLocal->GetAbsOrigin();
        float bestDist = FLT_MAX;
        Vector bestSpot;
        for (const auto& spot : m_vMvMSpawnSpots)
        {
            float d = spot.DistToSqr(vLocalPos);
            if (d < bestDist)
            {
                bestDist = d;
                bestSpot = spot;
            }
        }
        if (F::NavEngine.navTo(bestSpot, staynear, true, !F::NavEngine.isPathing()))
        {
            m_iStayNearTargetIdx = -1;
            return true;
        }
    }

    return false;
} 