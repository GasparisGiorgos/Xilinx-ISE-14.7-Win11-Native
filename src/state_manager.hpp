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

#pragma once
#include "common.hpp"

enum class ActionType {
    CREATE_FILE,
    CREATE_DIR,
    BACKUP_FILE,
    SET_ENV_VAR,
    SET_REGISTRY,
    CREATE_SHORTCUT,
    MODIFY_PROVIDER_ORDER
};

struct StateAction {
    ActionType type;
    std::string primaryTarget;
    std::string secondaryData;
};

class StateManager {
public:
    static StateManager& Instance() {
        static StateManager instance;
        return instance;
    }

    void RecordAction(ActionType type, const std::string& primary, const std::string& secondary = "");
    void SaveState(const fs::path& stateFile);
    void LoadState(const fs::path& stateFile);
    bool RollbackAll(const fs::path& stateFile);
    void ClearState(const fs::path& stateFile);

    static fs::path GetDefaultStatePath();

private:
    StateManager() = default;
    std::vector<StateAction> m_actions;
};
