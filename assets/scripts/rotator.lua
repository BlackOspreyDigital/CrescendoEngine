-- rotator.lua
-- Returns a function that the Engine calls every frame
-- self = CBaseEntity* (The C++ object)
-- dt   = Delta Time (float)

return function(self, dt)
    -- Read current rotation
    local currentY = self.angles.y
    
    -- Add rotation (90 degrees per second)
    self.angles.y = currentY + (90.0 * dt)
    
    -- Optional: Bob up and down using Sine wave
    -- We can modify origin because we bound Vec3!
    -- self.origin.y = math.sin(os.clock() * 2.0) * 2.0
end