-- Example custom victory condition: Economic Domination
-- Win by controlling 70% of all global trade routes.

registerVictoryCondition("Economic Domination", function(playerId, turn)
    -- This would query game state via C++ bindings
    -- For now, it's a placeholder showing the scripting API
    -- Real implementation: game.getPlayerTradeRouteShare(playerId) >= 0.70
    return false
end)
