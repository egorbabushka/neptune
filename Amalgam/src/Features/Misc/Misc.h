#pragma once
#include "../../SDK/SDK.h"
#include <set>

class bf_read;

class CMisc
{
	void CrashServer();
	void AutoJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoJumpbug(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoStrafe(CTFPlayer* pLocal, CUserCmd* pCmd);
	void MovementLock(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakJump(CTFPlayer* pLocal, CUserCmd* pCmd);
	void BreakShootSound(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AntiAFK(CTFPlayer* pLocal, CUserCmd* pCmd);
	void InstantRespawnMVM(CTFPlayer* pLocal);
	void NoiseSpam(CTFPlayer* pLocal);
	void VoiceCommandSpam(CTFPlayer* pLocal);
	void MicSpam(CTFPlayer* pLocal);
	void RandomVotekick(CTFPlayer* pLocal);
	void CallVoteSpam(CTFPlayer* pLocal);
	void ChatSpam(CTFPlayer* pLocal);
	void AchievementSpam(CTFPlayer* pLocal);
	void KillSay(int victim);
	std::string ProcessTextReplacements(std::string text);
	void CheatsBypass();
	void WeaponSway();
	void BotNetworking();
	void GrabSteamIDs();
	void AutoReport();
	void TauntKartControl(CTFPlayer* pLocal, CUserCmd* pCmd);
	void FastMovement(CTFPlayer* pLocal, CUserCmd* pCmd);
	void AutoPeek(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);
	void EdgeJump(CTFPlayer* pLocal, CUserCmd* pCmd, bool bPost = false);
	void StealIdentity(CTFPlayer* pLocal);
	
	bool m_bPeekPlaced = false;
	Vec3 m_vPeekReturnPos = {};

	//bool bSteamCleared = false;
	std::vector<std::string> m_vChatSpamLines;
	std::vector<std::string> m_vKillSayLines;
	std::vector<std::string> m_vAutoReplyLines;
	Timer m_tChatSpamTimer;
	Timer m_tVoiceCommandTimer;
	Timer m_tCallVoteSpamTimer;
	Timer m_tMicSpamTimer;
	Timer m_tAchievementSpamTimer;
	int m_iTokenBucket = 5;
	float m_flLastTokenTime = 0.0f;
	int m_iCurrentChatSpamIndex = 0;
	int m_iLastKilledPlayer = 0;
	std::string m_sLastKilledPlayerName;
	enum class AchievementSpamState
	{
		IDLE,
		CLEARING,
		WAITING,
		AWARDING
	};
	AchievementSpamState m_eAchievementSpamState = AchievementSpamState::IDLE;
	Timer m_tAchievementDelayTimer;
	int m_iAchievementSpamID = -1;
	const char* m_sAchievementSpamName = nullptr;

	// Chat Relay variables
	bool m_bChatRelayInitialized = false;
	std::string m_sChatRelayPath;
	std::string m_sServerIdentifier;
	std::set<std::string> m_setRecentMessageHashes;
	Timer m_tCleanupTimer;
	int m_iLocalPlayerIndex = -1;

	Timer m_tStealIdentityTimer;
	int m_nStolenAvatar = -1;

public:
	void RunPre(CTFPlayer* pLocal, CUserCmd* pCmd);
	void RunPost(CTFPlayer* pLocal, CUserCmd* pCmd, bool pSendPacket);
	void Event(IGameEvent* pEvent, uint32_t uNameHash);
	int AntiBackstab(CTFPlayer* pLocal, CUserCmd* pCmd, bool bSendPacket);
	void PingReducer();
	void UnlockAchievements();
	void LockAchievements();
	void VotekickResponse(bf_read& msgData);
	bool SteamRPC();
	void AutoReply(int speaker, const char* text);
	std::unordered_map<std::string, std::vector<std::string>> m_mAutoReplyTriggers;
	std::unordered_map<std::string, std::vector<std::string>> m_mVotekickResponses;
	void LoadVotekickConfig();
	void LoadAutoReplyConfig();
	void AutoMvmReadyUp();
	
	void ChatRelay(int speaker, const char* text, bool teamChat = false);
	void InitializeChatRelay();
	std::string GetAppDataPath();
	std::string GetChatRelayPath();
	bool IsPrimaryBot();
	std::string GetServerIdentifier();
	bool ShouldLogMessage(const std::string& messageHash);
	void CleanupOldMessages();
	int m_iWishCmdrate = -1;
	bool m_bAntiAFK = false;
	Timer m_tRandomVotekickTimer;
	std::string m_sVoteInitiatorName; 
	std::string m_sVoteTargetName;

private:
	int m_iVoteTarget = -1;
	std::string m_sVoteType;
	std::string GetGameDirectory();
	bool LoadLines(const std::string& category, const std::string& fileName,
		const std::vector<std::string>& defaultLines, std::vector<std::string>& outLines);
};
ADD_FEATURE(CMisc, Misc)
