-- car_physics.lua

-- Simulation Constants
gear_ratios = { 3.2, 2.1, 1.5, 1.1, 0.9 }
current_gear = 1
shift_timer = 0.0

function Update(car, dt, w, s, a, d)
    local throttle = 0.0
    local steering = 0.0
    local brake = 0.0
    
    -- 1. Interpret Input
    if w then throttle = 1.0 end
    if s then brake = 1.0 end
    if a then steering = -1.0 end
    if d then steering = 1.0 end

    -- 2. Read C++ Data
    local speed = car:GetSpeed() -- Calls C++ GetSpeedKmH()

    -- 3. Simple Automatic Transmission Logic
    if speed > 30 and current_gear == 1 then current_gear = 2 end
    if speed > 60 and current_gear == 2 then current_gear = 3 end
    if speed < 25 and current_gear == 2 then current_gear = 1 end

    -- 4. Tune Torque based on Gear
    -- We modify the C++ variable 'engineTorque' directly!
    car.engineTorque = 500.0 * gear_ratios[current_gear]

    -- 5. Apply to Physics Engine
    -- Pass calculated values back to Jolt
    car:SetInput(throttle, steering, brake, 0.0)
end