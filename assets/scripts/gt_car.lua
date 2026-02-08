-- gt_car.lua
if not self.physicsCreated then
    Physics.CreateCar(self, 1500.0, 600.0) -- Mass, Torque
    self.physicsCreated = true 
end

-- Tuning Variables (You can change these while the game runs!)
local gear_ratios = { 3.5, 2.6, 1.9, 1.4, 1.0 }
local current_gear = 1
local shift_timer = 0.0

return function(self, dt)
    -- 'self' is the Entity. We need to get its C++ controller.
    -- We assume you added a helper to get the controller from the entity
    -- But for now, we will use the global 'carController' exposed earlier for simplicity.
    
    local throttle = 0.0
    local steering = 0.0
    local brake = 0.0
    local handbrake = 0.0

    -- 1. Input Handling (Using our new binding)
    if Input.IsKeyDown(KEY_W) then throttle = 1.0 end
    if Input.IsKeyDown(KEY_S) then throttle = -1.0 end -- Simple reverse
    if Input.IsKeyDown(KEY_A) then steering = -1.0 end
    if Input.IsKeyDown(KEY_D) then steering = 1.0 end
    if Input.IsKeyDown(KEY_SPACE) then handbrake = 1.0 end

    -- 2. Read Speed from C++
    local speed = carController:GetSpeed() -- Km/H

    -- 3. Automatic Transmission Logic
    if speed > 40 and current_gear == 1 then current_gear = 2 end
    if speed > 80 and current_gear == 2 then current_gear = 3 end
    if speed < 35 and current_gear == 2 then current_gear = 1 end

    -- 4. Apply Tuning
    -- We modify the C++ torque value based on our gear
    carController.engineTorque = 600.0 * gear_ratios[current_gear]

    -- 5. Send Commands to Physics Engine
    carController:SetInput(throttle, steering, brake, handbrake)
    
    -- Debug Print (Optional - check your terminal)
    -- print("Gear: " .. current_gear .. " Speed: " .. math.floor(speed))
end