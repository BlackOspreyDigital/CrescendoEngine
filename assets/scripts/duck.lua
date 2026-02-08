function Update(deltaTime)
    local time = os.clock()

    --bobbing
    local height = math.sin(time * 2.5) * 0.15
    this.origin.z = 0.0 + height

    -- waddling x rot
    this.angles.x = math.sin(time * 8.0) * 5.0

    -- spinning z rot
    this.angles.z = this.angles.z + (15.0 * deltaTime)
end