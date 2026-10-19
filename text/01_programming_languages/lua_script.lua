-- Fast Math & Easing Functions for Direct2D
local Easing = {}

function Easing.easeInOutCubic(t)
    if t < 0.5 then
        return 4 * t * t * t
    else
        local p = 2 * t - 2
        return 0.5 * p * p * p + 1
    end
end

function Easing.lerp(a, b, t)
    return a + (b - a) * t
end

return Easing
