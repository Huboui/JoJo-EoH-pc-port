// shadPS4 Steamworks HLE Integration for JoJo's Bizarre Adventure: Eyes of Heaven
#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "steam/isteamnetworkingmessages.h"
#include "steam/steam_api.h"

namespace Libraries::Network {

class SteamworksHLE {
public:
    static SteamworksHLE& Instance();

    bool Initialize();
    void Shutdown();

    bool IsActive() const { return m_initialized; }
    uint64_t GetMySteamId() const;
    std::string GetMyPersonaName() const;

    // PSN / Online ID mapping
    void FillOnlineId(char* outOnlineId, size_t maxLen);
    void FillNpId(uint8_t* outHandleData, size_t len);

    // Matching2 / Lobby mapping
    bool CreateLobby(int maxMembers, bool openInviteOverlay = true);
    void SetLobbyData(const std::string& key, const std::string& value);
    std::string GetLobbyData(const std::string& key);
    void SearchLobbies();
    bool JoinLobby(uint64_t steamIdLobby);
    void LeaveLobby();
    uint64_t GetCurrentLobbyId() const { return m_currentLobbyId; }
    void OpenInviteOverlay();

    // Virtual Peer IP Mapping (10.13.37.0/24 subnet)
    bool IsVirtualPeerIp(uint32_t ip) const;
    bool HasActivePeer() const;
    uint32_t GetLocalVirtualIp() const { return m_localVirtualIp; }
    uint32_t GetActivePeerIp() const;
    uint32_t RegisterPeer(uint64_t steamId);
    uint64_t GetSteamIdFromIp(uint32_t ip) const;
    uint32_t GetIpFromSteamId(uint64_t steamId) const;

    // Routing for sceNetSendto and sceNetRecvfrom
    int SendTo(const void* buf, size_t len, uint32_t destIp, uint16_t destPort);
    int RecvFrom(void* buf, size_t len, uint32_t* srcIp, uint16_t* srcPort);

private:
    SteamworksHLE();
    ~SteamworksHLE();

    SteamworksHLE(const SteamworksHLE&) = delete;
    SteamworksHLE& operator=(const SteamworksHLE&) = delete;

    void CallbackThread();

    // Call results
    CCallResult<SteamworksHLE, LobbyCreated_t> m_lobbyCreatedCallResult;
    CCallResult<SteamworksHLE, LobbyEnter_t> m_lobbyEnterCallResult;
    CCallResult<SteamworksHLE, LobbyMatchList_t> m_lobbyMatchListCallResult;

    void OnLobbyCreated(LobbyCreated_t* pResult, bool bIOFailure);
    void OnLobbyEntered(LobbyEnter_t* pResult, bool bIOFailure);
    void OnLobbyMatchList(LobbyMatchList_t* pResult, bool bIOFailure);

    // Callbacks
    STEAM_CALLBACK(SteamworksHLE, OnGameLobbyJoinRequested, GameLobbyJoinRequested_t);
    STEAM_CALLBACK(SteamworksHLE, OnLobbyChatUpdate, LobbyChatUpdate_t);
    STEAM_CALLBACK(SteamworksHLE, OnSessionRequest, SteamNetworkingMessagesSessionRequest_t);

    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_running{false};
    std::thread m_callbackThread;

    uint64_t m_currentLobbyId{0};
    bool m_openOverlayOnCreate{true};

    uint32_t m_localVirtualIp{0};
    uint32_t m_nextPeerIpIndex{2};

    mutable std::mutex m_peerMutex;
    std::unordered_map<uint32_t, uint64_t> m_ipToSteamId;
    std::unordered_map<uint64_t, uint32_t> m_steamIdToIp;
    uint64_t m_lastPeerSteamId{0};
};

} // namespace Libraries::Network
