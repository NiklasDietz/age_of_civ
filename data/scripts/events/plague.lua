-- Example world event: Plague
-- Demonstrates how mods can add custom events via Lua scripting.

registerEvent("onTurnStart", function(turn)
    -- Plague has a small chance of occurring after turn 50
    if turn < 50 then return end

    -- Simple deterministic check (real implementation would use game RNG)
    if turn % 137 == 0 then
        print("[Event] The Black Death strikes! Population reduced.")
        fireEvent("onPlague", turn)
    end
end)

registerEvent("onPlague", function(turn)
    -- Modders can chain additional effects onto plague events
    print("[Event] Trade routes disrupted by plague on turn " .. turn)
end)
