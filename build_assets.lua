print("========================================")
print("      Osprey Engine Asset Compiler      ")
print("========================================")

local content_dir = "content/textures"
local asset_dir = "assets/textures"

-- 1. Ensure the output directories exist
os.execute('mkdir -p "' .. asset_dir .. '"')

-- 2. Find all PNG files in the content directory using the native 'find' command
local command = string.format('find "%s" -type f -name "*.png"', content_dir)
local file_list = io.popen(command)

if file_list then
    for input_path in file_list:lines() do
        -- Extract just the filename (e.g., "vikingemerald_default.png")
        local filename = input_path:match("^.+/(.+)$") or input_path
        
        -- Strip the extension and add .ktx2 (e.g., "vikingemerald_default.ktx2")
        local basename = filename:match("(.+)%..+")
        local output_path = asset_dir .. "/" .. basename .. ".ktx2"
        
        print("[Toktx] Compressing: " .. filename .. " -> .ktx2")
        
        -- 3. Run the Khronos hardware compressor
        -- --t2         : Use KTX version 2
        -- --genmipmap  : Automatically generate mipmaps so Vulkan doesn't have to!
        -- --bcmp       : Apply Basis Universal Supercompression
        local build_cmd = string.format('toktx --t2 --genmipmap --bcmp "%s" "%s"', output_path, input_path)
        
        local success = os.execute(build_cmd)
        if not success then
            print("[Error] Failed to compile: " .. filename)
        end
    end
    file_list:close()
end

print("========================================")
print("         Asset Build Complete!          ")
print("========================================")