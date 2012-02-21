-----------------------------------------------------------
-- Helper functions
-----------------------------------------------------------

function string:split(sSeparator, nMax, bRegexp)
	assert(sSeparator ~= '')
	assert(nMax == nil or nMax >= 1)

	local aRecord = {}

	if self:len() > 0 then
		local bPlain = not bRegexp
		nMax = nMax or -1

		local nField=1 nStart=1
		local nFirst,nLast = self:find(sSeparator, nStart, bPlain)
		while nFirst and nMax ~= 0 do
			aRecord[nField] = self:sub(nStart, nFirst-1)
			nField = nField+1
			nStart = nLast+1
			nFirst,nLast = self:find(sSeparator, nStart, bPlain)
			nMax = nMax-1
		end
		aRecord[nField] = self:sub(nStart)
	end

	return aRecord
end



-----------------------------------------------------------
-- Player metadata test
-----------------------------------------------------------

minetest.register_on_chat_message( function(name, message)
	print("test-jachoo: name="..dump(name).." message="..dump(message))
		
	local v = nil
	local t = nil
	
	t = "string"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = "a"
	else
		print("read player meta  ["..t.."] = "..v)
		v = v .. "a"
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
		
	t = "int"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = 0
	else
		print("read player meta  ["..t.."] = "..v)
		v = v + 1
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
	
	t = "double"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = 0.1
	else
		print("read player meta  ["..t.."] = "..v)
		v = v + 1.1
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
	
	t = "bool"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = true
	else
		local x
		if v then x = "T" else x = "F" end
		print("read player meta  ["..t.."] = "..x)
		v = not v
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
	
	t = "v3s16"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = {x=1,y=2,z=3}
	else
		print("read player meta  ["..t.."] = "..v.x..","..v.y..","..v.z)
		v = {x=v.x+1,y=v.y+1,z=v.z+1}
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
	
	t = "v3f"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = {x=1.1,y=2.2,z=3.3}
	else
		print("read player meta  ["..t.."] = "..v.x..","..v.y..","..v.z)
		v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
	
	t = "v3fpos"
	v = minetest.get_player_meta(name,"test-"..t,t)
	if v == nil
		then v = {x=1.1,y=-20000.2,z=30000.3}
	else
		print("read player meta  ["..t.."] = "..v.x..","..v.y..","..v.z)
		v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
	end
	minetest.set_player_meta(name,"test-"..t,t,v)
		
end)

-----------------------------------------------------------
-- Player metadata example: command /lastmessage
-----------------------------------------------------------

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

-----------------------------------------------------------
-- Map metadata example: /getmapmeta and /setmapmeta
-----------------------------------------------------------

minetest.register_on_chat_message( function(name, message)
	
	local cmd = "/getmapmeta"
	
	if message:sub(0, #cmd) == cmd then
	
		local meta_name = message:sub(#cmd+2,100)
	
		local v = minetest.get_map_meta(meta_name,"string")
		
		if v then
			minetest.chat_send_player(name, 'Map meta data ['..meta_name..'] = '..v)
		end
		return true
	end
	
	cmd = "/setmapmeta"
	
	if message:sub(0, #cmd) == cmd then
	
		local data = message:split(' ',2)
		
		local meta_name = data[2]
		local meta_val = data[3]
		
		-- minetest.chat_send_player(name,'meta_name = '..meta_name)
		-- minetest.chat_send_player(name,'meta_val = '..meta_val)
		
		if not meta_name or not meta_val then
			minetest.chat_send_player(name,'Set map meta - wrong params!')	
			return true
		end
	
		minetest.set_map_meta(meta_name,"string",meta_val)
		minetest.chat_send_player(name,'Map meta set - OK')
		
		return true
	end
	
end)

-----------------------------------------------------------
-- Custom databases test
-----------------------------------------------------------

jachoo = {}
jachoo.db = minetest.get_database("testdb")
jachoo.table = minetest.get_db_table(jachoo.db,"testtable")
jachoo.table2 = minetest.get_db_table(jachoo.db,"testtable2")

minetest.register_on_chat_message( function(name, message)
	
	local cmd = "/dbtest"
	
	if message:sub(0, #cmd) == cmd then
	
		print("test-jachoo: name="..dump(name).." message="..dump(message))

		local tab = jachoo.table
		
		local v = nil
		local t = nil
		local k = nil
		
		t = "string"
		k = "ks1"
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = "a"
		else
			print("read data  ["..t.."] = "..v)
			v = v .. "a"
		end
		minetest.set_table_data(tab,t,k,t,v)
			
		t = "int"
		k = 101
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = 0
		else
			print("read data  ["..t.."] = "..v)
			v = v + 1
		end
		minetest.set_table_data(tab,t,k,t,v)
		
		t = "double"
		k = 23.48
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = 0.1
		else
			print("read data  ["..t.."] = "..v)
			v = v + 1.1
		end
		minetest.set_table_data(tab,t,k,t,v)
		
		t = "bool"
		k = false
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = true
		else
			local x
			if v then x = "T" else x = "F" end
			print("read data  ["..t.."] = "..x)
			v = not v
		end
		minetest.set_table_data(tab,t,k,t,v)
		
		t = "v3s16"
		k = {x=10,y=20,z=31}
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = {x=1,y=2,z=3}
		else
			print("read data  ["..t.."] = "..v.x..","..v.y..","..v.z)
			v = {x=v.x+1,y=v.y+1,z=v.z+1}
		end
		minetest.set_table_data(tab,t,k,t,v)
		
		t = "v3f"
		k = {x=10.1,y=20.2,z=30.8}
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = {x=1.1,y=2.2,z=3.3}
		else
			print("read data  ["..t.."] = "..v.x..","..v.y..","..v.z)
			v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
		end
		minetest.set_table_data(tab,t,k,t,v)
		
		t = "v3fpos"
		k = {x=20.1,y=20.2,z=30.8}
		v = minetest.get_table_data(tab,t,k,t)
		if v == nil
			then v = {x=1.1,y=-20000.2,z=30000.3}
		else
			print("read data  ["..t.."] = "..v.x..","..v.y..","..v.z)
			v = {x=v.x+1.1,y=v.y+1.1,z=v.z+1.1}
		end
		minetest.set_table_data(tab,t,k,t,v)
	
		return true
	end
	
end)

-----------------------------------------------------------
-- Custom database example:
-- /send <player> <message>
-- /mailbox
-----------------------------------------------------------

jachoo.mailbox = minetest.get_db_table(jachoo.db,"mailbox")

minetest.register_on_chat_message( function(name, message)
	
	local cmd = "/send"
	
	if message:sub(0, #cmd) == cmd then
	
		-- /send <player> <message>
		local data = message:split(' ',2)
		
		local player = data[2]
		local msg = data[3]
		
		if not player or not msg then
			minetest.chat_send_player(name,'Wrong params: /send <player> <message>')
			return true
		end
		
		local receiverref = minetest.env:get_player_by_name(player)
		if receiverref == nil then
			minetest.chat_send_player(name, player..' is not a known player')
			return true
		end
		
		local v = minetest.get_table_data(jachoo.mailbox,"string",player,"string")
		if v then
			v = v.."\n"..name..": "..msg
		else
			v = name..": "..msg
		end
		
		minetest.set_table_data(jachoo.mailbox,"string",player,"string",v)
		minetest.chat_send_player(name, "Message sent to "..player)
	
		return true
	end
	
	local cmd = "/mailbox"
	
	if message:sub(0, #cmd) == cmd then
			
		local v = minetest.get_table_data(jachoo.mailbox,"string",name,"string")
		if v then
			minetest.chat_send_player(name, "Mailbox: \n"..v)
			minetest.remove_table_data(jachoo.mailbox,"string",name)
		else
			minetest.chat_send_player(name, "Your mailbox is empty")
		end
	
		return true
	end
	
end)
