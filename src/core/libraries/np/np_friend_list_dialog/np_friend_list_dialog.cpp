// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <fmt/format.h>
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/libraries/network/steamworks_hle.h"
#include "core/libraries/np/np_friend_list_dialog/np_friend_list_dialog.h"
#include "core/libraries/system/commondialog.h"

namespace Libraries::Np::NpFriendListDialog {

static auto g_status = Libraries::CommonDialog::Status::NONE;

s32 PS4_SYSV_ABI sceNpFriendListDialogInitialize() {
    LOG_INFO(Lib_NpManager, "sceNpFriendListDialogInitialize called");
    g_status = Libraries::CommonDialog::Status::INITIALIZED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpFriendListDialogOpen(const void* param) {
    LOG_INFO(Lib_NpManager, "sceNpFriendListDialogOpen called, param={}", fmt::ptr(param));
    if (Libraries::Network::SteamworksHLE::Instance().IsActive()) {
        Libraries::Network::SteamworksHLE::Instance().OpenInviteOverlay();
    }
    g_status = Libraries::CommonDialog::Status::FINISHED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpFriendListDialogUpdateStatus() {
    return static_cast<s32>(g_status);
}

s32 PS4_SYSV_ABI sceNpFriendListDialogClose() {
    LOG_INFO(Lib_NpManager, "sceNpFriendListDialogClose called");
    g_status = Libraries::CommonDialog::Status::FINISHED;
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceNpFriendListDialogTerminate() {
    LOG_INFO(Lib_NpManager, "sceNpFriendListDialogTerminate called");
    g_status = Libraries::CommonDialog::Status::NONE;
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("2L-W-ZYn2Qo", "libSceNpFriendListDialog", 1, "libSceNpFriendListDialog",
                 sceNpFriendListDialogInitialize);
    LIB_FUNCTION("zUM-RG5Hmyc", "libSceNpFriendListDialog", 1, "libSceNpFriendListDialog",
                 sceNpFriendListDialogOpen);
    LIB_FUNCTION("frwz3eyuA6w", "libSceNpFriendListDialog", 1, "libSceNpFriendListDialog",
                 sceNpFriendListDialogUpdateStatus);
    LIB_FUNCTION("+Y1acHGomGQ", "libSceNpFriendListDialog", 1, "libSceNpFriendListDialog",
                 sceNpFriendListDialogClose);
    LIB_FUNCTION("ECEzk+K9L2k", "libSceNpFriendListDialog", 1, "libSceNpFriendListDialog",
                 sceNpFriendListDialogTerminate);
}

} // namespace Libraries::Np::NpFriendListDialog
