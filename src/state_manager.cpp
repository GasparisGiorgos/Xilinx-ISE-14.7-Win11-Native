/*
 * Copyright (C) 2026 Giorgos Gasparis <gasp.giorgos@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "state_manager.hpp"

fs::path StateManager::GetDefaultStatePath() {
    wchar_t profilePath[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, profilePath) == S_OK) {
        fs::path p = fs::path(profilePath) / ".Xilinx" / "deployer_state.json";
        return p;
    }
    return fs::current_path() / "deployer_state.json";
}

void StateManager::RecordAction(ActionType type, const std::string& primary, const std::string& secondary) {
    StateAction act{type, primary, secondary};
    m_actions.push_back(act);
    SaveState(GetDefaultStatePath());
}

void StateManager::SaveState(const fs::path& stateFile) {
    try {
        if (!stateFile.parent_path().empty()) {
            fs::create_directories(stateFile.parent_path());
        }
        std::ofstream out(stateFile);
        if (!out.is_open()) return;

        out << "[\n";
        for (size_t i = 0; i < m_actions.size(); ++i) {
            const auto& a = m_actions[i];
            out << "  {\n";
            out << "    \"type\": " << static_cast<int>(a.type) << ",\n";
            out << "    \"primary\": \"" << a.primaryTarget << "\",\n";
            out << "    \"secondary\": \"" << a.secondaryData << "\"\n";
            out << "  }" << (i + 1 < m_actions.size() ? "," : "") << "\n";
        }
        out << "]\n";
        out.close();
    } catch (...) {}
}

void StateManager::LoadState(const fs::path& stateFile) {
    m_actions.clear();
    if (!fs::exists(stateFile)) return;

    std::ifstream in(stateFile);
    if (!in.is_open()) return;

    std::string line;
    StateAction currentAction;
    bool inObject = false;

    while (std::getline(in, line)) {
        if (line.find("{") != std::string::npos) {
            inObject = true;
            currentAction = StateAction{};
        } else if (line.find("}") != std::string::npos) {
            if (inObject) {
                m_actions.push_back(currentAction);
                inObject = false;
            }
        } else if (inObject) {
            size_t typePos = line.find("\"type\":");
            if (typePos != std::string::npos) {
                std::string val = line.substr(typePos + 7);
                size_t comma = val.find(",");
                if (comma != std::string::npos) val = val.substr(0, comma);
                try {
                    currentAction.type = static_cast<ActionType>(std::stoi(val));
                } catch (...) {}
            }
            size_t primPos = line.find("\"primary\": \"");
            if (primPos != std::string::npos) {
                size_t endQ = line.find("\"", primPos + 12);
                if (endQ != std::string::npos) {
                    currentAction.primaryTarget = line.substr(primPos + 12, endQ - (primPos + 12));
                }
            }
            size_t secPos = line.find("\"secondary\": \"");
            if (secPos != std::string::npos) {
                size_t endQ = line.find("\"", secPos + 14);
                if (endQ != std::string::npos) {
                    currentAction.secondaryData = line.substr(secPos + 14, endQ - (secPos + 14));
                }
            }
        }
    }
}

bool StateManager::RollbackAll(const fs::path& stateFile) {
    LoadState(stateFile);
    if (m_actions.empty()) {
        std::cout << " " << Colors::YELLOW << "[NOTICE] No recorded state actions found in rollback ledger." << Colors::RESET << "\n";
        return true;
    }

    std::cout << "\n " << Colors::BOLD << Colors::WHITE << "Executing State Rollback (" 
              << m_actions.size() << " actions logged in ledger)..." << Colors::RESET << "\n\n";

    // Traverse ledger in REVERSE order for perfect dependency unspooling
    for (auto it = m_actions.rbegin(); it != m_actions.rend(); ++it) {
        const auto& act = *it;
        switch (act.type) {
            case ActionType::CREATE_FILE: {
                fs::path p(act.primaryTarget);
                if (fs::exists(p)) {
                    std::error_code ec;
                    SetFileAttributesW(p.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
                    fs::remove(p, ec);
                    std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                              << " Removed created file: " << Colors::DIM << p.string() << Colors::RESET << "\n";
                }
                break;
            }
            case ActionType::CREATE_SHORTCUT: {
                fs::path p(act.primaryTarget);
                if (fs::exists(p)) {
                    std::error_code ec;
                    fs::remove(p, ec);
                    std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                              << " Removed shortcut: " << Colors::DIM << p.string() << Colors::RESET << "\n";
                }
                break;
            }
            case ActionType::BACKUP_FILE: {
                fs::path target(act.primaryTarget);
                fs::path backup(act.secondaryData);
                if (fs::exists(backup)) {
                    SetFileAttributesW(target.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
                    CopyFileW(backup.wstring().c_str(), target.wstring().c_str(), FALSE);
                    std::error_code ec;
                    fs::remove(backup, ec);
                    std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                              << " Restored original file: " << Colors::DIM << target.string() << Colors::RESET << "\n";
                }
                break;
            }
            case ActionType::SET_ENV_VAR: {
                std::wstring name(act.primaryTarget.begin(), act.primaryTarget.end());
                HKEY hKey;
                if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Environment", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                    if (act.secondaryData.empty()) {
                        RegDeleteValueW(hKey, name.c_str());
                        std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                                  << " Cleared environment variable: " << Colors::BOLD << act.primaryTarget << Colors::RESET << "\n";
                    } else {
                        std::wstring prevVal(act.secondaryData.begin(), act.secondaryData.end());
                        RegSetValueExW(hKey, name.c_str(), 0, REG_SZ,
                                      (const BYTE*)prevVal.c_str(),
                                      (DWORD)((prevVal.length() + 1) * sizeof(wchar_t)));
                        std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                                  << " Restored environment variable: " << Colors::BOLD << act.primaryTarget << Colors::RESET << "\n";
                    }
                    RegCloseKey(hKey);
                }
                break;
            }
            case ActionType::MODIFY_PROVIDER_ORDER: {
                if (!act.secondaryData.empty()) {
                    HKEY hKey;
                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\NetworkProvider\\Order",
                                      0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                        std::wstring orig(act.secondaryData.begin(), act.secondaryData.end());
                        RegSetValueExW(hKey, L"ProviderOrder", 0, REG_SZ,
                                      (const BYTE*)orig.c_str(),
                                      (DWORD)((orig.length() + 1) * sizeof(wchar_t)));
                        RegCloseKey(hKey);
                        std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                                  << " Restored Network ProviderOrder: " << Colors::BOLD << act.secondaryData << Colors::RESET << "\n";
                    }
                }
                break;
            }
            case ActionType::CREATE_DIR: {
                fs::path p(act.primaryTarget);
                if (fs::exists(p) && fs::is_empty(p)) {
                    std::error_code ec;
                    fs::remove(p, ec);
                    std::cout << "  " << Colors::GREEN << "[REVERT]" << Colors::RESET 
                              << " Removed empty directory: " << Colors::DIM << p.string() << Colors::RESET << "\n";
                }
                break;
            }
            default:
                break;
        }
    }

    // Broadcast environment update
    DWORD_PTR dwResult;
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)L"Environment",
                        SMTO_ABORTIFHUNG, 2000, &dwResult);

    ClearState(stateFile);
    return true;
}

void StateManager::ClearState(const fs::path& stateFile) {
    m_actions.clear();
    std::error_code ec;
    fs::remove(stateFile, ec);
}
