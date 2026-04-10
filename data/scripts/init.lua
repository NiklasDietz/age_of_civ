-- Age of Civilization: Lua Scripting API
-- This file is loaded first when Lua scripting is initialized.
-- It sets up the scripting environment and registers core functions.

print("[Lua] Scripting engine initialized")

-- Game state is available via the 'game' table (bound from C++)
-- game.mapWidth, game.mapHeight, etc.

-- Event registry: mods can register custom event handlers
events = {}

function registerEvent(name, handler)
    if events[name] == nil then
        events[name] = {}
    end
    table.insert(events[name], handler)
end

function fireEvent(name, ...)
    if events[name] == nil then return end
    for _, handler in ipairs(events[name]) do
        handler(...)
    end
end

-- Victory condition registry
victories = {}

function registerVictoryCondition(name, checkFunc)
    victories[name] = checkFunc
end

-- Called by C++ each turn to check custom victory conditions
function checkCustomVictories(playerId, turn)
    for name, checkFunc in pairs(victories) do
        if checkFunc(playerId, turn) then
            return true, name
        end
    end
    return false, nil
end
