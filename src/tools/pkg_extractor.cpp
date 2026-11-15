// SPDX-FileCopyrightText: Copyright 2026 JoJo EoH Standalone Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

#pragma pack(push, 1)
struct PkgHeader {
    uint32_t magic;            // 0x7F434E54 ("\x7FCNT")
    uint32_t type;             // 0x01 = main, 0x02 = patch, 0x04 = remaster
    uint32_t pkg_flags;
    uint32_t file_count;
    uint32_t entry_count;
    uint16_t sc_entry_count;
    uint16_t entry_count_2;
    uint32_t table_offset;     // Big Endian
    uint32_t entry_data_size;  // Big Endian
    uint64_t body_offset;      // Big Endian
    uint64_t body_size;        // Big Endian
    uint8_t  padding[0x18];
    char     content_id[36];
    uint8_t  padding2[0x0C];
    uint32_t drm_type;
    uint32_t content_type;
};

struct PkgEntry {
    uint32_t id;
    uint32_t filename_offset;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t offset;
    uint32_t size;
    uint64_t padding;
};
#pragma pack(pop)

static uint32_t Swap32(uint32_t val) {
    return ((val >> 24) & 0xff) |
           ((val << 8) & 0xff0000) |
           ((val >> 8) & 0xff00) |
           ((val << 24) & 0xff000000);
}

static uint64_t Swap64(uint64_t val) {
    return (((val & 0xff00000000000000ULL) >> 56) |
            ((val & 0x00ff000000000000ULL) >> 40) |
            ((val & 0x0000ff0000000000ULL) >> 24) |
            ((val & 0x000000ff00000000ULL) >> 8)  |
            ((val & 0x00000000ff000000ULL) << 8)  |
            ((val & 0x0000000000ff0000ULL) << 24) |
            ((val & 0x000000000000ff00ULL) << 40) |
            ((val & 0x00000000000000ffULL) << 56));
}

static std::string GetEntryName(uint32_t id) {
    switch (id) {
        case 0x1000: return "sce_sys/param.sfo";
        case 0x1008: return "sce_sys/trophy/trophy00.trp";
        case 0x1200: return "sce_sys/icon0.png";
        case 0x1220: return "sce_sys/pic0.png";
        case 0x1240: return "sce_sys/pic1.png";
        case 0x1260: return "sce_sys/changeinfo.xml";
        case 0x1280: return "sce_sys/pronunciation.xml";
        case 0x12A0: return "sce_sys/pronunciation.sig";
        case 0x1001: return "sce_sys/origin.sfo";
        default: return "";
    }
}

static void VerifyInstallation(const fs::path& targetDir) {
    std::cout << "\n=======================================================\n";
    std::cout << " JoJo's Bizarre Adventure: Eyes of Heaven Verification \n";
    std::cout << "=======================================================\n";
    std::cout << " Target Directory: " << fs::absolute(targetDir).string() << "\n\n";

    bool has_eboot = fs::exists(targetDir / "eboot.bin");
    bool has_sfo = fs::exists(targetDir / "sce_sys" / "param.sfo");
    bool has_icon = fs::exists(targetDir / "sce_sys" / "icon0.png");

    std::cout << " [ " << (has_eboot ? "OK" : "MISSING") << " ] eboot.bin (Game Executable)\n";
    std::cout << " [ " << (has_sfo   ? "OK" : "MISSING") << " ] sce_sys/param.sfo (System Metadata)\n";
    std::cout << " [ " << (has_icon  ? "OK" : "MISSING") << " ] sce_sys/icon0.png (Cover Icon)\n";

    // Check for CPK archives
    int cpk_count = 0;
    std::error_code ec;
    if (fs::exists(targetDir)) {
        for (const auto& entry : fs::directory_iterator(targetDir, ec)) {
            if (entry.path().extension() == ".cpk") {
                std::cout << " [ OK ] " << entry.path().filename().string() << " (" 
                          << (entry.file_size() / (1024 * 1024)) << " MB)\n";
                cpk_count++;
            }
        }
    }

    std::cout << "\n-------------------------------------------------------\n";
    if (has_eboot && has_sfo) {
        std::cout << " STATUS: READY TO PLAY!\n";
        std::cout << " You can now launch 'JoJo EoH.exe' or 'shadps4.exe'.\n";
    } else {
        std::cout << " STATUS: INCOMPLETE INSTALLATION\n";
        std::cout << " Please copy your dumped game files (eboot.bin, .cpk, etc.)\n";
        std::cout << " directly into: " << fs::absolute(targetDir).string() << "\n";
    }
    std::cout << "-------------------------------------------------------\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    std::cout << "=======================================================\n";
    std::cout << " JoJo's Bizarre Adventure: Eyes of Heaven (CUSA04909) \n";
    std::cout << " Standalone PKG Extractor & Installation Helper       \n";
    std::cout << "=======================================================\n\n";

    fs::path targetDir = fs::current_path() / "game_data" / "CUSA04909";
    std::error_code ec;
    fs::create_directories(targetDir, ec);

    std::string pkgPath;
    if (argc > 1) {
        pkgPath = argv[1];
    } else {
        // Search current directory for any .pkg file
        for (const auto& entry : fs::directory_iterator(fs::current_path(), ec)) {
            if (entry.path().extension() == ".pkg") {
                pkgPath = entry.path().string();
                std::cout << "Found PKG in current folder: " << entry.path().filename().string() << "\n";
                break;
            }
        }

        if (pkgPath.empty()) {
            std::cout << "No .pkg file specified as argument.\n";
            std::cout << "You can:\n";
            std::cout << "  1. Drag and drop your .pkg file directly onto pkg_extractor.exe\n";
            std::cout << "  2. Enter the full path to your .pkg file below\n";
            std::cout << "  3. Or press Enter to verify existing game_data/CUSA04909 files\n\n";
            std::cout << "Path to PKG: ";
            std::getline(std::cin, pkgPath);

            // Strip quotes if user dragged and dropped path into terminal
            if (!pkgPath.empty() && (pkgPath.front() == '"' || pkgPath.front() == '\'')) {
                pkgPath = pkgPath.substr(1, pkgPath.size() - 2);
            }
        }
    }

    if (pkgPath.empty() || !fs::exists(pkgPath)) {
        if (!pkgPath.empty()) {
            std::cout << "Could not locate file: " << pkgPath << "\n";
        }
        VerifyInstallation(targetDir);
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
        return 0;
    }

    std::cout << "\nOpening package: " << pkgPath << "\n";
    std::ifstream file(pkgPath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Error: Unable to open file.\n";
        return 1;
    }

    PkgHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (file.gcount() < static_cast<std::streamsize>(sizeof(header))) {
        std::cerr << "Error: File too small to be a valid PS4 PKG.\n";
        return 1;
    }

    // Check Magic: 0x7F434E54
    if (header.magic != 0x7F434E54 && header.magic != 0x544E437F) {
        std::cerr << "Error: Invalid PKG header magic (expected \\x7FCNT).\n";
        return 1;
    }

    char contentId[37] = {0};
    std::memcpy(contentId, header.content_id, 36);
    contentId[36] = '\0';
    std::cout << "Content ID: " << contentId << "\n";

    uint32_t tableOffset = Swap32(header.table_offset);
    uint32_t entryCount = Swap32(header.entry_count);
    std::cout << "Entries:    " << entryCount << "\n";
    std::cout << "Table Off:  0x" << std::hex << tableOffset << std::dec << "\n\n";

    file.seekg(tableOffset, std::ios::beg);
    std::vector<PkgEntry> entries(entryCount);
    file.read(reinterpret_cast<char*>(entries.data()), entryCount * sizeof(PkgEntry));

    std::cout << "Extracting system files and metadata to " << targetDir.string() << "...\n";
    int extractedCount = 0;

    for (const auto& e : entries) {
        uint32_t id = Swap32(e.id);
        uint32_t offset = Swap32(e.offset);
        uint32_t size = Swap32(e.size);

        std::string name = GetEntryName(id);
        if (name.empty()) {
            continue;
        }

        fs::path outPath = targetDir / name;
        fs::create_directories(outPath.parent_path(), ec);

        std::cout << "  -> Extracting " << name << " (" << (size / 1024) << " KB)...\n";
        file.seekg(offset, std::ios::beg);
        std::vector<char> buf(size);
        file.read(buf.data(), size);

        std::ofstream out(outPath, std::ios::binary);
        if (out.is_open()) {
            out.write(buf.data(), size);
            out.close();
            extractedCount++;
        }
    }

    std::cout << "\nSuccessfully extracted " << extractedCount << " system files.\n";

    // Check if inner pfs is encrypted
    bool has_eboot = fs::exists(targetDir / "eboot.bin");
    if (!has_eboot) {
        std::cout << "\n[!] Notice regarding Game Data (eboot.bin & CPK archives):\n";
        std::cout << "  Retail PS4 PKG archives have encrypted PFS game partitions.\n";
        std::cout << "  If you dumped your game from PS4 using ItemzFlow, GoldHEN Dump, or FPKG,\n";
        std::cout << "  please place the extracted 'eboot.bin' and '*.cpk' game files into:\n";
        std::cout << "  -> " << fs::absolute(targetDir).string() << "\n";
    }

    VerifyInstallation(targetDir);

    std::cout << "\nDone! Press Enter to exit...";
    std::cin.get();
    return 0;
}
