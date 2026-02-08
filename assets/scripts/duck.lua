-- We return a function that the C++ Engine calls every frame
-- The engine passes the entity itself as 'this' and the time step as 'dt'
return function(this, dt)
    local time = os.clock()

    -- Bobbing (Sine wave on Z axis)
    local height = math.sin(time * 2.5) * 0.15
    this.origin.z = 0.0 + height

    -- Waddling (Rocking on X axis)
    this.angles.x = math.sin(time * 8.0) * 5.0

    -- Spinning (Rotate on Z axis)
    -- We modify the existing angle by adding speed * delta_time
    this.angles.z = this.angles.z + (15.0 * dt)
end