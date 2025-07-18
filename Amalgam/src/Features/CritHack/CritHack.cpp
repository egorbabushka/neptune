#include "CritHack.h"

#include "../Ticks/Ticks.h"

#define WEAPON_RANDOM_RANGE				10000
#define TF_DAMAGE_CRIT_MULTIPLIER		3.0f
#define TF_DAMAGE_CRIT_CHANCE			0.02f
#define TF_DAMAGE_CRIT_CHANCE_RAPID		0.02f
#define TF_DAMAGE_CRIT_CHANCE_MELEE		0.15f
#define TF_DAMAGE_CRIT_DURATION_RAPID	2.0f

//#define SERVER_CRIT_DATA

void CCritHack::Fill(const CUserCmd* pCmd, int n)
{
	if (!m_iFillStart)
		m_iFillStart = pCmd->command_number;

	for (auto& [iSlot, tStorage] : m_mStorage)
	{
		if (!tStorage.m_bActive)
			continue;

		for (auto it = tStorage.m_vCritCommands.begin(); it != tStorage.m_vCritCommands.end();)
		{
			if (*it <= pCmd->command_number)
				it = tStorage.m_vCritCommands.erase(it);
			else
				++it;
		}
		for (auto it = tStorage.m_vSkipCommands.begin(); it != tStorage.m_vSkipCommands.end();)
		{
			if (*it <= pCmd->command_number)
				it = tStorage.m_vSkipCommands.erase(it);
			else
				++it;
		}
		/*tStorage.m_vCritCommands.clear( );
		tStorage.m_vSkipCommands.clear( );
		for ( int i = 0; i < ( sizeof( no_pipe_rotation_cmdnums ) / 4 ); i++ )
		{
			if ( tStorage.m_vCritCommands.size( ) >= unsigned( n ) )
				break;

			const int iCmdNum = no_pipe_rotation_cmdnums[ i ];
			if (IsCritCommand(iSlot, tStorage.m_iEntIndex, tStorage.m_flMultCritChance, iCmdNum))
				tStorage.m_vCritCommands.push_back(iCmdNum);
		}
		for ( int i = 0; i < ( sizeof( no_pipe_rotation_cmdnums ) / 4 ); i++ )
		{
			if ( tStorage.m_vSkipCommands.size( ) >= unsigned( n ) )
				break;

			const int iCmdNum = no_pipe_rotation_cmdnums[ i ];
			if ( IsCritCommand( iSlot, tStorage.m_iEntIndex, tStorage.m_flMultCritChance, iCmdNum, false ) )
				tStorage.m_vSkipCommands.push_back( iCmdNum );
		}*/
		for (int i = 0; i < n; i++)
		{
			if (tStorage.m_vCritCommands.size() >= unsigned(n))
				break;

			const int iCmdNum = m_iFillStart + i;
			if (IsCritCommand(iSlot, tStorage.m_iEntIndex, tStorage.m_flMultCritChance, iCmdNum))
				tStorage.m_vCritCommands.push_back(iCmdNum);
		}
		for (int i = 0; i < n; i++)
		{
			if (tStorage.m_vSkipCommands.size() >= unsigned(n))
				break;

			const int iCmdNum = m_iFillStart + i;
			if (IsCritCommand(iSlot, tStorage.m_iEntIndex, tStorage.m_flMultCritChance, iCmdNum, false))
				tStorage.m_vSkipCommands.push_back(iCmdNum);
		}
	}

	m_iFillStart += n;
}



bool CCritHack::IsCritCommand(int iSlot, int iIndex, float flMultCritChance, const i32 command_number, const bool bCrit, const bool bSafe)
{
	const auto uSeed = MD5_PseudoRandom(command_number) & 0x7FFFFFFF;
	CValve_Random* Random = new CValve_Random();
	Random->SetSeed(DecryptOrEncryptSeed(iSlot, iIndex, uSeed));
	//SDK::RandomSeed( DecryptOrEncryptSeed( iSlot, iIndex, uSeed ) );
	const int iRandom = Random->RandomInt(0, WEAPON_RANDOM_RANGE - 1);//SDK::RandomInt(0, WEAPON_RANDOM_RANGE - 1);
	delete(Random);

	if (bSafe)
	{
		int iLower, iUpper;
		if (iSlot == EWeaponSlot::SLOT_MELEE)
			iLower = 1500, iUpper = 6000;
		else
			iLower = 100, iUpper = 800;
		iLower *= flMultCritChance, iUpper *= flMultCritChance;
		if (bCrit ? iLower >= 0 : iUpper < WEAPON_RANDOM_RANGE)
			return bCrit ? iRandom < iLower : !(iRandom < iUpper);
	}
	const int iRange = m_flCritChance * WEAPON_RANDOM_RANGE;
	return bCrit ? iRandom < iRange : !(iRandom < iRange);
}

u32 CCritHack::DecryptOrEncryptSeed(int iSlot, int iIndex, u32 uSeed)
{
	int iLeft = iSlot == SLOT_MELEE ? 8 : 0;
	unsigned int iMask = iIndex << (iLeft + 8) | I::EngineClient->GetLocalPlayer() << iLeft;
	return iMask ^ uSeed;
}



void CCritHack::GetTotalCrits(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	int iSlot = pWeapon->GetSlot();
	if (!m_mStorage.contains(iSlot))
		return;

	auto& tStorage = m_mStorage[iSlot];
	if (!tStorage.m_bActive)
		return;

	static float flOldBucket = 0.f; static int iOldID = 0, iOldCritChecks = 0, iOldCritSeedRequests = 0;
	const float flBucket = pWeapon->m_flCritTokenBucket(); const int iID = pWeapon->GetWeaponID(), iCritChecks = pWeapon->m_nCritChecks(), iCritSeedRequests = pWeapon->m_nCritSeedRequests();
	const bool bShouldUpdate = tStorage.m_flDamage < 0.f || flOldBucket != flBucket || iOldID != iID || iOldCritChecks != iCritChecks || iOldCritSeedRequests != iCritSeedRequests;
	flOldBucket = flBucket; iOldID = iID, iOldCritChecks = iCritChecks, iOldCritSeedRequests = iCritSeedRequests;
	if (!bShouldUpdate)
		return;

	static auto tf_weapon_criticals_bucket_cap = U::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap->GetFloat();
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot(false);
	if (iSlot != SLOT_MELEE && nProjectilesPerShot > 0)
		nProjectilesPerShot = SDK::AttribHookValue(nProjectilesPerShot, "mult_bullets_per_shot", pWeapon);
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	if (bRapidFire)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	float flMult = iSlot == SLOT_MELEE ? 0.5f : Math::RemapVal(float(iCritSeedRequests + 1) / (iCritChecks + 1), 0.1f, 1.f, 1.f, 3.f);
	float flCost = flDamage * TF_DAMAGE_CRIT_MULTIPLIER;

	int iPotentialCrits = (flBucketCap - flBaseDamage) / (TF_DAMAGE_CRIT_MULTIPLIER * flDamage / (iSlot == SLOT_MELEE ? 2 : 1) - flBaseDamage);
	int iAvailableCrits = 0;
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		int iAttempts = std::min(iPotentialCrits, 1000);
		for (int i = 0; i < iAttempts; i++)
		{
			iTestShots++; iTestCrits++;

			float flTestMult = iSlot == SLOT_MELEE ? 0.5f : Math::RemapVal(float(iTestCrits) / iTestShots, 0.1f, 1.f, 1.f, 3.f);
			flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap) - flCost * flTestMult;
			if (flTestBucket < 0.f)
				break;

			iAvailableCrits++;
		}
	}

	int iNextCrit = 0;
	if (iAvailableCrits != iPotentialCrits)
	{
		int iTestShots = iCritChecks, iTestCrits = iCritSeedRequests;
		float flTestBucket = flBucket;
		float flTickBase = I::GlobalVars->curtime;
		float flLastRapidFireCritCheckTime = pWeapon->m_flLastRapidFireCritCheckTime();
		int iAttempts = std::min(iPotentialCrits, 1000);
		for (int i = 0; i < 1000; i++)
		{
			int iCrits = 0;
			{
				int iTestShots2 = iTestShots, iTestCrits2 = iTestCrits;
				float flTestBucket2 = flTestBucket;
				for (int j = 0; j < iAttempts; j++)
				{
					iTestShots2++; iTestCrits2++;

					float flTestMult = iSlot == SLOT_MELEE ? 0.5f : Math::RemapVal(float(iTestCrits2) / iTestShots2, 0.1f, 1.f, 1.f, 3.f);
					flTestBucket2 = std::min(flTestBucket2 + flBaseDamage, flBucketCap) - flCost * flTestMult;
					if (flTestBucket2 < 0.f)
						break;

					iCrits++;
				}
			}
			if (iAvailableCrits < iCrits)
				break;

			if (!bRapidFire)
				iTestShots++;
			else 
			{
				flTickBase += std::ceilf(flFireRate / TICK_INTERVAL) * TICK_INTERVAL;
				if (flTickBase >= flLastRapidFireCritCheckTime + 1.f || !i && flTestBucket == flBucketCap)
				{
					iTestShots++;
					flLastRapidFireCritCheckTime = flTickBase;
				}
			}

			flTestBucket = std::min(flTestBucket + flBaseDamage, flBucketCap);

			iNextCrit++;
		}
	}

	tStorage.m_flDamage = flBaseDamage;
	tStorage.m_flCost = flCost * flMult;
	tStorage.m_iPotentialCrits = iPotentialCrits;
	tStorage.m_iAvailableCrits = iAvailableCrits;
	tStorage.m_iNextCrit = iNextCrit;
}

void CCritHack::CanFireCritical(CTFPlayer* pLocal, CTFWeaponBase* pWeapon)
{
	m_bCritBanned = false;
	m_flDamageTilFlip = 0;

	if (auto pResource = H::Entities.GetPR())
	{
		m_iResourceDamage = pResource->m_iDamage(pLocal->entindex());
		m_iDesyncDamage = m_iRangedDamage + m_iMeleeDamage - m_iResourceDamage;
	}

	if (pWeapon->GetSlot() == SLOT_MELEE)
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_MELEE * pLocal->GetCritMult();
	else if (pWeapon->IsRapidFire())
	{
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE_RAPID * pLocal->GetCritMult();
		float flNonCritDuration = (TF_DAMAGE_CRIT_DURATION_RAPID / m_flCritChance) - TF_DAMAGE_CRIT_DURATION_RAPID;
		m_flCritChance = 1.f / flNonCritDuration;
	}
	else
		m_flCritChance = TF_DAMAGE_CRIT_CHANCE * pLocal->GetCritMult();
	m_flCritChance = SDK::AttribHookValue(m_flCritChance, "mult_crit_chance", pWeapon);

	const float flNormalizedDamage = m_iCritDamage / TF_DAMAGE_CRIT_MULTIPLIER;
	float flCritChance = m_flCritChance + 0.1f;
	if (m_iRangedDamage && m_iCritDamage)
	{
		const float flObservedCritChance = flNormalizedDamage / (flNormalizedDamage + m_iRangedDamage - m_iCritDamage);
		m_bCritBanned = flObservedCritChance > flCritChance;
	}

	if (m_bCritBanned)
		m_flDamageTilFlip = flNormalizedDamage / flCritChance + flNormalizedDamage * 2 - m_iRangedDamage;
	else
		m_flDamageTilFlip = TF_DAMAGE_CRIT_MULTIPLIER * (flNormalizedDamage - flCritChance * (flNormalizedDamage + m_iRangedDamage - m_iCritDamage)) / (flCritChance - 1);
}

bool CCritHack::WeaponCanCrit(CTFWeaponBase* pWeapon, bool bWeaponOnly)
{
	if (!bWeaponOnly && !pWeapon->AreRandomCritsEnabled() || SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon) <= 0.f)
		return false;

	switch (pWeapon->GetWeaponID())
	{
	case TF_WEAPON_PDA:
	case TF_WEAPON_PDA_ENGINEER_BUILD:
	case TF_WEAPON_PDA_ENGINEER_DESTROY:
	case TF_WEAPON_PDA_SPY:
	case TF_WEAPON_PDA_SPY_BUILD:
	case TF_WEAPON_BUILDER:
	case TF_WEAPON_INVIS:
	case TF_WEAPON_JAR_MILK:
	case TF_WEAPON_LUNCHBOX:
	case TF_WEAPON_BUFF_ITEM:
	case TF_WEAPON_FLAME_BALL:
	case TF_WEAPON_ROCKETPACK:
	case TF_WEAPON_JAR_GAS:
	case TF_WEAPON_LASER_POINTER:
	case TF_WEAPON_MEDIGUN:
	case TF_WEAPON_SNIPERRIFLE:
	case TF_WEAPON_SNIPERRIFLE_DECAP:
	case TF_WEAPON_SNIPERRIFLE_CLASSIC:
	case TF_WEAPON_COMPOUND_BOW:
	case TF_WEAPON_JAR:
	case TF_WEAPON_KNIFE:
	case TF_WEAPON_PASSTIME_GUN:
		return false;
	}

	return true;
}



void CCritHack::ResetWeapons(CTFPlayer* pLocal)
{
	for (auto& [iSlot, tStorage] : m_mStorage)
		tStorage.m_bActive = false;

	for (int i = 0; i < MAX_WEAPONS; i++)
	{
		auto pWeapon = pLocal->GetWeaponFromSlot(i);
		if (!pWeapon || !WeaponCanCrit(pWeapon, true))
			continue;

		int iSlot = pWeapon->GetSlot();
		auto& tStorage = m_mStorage[iSlot];
		tStorage.m_bActive = true;

		int iEntIndex = pWeapon->entindex();
		int iDefIndex = pWeapon->m_iItemDefinitionIndex();
		float flMultCritChance = SDK::AttribHookValue(1.f, "mult_crit_chance", pWeapon);

		if (tStorage.m_iEntIndex != iEntIndex || tStorage.m_iDefIndex != iDefIndex || tStorage.m_flMultCritChance != flMultCritChance)
			tStorage = { iEntIndex, iDefIndex, flMultCritChance };
	}
}

void CCritHack::Reset()
{
	m_mStorage.clear();

	m_iFillStart = 0;
	
	m_iCritDamage = 0;
	m_iRangedDamage = 0;
	m_iMeleeDamage = 0;

	m_bCritBanned = false;
	m_flDamageTilFlip = 0;
	m_flCritChance = 0.f;

	m_mHealthHistory.clear();
}



float CCritHack::GetCost( CTFWeaponBase* pWeapon )
{
	static auto tf_weapon_criticals_bucket_cap = U::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
	const float flBucketCap = tf_weapon_criticals_bucket_cap->GetFloat();
	bool bRapidFire = pWeapon->IsRapidFire();
	float flFireRate = pWeapon->GetFireRate();

	float flDamage = pWeapon->GetDamage();
	int nProjectilesPerShot = pWeapon->GetBulletsPerShot(false);
	if (pWeapon->GetSlot() != SLOT_MELEE && nProjectilesPerShot > 0)
		nProjectilesPerShot = SDK::AttribHookValue(nProjectilesPerShot, "mult_bullets_per_shot", pWeapon);
	else
		nProjectilesPerShot = 1;
	float flBaseDamage = flDamage *= nProjectilesPerShot;
	if (bRapidFire)
	{
		flDamage *= TF_DAMAGE_CRIT_DURATION_RAPID / flFireRate;
		if (flDamage * TF_DAMAGE_CRIT_MULTIPLIER > flBucketCap)
			flDamage = flBucketCap / TF_DAMAGE_CRIT_MULTIPLIER;
	}

	return flDamage * TF_DAMAGE_CRIT_MULTIPLIER;
}

void CCritHack::Run(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pWeapon || !pLocal->IsAlive() || !I::EngineClient->IsInGame())
		return;

	int iSlot = pWeapon->GetSlot();
	ResetWeapons(pLocal);
	Fill(pCmd, 25);
	GetTotalCrits(pLocal, pWeapon);
	CanFireCritical(pLocal, pWeapon);
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !m_mStorage.contains(iSlot))
		return;

	auto& tStorage = m_mStorage[iSlot];
	if (!tStorage.m_bActive)
		return;

	if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && pCmd->buttons & IN_ATTACK)
		pCmd->buttons &= ~IN_ATTACK2;

	bool bAttacking = G::Attacking /*== 1*/ || F::Ticks.m_bDoubletap || F::Ticks.m_bSpeedhack;
	if (G::PrimaryWeaponType == EWeaponType::MELEE)
	{
		bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK;
		if (!bAttacking && pWeapon->GetWeaponID() == TF_WEAPON_FISTS)
			bAttacking = G::CanPrimaryAttack && pCmd->buttons & IN_ATTACK2;
	}
	else if (pWeapon->GetWeaponID() == TF_WEAPON_MINIGUN && !(G::LastUserCmd->buttons & IN_ATTACK))
		bAttacking = false;
	else if (!bAttacking)
	{
		switch (pWeapon->GetWeaponID())
		{
		case TF_WEAPON_ROCKETLAUNCHER:
		case TF_WEAPON_ROCKETLAUNCHER_DIRECTHIT:
			if (pWeapon->IsInReload() && G::CanPrimaryAttack && SDK::AttribHookValue(0, "can_overload", pWeapon))
			{
				int iClip1 = pWeapon->m_iClip1();
				if (pWeapon->m_bRemoveable() && iClip1 > 0)
					bAttacking = true;
				else if (iClip1 >= pWeapon->GetMaxClip1() || iClip1 > 0 && pLocal->GetAmmoCount(pWeapon->m_iPrimaryAmmoType()) == 0)
					bAttacking = true;
			}
		}
	}

	bool bRapidFire = pWeapon->IsRapidFire();
	bool bStreamWait = bRapidFire && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f;

	int iClosestCrit = !tStorage.m_vCritCommands.empty() ? tStorage.m_vCritCommands.front() : 0;
	int iClosestSkip = !tStorage.m_vSkipCommands.empty() ? tStorage.m_vSkipCommands.front() : 0;

	if (bAttacking)
	{
		const bool bCanCrit = tStorage.m_iAvailableCrits > 0 && (!m_bCritBanned || iSlot == SLOT_MELEE) && !bStreamWait;
		const bool bPressed = Vars::CritHack::ForceCrits.Value || iSlot == SLOT_MELEE && Vars::CritHack::AlwaysMeleeCrit.Value && (Vars::Aimbot::General::AutoShoot.Value ? pCmd->buttons & IN_ATTACK && !(G::OriginalMove.m_iButtons & IN_ATTACK) : Vars::Aimbot::General::AimType.Value);
		if (!Vars::Misc::Game::AntiCheatCompatibility.Value)
		{
			if (bCanCrit && bPressed && iClosestCrit)
				pCmd->command_number = iClosestCrit;
			else if (Vars::CritHack::AvoidRandomCrits.Value && iClosestSkip)
				pCmd->command_number = iClosestSkip;
		}
		else if (Vars::Misc::Game::AntiCheatCritHack.Value)
		{
			bool bShouldAvoid = false;

			bool bCritCommand = IsCritCommand(iSlot, tStorage.m_iEntIndex, tStorage.m_flMultCritChance, pCmd->command_number, true, false);
			if (bCanCrit && bPressed && !bCritCommand
				|| Vars::CritHack::AvoidRandomCrits.Value && !bPressed && bCritCommand)
				bShouldAvoid = true;

			if (bShouldAvoid)
			{
				pCmd->buttons &= ~IN_ATTACK;
				pCmd->viewangles = G::OriginalMove.m_vView;
				G::PSilentAngles = false;
			}
		}
	}

	m_iWishRandomSeed = MD5_PseudoRandom(pCmd->command_number) & std::numeric_limits<int>::max();

	if (pCmd->command_number == iClosestCrit)
		tStorage.m_vCritCommands.pop_front();
	else if (pCmd->command_number == iClosestSkip)
		tStorage.m_vSkipCommands.pop_front();
}

int CCritHack::PredictCmdNum(CTFPlayer* pLocal, CTFWeaponBase* pWeapon, CUserCmd* pCmd)
{
	if (!pLocal || !pWeapon || !pLocal->IsAlive() || !I::EngineClient->IsInGame())
		return pCmd->command_number;

	int iSlot = pWeapon->GetSlot();
	if (pLocal->IsCritBoosted() || pWeapon->m_flCritTime() > I::GlobalVars->curtime || !m_mStorage.contains(iSlot))
		return pCmd->command_number;

	auto& tStorage = m_mStorage[iSlot];
	bool bRapidFire = pWeapon->IsRapidFire();
	bool bStreamWait = bRapidFire && I::GlobalVars->curtime < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f;

	int closestCrit = !tStorage.m_vCritCommands.empty() ? tStorage.m_vCritCommands.front() : 0;
	int closestSkip = !tStorage.m_vSkipCommands.empty() ? tStorage.m_vSkipCommands.front() : 0;

	const bool bCanCrit = tStorage.m_iAvailableCrits > 0 && (!m_bCritBanned || pWeapon->GetSlot() == SLOT_MELEE) && !bStreamWait;
	const bool bPressed = Vars::CritHack::ForceCrits.Value || pWeapon->GetSlot() == SLOT_MELEE && Vars::CritHack::AlwaysMeleeCrit.Value && (Vars::Aimbot::General::AutoShoot.Value ? pCmd->buttons & IN_ATTACK && !(G::OriginalMove.m_iButtons & IN_ATTACK) : Vars::Aimbot::General::AimType.Value);
	if (bCanCrit && bPressed && closestCrit)
		return closestCrit;
	else if (Vars::CritHack::AvoidRandomCrits.Value && closestSkip)
		return closestSkip;

	return pCmd->command_number;
}

void CCritHack::Event(IGameEvent* pEvent, uint32_t uHash, CTFPlayer* pLocal)
{
	switch (uHash)
	{
	case FNV1A::Hash32Const("player_hurt"):
	{
		if (!pLocal)
			break;

		int iVictim = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("userid"));
		int iAttacker = I::EngineClient->GetPlayerForUserID(pEvent->GetInt("attacker"));
		bool bCrit = pEvent->GetBool("crit") || pEvent->GetBool("minicrit");
		int iDamage = pEvent->GetInt("damageamount");
		int iHealth = pEvent->GetInt("health");
		int iWeaponID = pEvent->GetInt("weaponid");

		if (m_mHealthHistory.contains(iVictim))
		{
			auto& tHistory = m_mHealthHistory[iVictim];
			auto pVictim = I::ClientEntityList->GetClientEntity(iVictim)->As<CTFPlayer>();

			if (!iHealth)
				iDamage = std::min(iDamage, tHistory.m_iNewHealth);
			else if (pVictim && (pVictim->m_bFeignDeathReady() || pVictim->InCond(TF_COND_FEIGN_DEATH))) // damage number is spoofed upon sending, correct it
			{
				int iOldHealth = (tHistory.m_mHistory.contains(iHealth) ? tHistory.m_mHistory[iHealth].m_iOldHealth : tHistory.m_iNewHealth) % 32768;
				if (iHealth > iOldHealth)
				{
					for (auto& [_, tOldHealth] : tHistory.m_mHistory)
					{
						int iOldHealth2 = tOldHealth.m_iOldHealth % 32768;
						if (iOldHealth2 > iHealth)
							iOldHealth = iHealth > iOldHealth ? iOldHealth2 : std::min(iOldHealth, iOldHealth2);
					}
				}
				iDamage = std::clamp(iOldHealth - iHealth, 0, iDamage);
			}
		}
		if (iHealth)
			StoreHealthHistory(iVictim, iHealth);

		if (iVictim == iAttacker || iAttacker != I::EngineClient->GetLocalPlayer())
			break;

		if (auto pGameRules = I::TFGameRules())
		{
			auto pMatchDesc = pGameRules->GetMatchGroupDescription();
			if (pMatchDesc && pGameRules->m_iRoundState() != GR_STATE_RND_RUNNING)
			{
				switch (pMatchDesc->m_eMatchType)
				{
				case MATCH_TYPE_COMPETITIVE:
				case MATCH_TYPE_CASUAL:
					return;
				}
			}
		}

		CTFWeaponBase* pWeapon = nullptr;
		for (int i = 0; i < MAX_WEAPONS; i++)
		{
			auto pWeapon2 = pLocal->GetWeaponFromSlot(i);
			if (!pWeapon2 || pWeapon2->GetWeaponID() != iWeaponID)
				continue;

			pWeapon = pWeapon2;
			break;
		}

		if (!pWeapon || pWeapon->GetSlot() != SLOT_MELEE)
		{
			m_iRangedDamage += iDamage;
			if (bCrit && !pLocal->IsCritBoosted())
				m_iCritDamage += iDamage;
		}
		else
			m_iMeleeDamage += iDamage;

		break;
	}
	case FNV1A::Hash32Const("scorestats_accumulated_update"):
	case FNV1A::Hash32Const("mvm_reset_stats"):
		m_iRangedDamage = m_iCritDamage = m_iMeleeDamage = 0;
		break;
	case FNV1A::Hash32Const("client_beginconnect"):
	case FNV1A::Hash32Const("client_disconnect"):
	case FNV1A::Hash32Const("game_newmap"):
		Reset();
	}
}

void CCritHack::Store()
{
	for (auto& pEntity : H::Entities.GetGroup(EGroupType::PLAYERS_ALL))
	{
		auto pPlayer = pEntity->As<CTFPlayer>();
		if (pPlayer->IsAlive() && !pPlayer->IsAGhost())
			StoreHealthHistory(pPlayer->entindex(), pPlayer->m_iHealth());
	}
}

void CCritHack::StoreHealthHistory(int iIndex, int iHealth, bool bDamage)
{
	bool bContains = m_mHealthHistory.contains(iIndex);
	auto& tHistory = m_mHealthHistory[iIndex];

	if (!bContains)
		tHistory = { iHealth, iHealth };
	else if (iHealth != tHistory.m_iNewHealth)
	{
		tHistory.m_iOldHealth = std::max(bDamage && tHistory.m_mHistory.contains(iHealth % 32768) ? tHistory.m_mHistory[iHealth % 32768].m_iOldHealth : tHistory.m_iNewHealth, iHealth);
		tHistory.m_iNewHealth = iHealth;
	}

	tHistory.m_mHistory[iHealth % 32768] = { tHistory.m_iOldHealth, float(SDK::PlatFloatTime()) };
	while (tHistory.m_mHistory.size() > 3)
	{
		int iIndex2; float flMin = std::numeric_limits<float>::max();
		for (auto& [i, tStorage] : tHistory.m_mHistory)
		{
			if (tStorage.m_flTime < flMin)
				flMin = tStorage.m_flTime, iIndex2 = i;
		}
		tHistory.m_mHistory.erase(iIndex2);
	}
}

#ifdef SERVER_CRIT_DATA
MAKE_SIGNATURE(CTFGameStats_FindPlayerStats, "server.dll", "4C 8B C1 48 85 D2 75", 0x0);
MAKE_SIGNATURE(GetServerAnimating, "server.dll", "48 83 EC ? 8B D1 85 C9 7E ? 48 8B 05", 0x0);

static void* pCTFGameStats = nullptr;
MAKE_HOOK(CTFGameStats_FindPlayerStats, S::CTFGameStats_FindPlayerStats(), void*,
	void* rcx, CBasePlayer* pPlayer)
{
	pCTFGameStats = rcx;
	return CALL_ORIGINAL(rcx, pPlayer);
}
#endif

void CCritHack::Draw(CTFPlayer* pLocal)
{
	static auto tf_weapon_criticals = U::ConVars.FindVar("tf_weapon_criticals");
	if (!(Vars::Menu::Indicators.Value & Vars::Menu::IndicatorsEnum::CritHack) || !I::EngineClient->IsInGame() || !tf_weapon_criticals->GetBool())
		return;

	auto pWeapon = H::Entities.GetWeapon();
	if (!pWeapon || !pLocal->IsAlive() || pLocal->IsAGhost())
		return;

	int x = Vars::Menu::CritsDisplay.Value.x;
	int y = Vars::Menu::CritsDisplay.Value.y;

	const auto& fFont = H::Fonts.GetFont(FONT_INDICATORS);

	// Fixed box dimensions
	int boxWidth = 180;
	int boxHeight = 29;
	int barHeight = 3;
	int textBoxHeight = boxHeight - barHeight;

	// Adjust x position to center the box
	x -= boxWidth / 2;

	// Draw black transparent background
	Color_t bgColor = { 0, 0, 0, 180 };
	H::Draw.GradientRect(x, y, boxWidth, textBoxHeight, bgColor, bgColor, true);

	// Draw progress bar background
	H::Draw.GradientRect(x, y + textBoxHeight, boxWidth, barHeight, bgColor, bgColor, true);

	// Calculate and draw the progress bar with smooth interpolation
	static float currentProgress = 0.0f;
	float targetProgress = 0.0f;

	std::string leftText, rightText;
	Color_t leftColor = Vars::Menu::Theme::Active.Value;
	Color_t rightColor = Vars::Menu::Theme::Active.Value;
	Color_t barColor = { 0, 255, 100, 255 }; // Default green

	if (!WeaponCanCrit(pWeapon))
	{
		leftText = "Cannot crit";
		rightText = "DISABLED";
		rightColor = { 200, 40, 40, 255 }; // Dark red
		barColor = { 200, 40, 40, 255 };
		targetProgress = 1.0f;
	}
	else
	{
		const auto iSlot = pWeapon->GetSlot();
		const auto bRapidFire = pWeapon->IsRapidFire();
		
		if (!m_mStorage.contains(iSlot) || pWeapon->GetWeaponID() == TF_WEAPON_PASSTIME_GUN)
			return;
			
		auto& tStorage = m_mStorage[iSlot];
		if (!tStorage.m_bActive)
			return;

		if (tStorage.m_flDamage > 0)
		{
			if (pLocal->IsCritBoosted())
			{
				leftText = "Crit Boosted";
				rightText = "READY";
				rightColor = { 100, 255, 255, 255 };
				barColor = { 100, 255, 255, 255 };
				targetProgress = 1.0f;
			}
			else if (pWeapon->m_flCritTime() > I::GlobalVars->curtime)
			{
				const float flTime = pWeapon->m_flCritTime() - I::GlobalVars->curtime;
				leftText = std::format("Crits: {} / {}", std::max(0, tStorage.m_iAvailableCrits), tStorage.m_iPotentialCrits);
				rightText = "STREAMING";
				rightColor = { 100, 255, 255, 255 };
				barColor = { 100, 255, 255, 255 };
				targetProgress = flTime / TF_DAMAGE_CRIT_DURATION_RAPID;
			}
			else if (!m_bCritBanned || iSlot == SLOT_MELEE)
			{
				leftText = std::format("Crits: {} / {}", std::max(0, tStorage.m_iAvailableCrits), tStorage.m_iPotentialCrits);
				if (bRapidFire && TICKS_TO_TIME(pLocal->m_nTickBase()) < pWeapon->m_flLastRapidFireCritCheckTime() + 1.f)
				{
					const float flTime = pWeapon->m_flLastRapidFireCritCheckTime() + 1.f - TICKS_TO_TIME(pLocal->m_nTickBase());
					if (flTime > 0.0001f)
					{
						rightText = std::format("WAIT {:.2f}s", flTime);
						rightColor = tStorage.m_iAvailableCrits > 0 ? Color_t{ 40, 200, 40, 255 } : Color_t{ 200, 40, 40, 255 };
						barColor = { 255, 150, 0, 255 }; // Orange for waiting
					}
					else
					{
						rightText = "STREAMING";
						rightColor = { 100, 255, 255, 255 };
						barColor = { 100, 255, 255, 255 };
					}
					targetProgress = flTime;
				}
				else if (tStorage.m_iAvailableCrits >= tStorage.m_iPotentialCrits)
				{
					rightText = "READY";
					rightColor = { 40, 200, 40, 255 }; // Dark green
					targetProgress = 1.0f;
				}
				else
				{
					float currentBucket = pWeapon->m_flCritTokenBucket();
					int damageNeeded = static_cast<int>(std::ceil(tStorage.m_flCost - currentBucket));
					rightText = std::format("DMG: {}", std::max(0, damageNeeded));
					rightColor = tStorage.m_iAvailableCrits > 0 ? Color_t{ 40, 200, 40, 255 } : Color_t{ 200, 40, 40, 255 };
					
					static auto bucketCap = U::ConVars.FindVar("tf_weapon_criticals_bucket_cap");
					targetProgress = currentBucket / bucketCap->GetFloat();
				}
			}
			else
			{
				leftText = std::format("DMG: {}", static_cast<int>(ceilf(m_flDamageTilFlip)));
				rightText = "BANNED";
				rightColor = { 200, 40, 40, 255 }; // Dark red
				barColor = { 200, 40, 40, 255 };
				targetProgress = 0.2f; // Low progress when banned
			}
		}
		else
		{
			leftText = "Calculating";
			rightText = "";
		}
	}
	currentProgress = std::lerp(currentProgress, targetProgress, I::GlobalVars->frametime * 10.0f);

	int barWidth = static_cast<int>(boxWidth * currentProgress);
	if (barWidth > 0)
		H::Draw.GradientRect(x, y + textBoxHeight, barWidth, barHeight, barColor, barColor, true);

	H::Draw.String(fFont, x + 5, y + (textBoxHeight / 2), leftColor, ALIGN_LEFT, leftText.c_str());
	if (!rightText.empty())
		H::Draw.String(fFont, x + boxWidth - 5, y + (textBoxHeight / 2), rightColor, ALIGN_RIGHT, rightText.c_str());
	
	if (Vars::Misc::Game::AntiCheatCompatibility.Value)
		H::Draw.String(fFont, x + boxWidth / 2, y - fFont.m_nTall - 2, Vars::Colors::IndicatorTextBad.Value, ALIGN_CENTER, "Anti-cheat compatibility");
}