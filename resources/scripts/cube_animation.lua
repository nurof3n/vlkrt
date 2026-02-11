local startTime = os.clock()
local initialPos = nil

function OnUpdate(entity, dt)
    local time = os.clock() - startTime

    if initialPos == nil then
        initialPos = vec3.new(entity.Transform.Position.x, entity.Transform.Position.y, entity.Transform.Position.z)
    end

    -- Rotation: rotate around Y axis (0, 1, 0)
    local rotationSpeed = 1.0
    entity.Transform.Rotation = AngleAxis(time * rotationSpeed, vec3.new(0, 1, 0))

    -- Up and down movement: sine wave on Y axis
    local bounceHeight = 1.0
    local bounceSpeed = 2.0
    entity.Transform.Position.y = initialPos.y + math.sin(time * bounceSpeed) * bounceHeight
end
