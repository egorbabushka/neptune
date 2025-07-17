#include "AutoCommunityServer.h"
#include <fstream>
#include <filesystem>

HSteamPipe steamPipe;
HSteamUser user;
ISteamMatchmakingServers* matchmaking;
static bool bAlreadySearching = false;
std::vector<std::string> blacklist = {};

std::string GetGameDirectory()
{
	static std::string sGameDir;
	if (!sGameDir.empty())
		return sGameDir;

	char gamePath[MAX_PATH] = { 0 };
	if (!GetModuleFileNameA(GetModuleHandleA("tf_win64.exe"), gamePath, MAX_PATH))
	{
		return sGameDir; // empty
	}

	std::string gameDir = gamePath;
	size_t lastSlash = gameDir.find_last_of("\\/");
	if (lastSlash != std::string::npos)
		gameDir = gameDir.substr(0, lastSlash);

	sGameDir = gameDir;
	return sGameDir;
}

std::vector<std::string> GetServerBlacklist()
{
	std::vector<std::string> blacklist = {};
	const char* fileName = "blacklist.txt";

	const std::string gameDir = GetGameDirectory();
	std::vector<std::string> pathsToTry = {
		fileName,
		gameDir + "\\amalgam\\" + fileName
	};

	bool fileLoaded = false;

	for (const auto& path : pathsToTry)
	{
		try
		{
			std::ifstream file(path);
			if (file.is_open())
			{
				std::string line;
				while (std::getline(file, line))
				{
					if (!line.empty())
						blacklist.push_back(line);
				}
				file.close();

				SDK::Output(std::format("Loaded {} lines from {}", blacklist.size(), path).c_str());
				fileLoaded = true;
				break;
			}
		}
		catch (...)
		{
			continue;
		}
	}
	return blacklist;
}

std::vector<std::string> GetAllSupportedMaps()
{
	namespace fs = std::filesystem;

	std::vector<std::string> mapList;
	const fs::path mapsPath = "tf/maps";

	if (!fs::exists(mapsPath) || !fs::is_directory(mapsPath)) {
		// You can log this or handle error
		return mapList;
	}

	for (const auto& entry : fs::directory_iterator(mapsPath)) {
		if (!entry.is_regular_file())
			continue;

		const fs::path& filePath = entry.path();
		if (filePath.extension() == ".nav") {
			std::string mapName = filePath.stem().string(); // removes ".nav"
			mapList.push_back(mapName);
		}
	}

	return mapList;
}

bool IsOfficialMap(const char* mapName)
{
	static std::unordered_set<std::string> supportedMaps = []() {
		auto list = GetAllSupportedMaps();
		return std::unordered_set<std::string>(list.begin(), list.end());
		}();

	return supportedMaps.contains(mapName);
}

bool IsServerBlacklisted(const char* ip)
{
	static std::unordered_set<std::string> serverBlacklist = []() {
		auto list = blacklist;
		return std::unordered_set<std::string>(list.begin(), list.end());
		}();

	return serverBlacklist.contains(ip);
}

class CTF2ServerListResponse : public ISteamMatchmakingServerListResponse
{
public:
	std::vector<std::string> serverIPs = {};
	void ServerResponded(HServerListRequest hRequest, int iServer) override
	{
		gameserveritem_t* pServer = matchmaking->GetServerDetails(hRequest, iServer);
		if (!pServer || !pServer->GetName())
			return;
		const char* name = pServer->GetName();
		const char* suffix = "'s Server";
		size_t nameLen = strlen(name);
		size_t suffixLen = strlen(suffix);
		if (nameLen >= suffixLen &&
			strcmp(name + nameLen - suffixLen, suffix) == 0 && !(pServer->m_bPassword))
		{
			const char* ip = pServer->m_NetAdr.GetConnectionAddressString();
			SDK::Output(std::format("connect {}, {}, {}, {}/{}", ip, name, pServer->m_szMap, pServer->m_nPlayers, pServer->m_nMaxPlayers).c_str());
			if (!IsOfficialMap(pServer->m_szMap) || IsServerBlacklisted(ip) || pServer->m_nPlayers == 0 || (pServer->m_nPlayers) == (pServer->m_nMaxPlayers)) return;
			serverIPs.emplace_back(ip);
		}
	}

	void ServerFailedToRespond(HServerListRequest hRequest, int iServer) override {}
	void RefreshComplete(HServerListRequest hRequest, EMatchMakingServerResponse response) override {
		bAlreadySearching = false;
		if (serverIPs.empty()) {
			SDK::Output("No matching servers found.");
			return;
		}
		CValve_Random* Random = new CValve_Random();
		Random->SetSeed(I::EngineClient->Time());
		;		const std::string& chosenIP = serverIPs[Random->RandomInt(0, serverIPs.size() - 1)];

		SDK::Output(std::format("Connecting to random server: {}", chosenIP).c_str());
		I::EngineClient->ClientCmd_Unrestricted(std::format("connect {}", chosenIP).c_str());
	}
};

void CAutoCommunityServer::Run() 
{
	static float flNextSearch = 0.0f;
	float flCurrentTime = I::EngineClient->Time();
	if (flCurrentTime < flNextSearch) return;
	flNextSearch += 90.0f;
	if (!Vars::Misc::Automation::AutoCommunityServer.Value) return;
	SDK::Output("duh");
	blacklist = GetServerBlacklist();
	if (bAlreadySearching) 
	{ 
		SDK::Output("Already searching for server!"); 
		return;
	}
	if (I::EngineClient->IsDrawingLoadingImage()) return;
	if (I::EngineClient->IsInGame())
	{
		int nPlayerCount = 0;

		for (int i = 1; i <= I::EngineClient->GetMaxClients(); i++)
		{
			PlayerInfo_t pi{};
			if (I::EngineClient->GetPlayerInfo(i, &pi) && pi.userID != -1)
			{
				nPlayerCount++;
			}
		}
		if (nPlayerCount > 1)
			return;
		// I::TFGCClientSystem->AbandonCurrentMatch();
	}
	SDK::Output("Trying to find server");
	steamPipe = I::SteamClient->CreateSteamPipe();
	if (!steamPipe) {
		SDK::Output("Failed to create steam pipe");
		return;
	}
	user = I::SteamClient->ConnectToGlobalUser(steamPipe);
	if (!user) {
		SDK::Output("Failed to get steam user");
		return;
	}

	matchmaking = I::SteamClient->GetISteamMatchmakingServers(user, steamPipe, STEAMMATCHMAKINGSERVERS_INTERFACE_VERSION);
	if (!matchmaking)
	{
		SDK::Output("Matchmaking is null");
		return;
	}
	CTF2ServerListResponse* s_ResponseHandler = new CTF2ServerListResponse();

	MatchMakingKeyValuePair_t kvGameDir = { "gamedir", "tf" };
	MatchMakingKeyValuePair_t* filters[] = {
		&kvGameDir,
		nullptr
	};
	// Request internet server list (community servers)
	bAlreadySearching = true;
	matchmaking->RequestInternetServerList(
		I::SteamUtils->GetAppID(),
		filters,
		1,
		s_ResponseHandler
	);
}
