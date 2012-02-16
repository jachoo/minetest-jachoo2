minetest.register_on_chat_message( function(name, message)

	local meta_name = "lastmessage"
	
	if message:sub(1,1) == '/' then
		
		local cmd = "/lastmessage"
		local s
		
		if message:sub(0, #cmd) == cmd then
			local v = minetest.get_player_meta(name,meta_name,"string")
			if v then
				minetest.chat_send_player(name, 'Your last message: '..v)
			end
			return true
		end
		
	else
		
		minetest.set_player_meta(name,meta_name,"string",message)
	
	end
	
end)

-- minetest.register_on_chat_message( function(name, message)
	-- print("test-jachoo: name="..dump(name).." message="..dump(message))
	
	-- -- types: string, int, double, bool, v3s16, v3f
	
	-- -- local t = {string="a", int=0, double=0.0, bool=true, v3s16={x=1,y=10,z=100}, v3f={x=1.1,y=2.2,z=3.3}}

	-- -- for s,def in pairs(t) do
		-- -- print("wczytuje ["..s.."]")
		-- -- local v = minetest.get_player_meta(name,"test-"..s,s)
		-- -- print(v)
	-- -- end
	
	-- -- for s,def in pairs(t) do
		-- -- print("zapisuje ["..s.."]")
		-- -- minetest.set_player_meta(name,"test-"..s,s,def)
	-- -- end
	
	
	-- local v = nil
	-- local t = nil
	
	-- t = "string"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = "a"
	-- else
		-- print("odczytano ["..t.."] = "..v)
		-- v = v .. "a"
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
		
	-- t = "int"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = 0
	-- else
		-- print("odczytano ["..t.."] = "..v)
		-- v = v + 1
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	-- t = "double"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = 0.1
	-- else
		-- print("odczytano ["..t.."] = "..v)
		-- v = v + 1.1
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	-- t = "bool"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = true
	-- else
		-- local x
		-- if v then x = "T" else x = "F" end
		-- print("odczytano ["..t.."] = "..x)
		-- v = not v
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	-- t = "v3s16"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = {x=1,y=2,z=3}
	-- else
		-- print("odczytano ["..t.."] = "..v.x..","..v.y..","..v.z)
		-- v = {x=v.x+1,y=v.y+1,z=v.z+1}
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	-- t = "v3f"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = {x=1.1,y=2.2,z=3.3}
	-- else
		-- print("odczytano ["..t.."] = "..v.x..","..v.y..","..v.z)
		-- v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	-- t = "v3fpos"
	-- v = minetest.get_player_meta(name,"test-"..t,t)
	-- if v == nil
		-- then v = {x=1.1,y=-20000.2,z=30000.3}
	-- else
		-- print("odczytano ["..t.."] = "..v.x..","..v.y..","..v.z)
		-- v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
	-- end
	-- minetest.set_player_meta(name,"test-"..t,t,v)
	
	
-- end)
