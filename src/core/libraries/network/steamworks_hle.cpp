// shadPS4 Steamworks HLE Integration for JoJo's Bizarre Adventure: Eyes of Heaven
#include "core/libraries/network/steamworks_hle.h"
#include "common/logging/log.h"

#include <chrono>

namespace Libraries::Network {

SteamworksHLE& SteamworksHLE::Instance() {
    static SteamworksHLE instance;
    return instance;
}

SteamworksHLE::SteamworksHLE() {
    // 10.13.37.1 in little-endian network byte order
    m_localVirtualIp = (10) | (0 << 8) | (0 << 16) | (1 << 24); // 10.0.0.1
}

SteamworksHLE::~SteamworksHLE() {
    Shutdown();
}

bool SteamworksHLE::Initialize() {
    if (m_initialized) {
        return true;
    }

    LOG_INFO(Lib_Net, "Initializing Steamworks SDK HLE (AppID: 480 / Spacewar)...");

    if (!SteamAPI_Init()) {
        LOG_WARNING(Lib_Net, "SteamAPI_Init() failed! Is Steam running? Falling back to local offline mode.");
        return false;
    }

    m_initialized = true;
    m_running = true;

    // Warm up Steam relay network
    if (SteamNetworkingUtils()) {
        SteamNetworkingUtils()->InitRelayNetworkAccess();
    }

    uint64_t myId = GetMySteamId();
    std::string myName = GetMyPersonaName();
    LOG_INFO(Lib_Net, "Steamworks initialized successfully! PersonaName='{}', SteamID={}", myName, myId);

    // Start background callback thread
    m_callbackThread = std::thread(&SteamworksHLE::CallbackThread, this);

    return true;
}

void SteamworksHLE::Shutdown() {
    if (!m_initialized) {
        return;
    }
    m_running = false;
    if (m_callbackThread.joinable()) {
        m_callbackThread.join();
    }

    LeaveLobby();
    SteamAPI_Shutdown();
    m_initialized = false;
    LOG_INFO(Lib_Net, "Steamworks shutdown completed.");
}

void SteamworksHLE::CallbackThread() {
    while (m_running) {
        SteamAPI_RunCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

uint64_t SteamworksHLE::GetMySteamId() const {
    if (!m_initialized || !SteamUser()) {
        return 0x1000000000000001ULL;
    }
    return SteamUser()->GetSteamID().ConvertToUint64();
}

std::string SteamworksHLE::GetMyPersonaName() const {
    if (!m_initialized || !SteamFriends()) {
        return "Player_1000";
    }
    const char* persona = SteamFriends()->GetPersonaName();
    if (!persona || persona[0] == '\0') {
        return "Player_1000";
    }

    // Defensive Sanitization:
    // PSN Online IDs only allow alphanumeric characters, hyphens, and underscores, max 16 chars.
    // Strip control chars, format specifiers, and non-printable bytes to prevent guest crashes.
    std::string sanitized;
    sanitized.reserve(16);
    for (const char* p = persona; *p != '\0' && sanitized.size() < 16; ++p) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            sanitized.push_back(c);
        } else if (c == ' ') {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        sanitized = "Player_1000";
    }
    return sanitized;
}

void SteamworksHLE::FillOnlineId(char* outOnlineId, size_t maxLen) {
    if (!outOnlineId || maxLen == 0) return;
    std::string name = GetMyPersonaName();
    std::strncpy(outOnlineId, name.c_str(), maxLen - 1);
    outOnlineId[maxLen - 1] = '\0';
}

void SteamworksHLE::FillNpId(uint8_t* outHandleData, size_t len) {
    if (!outHandleData || len == 0) return;
    std::string name = GetMyPersonaName();
    std::memset(outHandleData, 0, len);
    std::strncpy(reinterpret_cast<char*>(outHandleData), name.c_str(), len - 1);
}

bool SteamworksHLE::CreateLobby(int maxMembers, bool openInviteOverlay) {
    if (!m_initialized || !SteamMatchmaking()) {
        LOG_WARNING(Lib_NpMatching2, "CreateLobby called but Steam is not active.");
        return false;
    }

    m_openOverlayOnCreate = openInviteOverlay;
    LOG_INFO(Lib_NpMatching2, "Creating Steam public lobby (maxMembers={})...", maxMembers);

    SteamAPICall_t hSteamAPICall =
        SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, maxMembers > 0 ? maxMembers : 4);
    m_lobbyCreatedCallResult.Set(hSteamAPICall, this, &SteamworksHLE::OnLobbyCreated);
    return true;
}

void SteamworksHLE::SetLobbyData(const std::string& key, const std::string& value) {
    if (m_initialized && m_currentLobbyId != 0 && SteamMatchmaking()) {
        SteamMatchmaking()->SetLobbyData(CSteamID(m_currentLobbyId), key.c_str(), value.c_str());
    }
}

std::string SteamworksHLE::GetLobbyData(const std::string& key) {
    if (m_initialized && m_currentLobbyId != 0 && SteamMatchmaking()) {
        const char* val = SteamMatchmaking()->GetLobbyData(CSteamID(m_currentLobbyId), key.c_str());
        return val ? std::string(val) : std::string{};
    }
    return {};
}

void SteamworksHLE::SearchLobbies() {
    if (!m_initialized || !SteamMatchmaking()) {
        return;
    }
    LOG_INFO(Lib_NpMatching2, "Searching Steam lobbies for JoJo_EoH...");
    SteamMatchmaking()->AddRequestLobbyListStringFilter("game", "JoJo_EoH", k_ELobbyComparisonEqual);
    SteamAPICall_t hCall = SteamMatchmaking()->RequestLobbyList();
    m_lobbyMatchListCallResult.Set(hCall, this, &SteamworksHLE::OnLobbyMatchList);
}

bool SteamworksHLE::JoinLobby(uint64_t steamIdLobby) {
    if (!m_initialized || !SteamMatchmaking()) {
        return false;
    }
    LOG_INFO(Lib_NpMatching2, "Joining Steam lobby {}...", steamIdLobby);
    SteamAPICall_t hCall = SteamMatchmaking()->JoinLobby(CSteamID(steamIdLobby));
    m_lobbyEnterCallResult.Set(hCall, this, &SteamworksHLE::OnLobbyEntered);
    return true;
}

void SteamworksHLE::LeaveLobby() {
    if (m_initialized && m_currentLobbyId != 0 && SteamMatchmaking()) {
        LOG_INFO(Lib_NpMatching2, "Leaving Steam lobby {}", m_currentLobbyId);
        SteamMatchmaking()->LeaveLobby(CSteamID(m_currentLobbyId));
        m_currentLobbyId = 0;
    }
}

void SteamworksHLE::OpenInviteOverlay() {
    if (!m_initialized || !SteamFriends()) {
        return;
    }
    if (m_currentLobbyId != 0) {
        LOG_INFO(Lib_NpMatching2, "Activating Steam Game Overlay Invite Dialog for lobby={}", m_currentLobbyId);
        SteamFriends()->ActivateGameOverlayInviteDialog(CSteamID(m_currentLobbyId));
    } else {
        SteamFriends()->ActivateGameOverlay("Friends");
    }
}

bool SteamworksHLE::IsVirtualPeerIp(uint32_t ip) const {
    // 10.13.37.0/24 subnet match in network byte order
    return (ip & 0x00FFFFFF) == ((10) | (0 << 8) | (0 << 16)); // 10.0.0.0/24
}

bool SteamworksHLE::HasActivePeer() const {
    std::lock_guard lock(m_peerMutex);
    return m_lastPeerSteamId != 0;
}

uint32_t SteamworksHLE::GetActivePeerIp() const {
    std::lock_guard lock(m_peerMutex);
    if (m_lastPeerSteamId != 0) {
        auto it = m_steamIdToIp.find(m_lastPeerSteamId);
        if (it != m_steamIdToIp.end()) {
            return it->second;
        }
    }
    return (10) | (0 << 8) | (0 << 16) | (2 << 24); // 10.0.0.2 default peer
}

uint32_t SteamworksHLE::RegisterPeer(uint64_t steamId) {
    if (steamId == 0) {
        return 0;
    }

    std::lock_guard lock(m_peerMutex);
    auto it = m_steamIdToIp.find(steamId);
    if (it != m_steamIdToIp.end()) {
        m_lastPeerSteamId = steamId;
        return it->second;
    }

    uint32_t idx = m_nextPeerIpIndex++;
    if (idx > 254) {
        idx = 2; // Safe subnet wrap-around
    }
    uint32_t ip = (10) | (0 << 8) | (0 << 16) | ((idx & 0xFF) << 24); // 10.0.0.x
    m_steamIdToIp[steamId] = ip;
    m_ipToSteamId[ip] = steamId;
    m_lastPeerSteamId = steamId;
    LOG_INFO(Lib_Net, "Registered virtual peer IP {}.{}.{}.{} for SteamID={}",
             ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF, steamId);
    return ip;
}

uint64_t SteamworksHLE::GetSteamIdFromIp(uint32_t ip) const {
    std::lock_guard lock(m_peerMutex);
    auto it = m_ipToSteamId.find(ip);
    return it != m_ipToSteamId.end() ? it->second : 0;
}

uint32_t SteamworksHLE::GetIpFromSteamId(uint64_t steamId) const {
    if (steamId == 0) {
        return 0;
    }
    std::lock_guard lock(m_peerMutex);
    auto it = m_steamIdToIp.find(steamId);
    return it != m_steamIdToIp.end() ? it->second : 0;
}

int SteamworksHLE::SendTo(const void* buf, size_t len, uint32_t destIp, uint16_t /*destPort*/) {
    // Defensive packet checks: upper bound 65535, non-null buffer
    constexpr size_t MAX_SAFE_PACKET_SIZE = 65535;
    if (!m_initialized || !SteamNetworkingMessages() || buf == nullptr || len == 0 || len > MAX_SAFE_PACKET_SIZE) {
        return -1;
    }

    uint64_t targetSteamId = GetSteamIdFromIp(destIp);
    if (targetSteamId == 0) {
        std::lock_guard lock(m_peerMutex);
        targetSteamId = m_lastPeerSteamId;
    }
    if (targetSteamId == 0) {
        return -1;
    }

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(targetSteamId);

    EResult res = SteamNetworkingMessages()->SendMessageToUser(
        identity, buf, static_cast<uint32>(len),
        k_nSteamNetworkingSend_UnreliableNoDelay | k_nSteamNetworkingSend_AutoRestartBrokenSession, 0);

    if (res == k_EResultOK) {
        return static_cast<int>(len);
    }
    return -1;
}

int SteamworksHLE::RecvFrom(void* buf, size_t len, uint32_t* srcIp, uint16_t* srcPort) {
    constexpr size_t MAX_SAFE_PACKET_SIZE = 65535;
    if (!m_initialized || !SteamNetworkingMessages() || buf == nullptr || len == 0) {
        return 0;
    }

    SteamNetworkingMessage_t* pMsg = nullptr;
    int count = SteamNetworkingMessages()->ReceiveMessagesOnChannel(0, &pMsg, 1);
    if (count > 0 && pMsg != nullptr) {
        // Defensive security checks: packet size and null pointer validation
        if (pMsg->m_cbSize > MAX_SAFE_PACKET_SIZE || pMsg->m_pData == nullptr) {
            LOG_WARNING(Lib_Net, "SteamworksHLE: Dropping oversized or corrupt packet (size={})", pMsg->m_cbSize);
            pMsg->Release();
            return 0;
        }

        size_t copySize = std::min(len, static_cast<size_t>(pMsg->m_cbSize));
        std::memcpy(buf, pMsg->m_pData, copySize);

        uint64_t senderSteamId = pMsg->m_identityPeer.GetSteamID64();
        uint32_t peerIp = 0;
        if (senderSteamId != 0) {
            peerIp = RegisterPeer(senderSteamId);
        }

        if (srcIp) *srcIp = peerIp;
        if (srcPort) *srcPort = 3658;

        pMsg->Release();
        return static_cast<int>(copySize);
    }
    return 0;
}

void SteamworksHLE::OnLobbyCreated(LobbyCreated_t* pResult, bool bIOFailure) {
    if (bIOFailure || pResult->m_eResult != k_EResultOK) {
        LOG_ERROR(Lib_NpMatching2, "Steam lobby creation failed, result={}", static_cast<int>(pResult->m_eResult));
        return;
    }

    m_currentLobbyId = pResult->m_ulSteamIDLobby;
    LOG_INFO(Lib_NpMatching2, "Steam lobby created! lobbyId={}", m_currentLobbyId);

    SteamMatchmaking()->SetLobbyData(CSteamID(m_currentLobbyId), "game", "JoJo_EoH");
    SteamMatchmaking()->SetLobbyData(CSteamID(m_currentLobbyId), "title", "Eyes of Heaven");
    SteamMatchmaking()->SetLobbyData(CSteamID(m_currentLobbyId), "host", GetMyPersonaName().c_str());

    if (m_openOverlayOnCreate) {
        m_openOverlayOnCreate = false;
        OpenInviteOverlay();
    }
}

void SteamworksHLE::OnLobbyEntered(LobbyEnter_t* pResult, bool bIOFailure) {
    if (bIOFailure || pResult->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
        LOG_ERROR(Lib_NpMatching2, "Steam lobby join failed, response={}", pResult->m_EChatRoomEnterResponse);
        return;
    }

    m_currentLobbyId = pResult->m_ulSteamIDLobby;
    LOG_INFO(Lib_NpMatching2, "Steam lobby entered: lobbyId={}", m_currentLobbyId);

    CSteamID owner = SteamMatchmaking()->GetLobbyOwner(CSteamID(m_currentLobbyId));
    if (owner.IsValid() && owner != SteamUser()->GetSteamID()) {
        RegisterPeer(owner.ConvertToUint64());
    }
}

void SteamworksHLE::OnLobbyMatchList(LobbyMatchList_t* pResult, bool bIOFailure) {
    if (bIOFailure) return;
    LOG_INFO(Lib_NpMatching2, "Steam found {} lobbies for JoJo_EoH", pResult->m_nLobbiesMatching);
}

void SteamworksHLE::OnGameLobbyJoinRequested(GameLobbyJoinRequested_t* pCallback) {
    LOG_INFO(Lib_NpMatching2, "Steam game lobby join requested from friend! Joining lobbyId={}",
             pCallback->m_steamIDLobby.ConvertToUint64());
    JoinLobby(pCallback->m_steamIDLobby.ConvertToUint64());
}

void SteamworksHLE::OnLobbyChatUpdate(LobbyChatUpdate_t* pCallback) {
    LOG_INFO(Lib_NpMatching2, "Steam lobby chat update: user={} stateChange={:#x}",
             pCallback->m_ulSteamIDUserChanged, pCallback->m_rgfChatMemberStateChange);
    if (pCallback->m_rgfChatMemberStateChange & k_EChatMemberStateChangeEntered) {
        RegisterPeer(pCallback->m_ulSteamIDUserChanged);
    }
}

void SteamworksHLE::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t* pCallback) {
    LOG_INFO(Lib_Net, "Incoming Steam Networking session request from SteamID={}",
             pCallback->m_identityRemote.GetSteamID64());
    if (SteamNetworkingMessages()) {
        SteamNetworkingMessages()->AcceptSessionWithUser(pCallback->m_identityRemote);
        RegisterPeer(pCallback->m_identityRemote.GetSteamID64());
    }
}

} // namespace Libraries::Network
