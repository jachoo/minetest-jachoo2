/*
Minetest-c55
Copyright (C) 2011 celeron55, Perttu Ahola <celeron55@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "scriptapi.h"

#include <iostream>
#include <list>
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include "log.h"
#include "server.h"
#include "porting.h"
#include "filesys.h"
#include "serverobject.h"
#include "script.h"
//#include "luna.h"
#include "luaentity_common.h"
#include "content_sao.h" // For LuaEntitySAO
#include "tooldef.h"
#include "nodedef.h"
#include "craftdef.h"
#include "craftitemdef.h"
#include "main.h" // For g_settings
#include "settings.h" // For accessing g_settings
#include "nodemetadata.h"
#include "mapblock.h" // For getNodeBlockPos
#include "content_nodemeta.h"
#include "utility.h"

static void stackDump(lua_State *L, std::ostream &o)
{
  int i;
  int top = lua_gettop(L);
  for (i = 1; i <= top; i++) {  /* repeat for each level */
	int t = lua_type(L, i);
	switch (t) {

	  case LUA_TSTRING:  /* strings */
	  	o<<"\""<<lua_tostring(L, i)<<"\"";
		break;

	  case LUA_TBOOLEAN:  /* booleans */
		o<<(lua_toboolean(L, i) ? "true" : "false");
		break;

	  case LUA_TNUMBER:  /* numbers */ {
	  	char buf[10];
		snprintf(buf, 10, "%g", lua_tonumber(L, i));
		o<<buf;
		break; }

	  default:  /* other values */
		o<<lua_typename(L, t);
		break;

	}
	o<<" ";
  }
  o<<std::endl;
}

static void realitycheck(lua_State *L)
{
	int top = lua_gettop(L);
	if(top >= 30){
		dstream<<"Stack is over 30:"<<std::endl;
		stackDump(L, dstream);
		script_error(L, "Stack is over 30 (reality check)");
	}
}

class StackUnroller
{
private:
	lua_State *m_lua;
	int m_original_top;
public:
	StackUnroller(lua_State *L):
		m_lua(L),
		m_original_top(-1)
	{
		m_original_top = lua_gettop(m_lua); // store stack height
	}
	~StackUnroller()
	{
		lua_settop(m_lua, m_original_top); // restore stack height
	}
};

class ModNameStorer
{
private:
	lua_State *L;
public:
	ModNameStorer(lua_State *L_, const std::string modname):
		L(L_)
	{
		// Store current modname in registry
		lua_pushstring(L, modname.c_str());
		lua_setfield(L, LUA_REGISTRYINDEX, "minetest_current_modname");
	}
	~ModNameStorer()
	{
		// Clear current modname in registry
		lua_pushnil(L);
		lua_setfield(L, LUA_REGISTRYINDEX, "minetest_current_modname");
	}
};

std::string get_current_modname(lua_State *L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, "minetest_current_modname");
	std::string modname = "";
	if(lua_type(L, -1) == LUA_TSTRING)
		modname = lua_tostring(L, -1);
	lua_pop(L, 1);
	return modname;
}

void check_modname_prefix(lua_State *L, std::string &name)
{
	if(name.size() == 0)
		throw LuaError(L, std::string("Name is empty"));
	
	if(name[0] == ':'){
		name = name.substr(1);
		return;
	}
	
	std::string modname = get_current_modname(L);
	assert(modname != "");
	
	// For __builtin, anything goes
	if(modname == "__builtin")
		return;
	
	if(name.substr(0, modname.size()+1) != modname + ":")
		throw LuaError(L, std::string("Name \"")+name
				+"\" does not follow naming conventions: "
				+"\"modname:\" or \":\" prefix required)");
	
	std::string subname = name.substr(modname.size()+1);
	if(!string_allowed(subname, "abcdefghijklmnopqrstuvwxyz"
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"))
		throw LuaError(L, std::string("Name \"")+name
				+"\" does not follow naming conventions: "
				+"\"contains unallowed characters");
}

static void push_v3f(lua_State *L, v3f p)
{
	lua_newtable(L);
	lua_pushnumber(L, p.X);
	lua_setfield(L, -2, "x");
	lua_pushnumber(L, p.Y);
	lua_setfield(L, -2, "y");
	lua_pushnumber(L, p.Z);
	lua_setfield(L, -2, "z");
}

static v2s16 read_v2s16(lua_State *L, int index)
{
	v2s16 p;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "x");
	p.X = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "y");
	p.Y = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return p;
}

static v2f read_v2f(lua_State *L, int index)
{
	v2f p;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "x");
	p.X = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "y");
	p.Y = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return p;
}

static Server* get_server(lua_State *L)
{
	// Get server from registry
	lua_getfield(L, LUA_REGISTRYINDEX, "minetest_server");
	return (Server*)lua_touserdata(L, -1);
}

static ServerEnvironment* get_env(lua_State *L)
{
	// Get environment from registry
	lua_getfield(L, LUA_REGISTRYINDEX, "minetest_env");
	return (ServerEnvironment*)lua_touserdata(L, -1);
}

static v3f read_v3f(lua_State *L, int index)
{
	v3f pos;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "x");
	pos.X = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "y");
	pos.Y = lua_tonumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "z");
	pos.Z = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return pos;
}

static v3f check_v3f(lua_State *L, int index)
{
	v3f pos;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "x");
	pos.X = luaL_checknumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "y");
	pos.Y = luaL_checknumber(L, -1);
	lua_pop(L, 1);
	lua_getfield(L, index, "z");
	pos.Z = luaL_checknumber(L, -1);
	lua_pop(L, 1);
	return pos;
}

static void pushFloatPos(lua_State *L, v3f p)
{
	p /= BS;
	push_v3f(L, p);
}

static v3f checkFloatPos(lua_State *L, int index)
{
	return check_v3f(L, index) * BS;
}

static void push_v3s16(lua_State *L, v3s16 p)
{
	lua_newtable(L);
	lua_pushnumber(L, p.X);
	lua_setfield(L, -2, "x");
	lua_pushnumber(L, p.Y);
	lua_setfield(L, -2, "y");
	lua_pushnumber(L, p.Z);
	lua_setfield(L, -2, "z");
}

static v3s16 read_v3s16(lua_State *L, int index)
{
	// Correct rounding at <0
	v3f pf = read_v3f(L, index);
	return floatToInt(pf, 1.0);
}

static v3s16 check_v3s16(lua_State *L, int index)
{
	// Correct rounding at <0
	v3f pf = check_v3f(L, index);
	return floatToInt(pf, 1.0);
}

static void pushnode(lua_State *L, const MapNode &n, INodeDefManager *ndef)
{
	lua_newtable(L);
	lua_pushstring(L, ndef->get(n).name.c_str());
	lua_setfield(L, -2, "name");
	lua_pushnumber(L, n.getParam1());
	lua_setfield(L, -2, "param1");
	lua_pushnumber(L, n.getParam2());
	lua_setfield(L, -2, "param2");
}

static MapNode readnode(lua_State *L, int index, INodeDefManager *ndef)
{
	lua_getfield(L, index, "name");
	const char *name = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	u8 param1;
	lua_getfield(L, index, "param1");
	if(lua_isnil(L, -1))
		param1 = 0;
	else
		param1 = lua_tonumber(L, -1);
	lua_pop(L, 1);
	u8 param2;
	lua_getfield(L, index, "param2");
	if(lua_isnil(L, -1))
		param2 = 0;
	else
		param2 = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return MapNode(ndef, name, param1, param2);
}

static video::SColor readARGB8(lua_State *L, int index)
{
	video::SColor color;
	luaL_checktype(L, index, LUA_TTABLE);
	lua_getfield(L, index, "a");
	if(lua_isnumber(L, -1))
		color.setAlpha(lua_tonumber(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, index, "r");
	color.setRed(lua_tonumber(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, index, "g");
	color.setGreen(lua_tonumber(L, -1));
	lua_pop(L, 1);
	lua_getfield(L, index, "b");
	color.setBlue(lua_tonumber(L, -1));
	lua_pop(L, 1);
	return color;
}

static core::aabbox3d<f32> read_aabbox3df32(lua_State *L, int index, f32 scale)
{
	core::aabbox3d<f32> box;
	if(lua_istable(L, -1)){
		lua_rawgeti(L, -1, 1);
		box.MinEdge.X = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, 2);
		box.MinEdge.Y = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, 3);
		box.MinEdge.Z = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, 4);
		box.MaxEdge.X = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, 5);
		box.MaxEdge.Y = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, 6);
		box.MaxEdge.Z = lua_tonumber(L, -1) * scale;
		lua_pop(L, 1);
	}
	return box;
}

static bool getstringfield(lua_State *L, int table,
		const char *fieldname, std::string &result)
{
	lua_getfield(L, table, fieldname);
	bool got = false;
	if(lua_isstring(L, -1)){
		result = lua_tostring(L, -1);
		got = true;
	}
	lua_pop(L, 1);
	return got;
}

static bool getintfield(lua_State *L, int table,
		const char *fieldname, int &result)
{
	lua_getfield(L, table, fieldname);
	bool got = false;
	if(lua_isnumber(L, -1)){
		result = lua_tonumber(L, -1);
		got = true;
	}
	lua_pop(L, 1);
	return got;
}

static bool getfloatfield(lua_State *L, int table,
		const char *fieldname, float &result)
{
	lua_getfield(L, table, fieldname);
	bool got = false;
	if(lua_isnumber(L, -1)){
		result = lua_tonumber(L, -1);
		got = true;
	}
	lua_pop(L, 1);
	return got;
}

static bool getboolfield(lua_State *L, int table,
		const char *fieldname, bool &result)
{
	lua_getfield(L, table, fieldname);
	bool got = false;
	if(lua_isboolean(L, -1)){
		result = lua_toboolean(L, -1);
		got = true;
	}
	lua_pop(L, 1);
	return got;
}

static std::string checkstringfield(lua_State *L, int table,
		const char *fieldname)
{
	lua_getfield(L, table, fieldname);
	std::string s = luaL_checkstring(L, -1);
	lua_pop(L, 1);
	return s;
}

static std::string getstringfield_default(lua_State *L, int table,
		const char *fieldname, const std::string &default_)
{
	std::string result = default_;
	getstringfield(L, table, fieldname, result);
	return result;
}

static int getintfield_default(lua_State *L, int table,
		const char *fieldname, int default_)
{
	int result = default_;
	getintfield(L, table, fieldname, result);
	return result;
}

/*static float getfloatfield_default(lua_State *L, int table,
		const char *fieldname, float default_)
{
	float result = default_;
	getfloatfield(L, table, fieldname, result);
	return result;
}*/

static bool getboolfield_default(lua_State *L, int table,
		const char *fieldname, bool default_)
{
	bool result = default_;
	getboolfield(L, table, fieldname, result);
	return result;
}

struct EnumString
{
	int num;
	const char *str;
};

static bool string_to_enum(const EnumString *spec, int &result,
		const std::string &str)
{
	const EnumString *esp = spec;
	while(esp->str){
		if(str == std::string(esp->str)){
			result = esp->num;
			return true;
		}
		esp++;
	}
	return false;
}

/*static bool enum_to_string(const EnumString *spec, std::string &result,
		int num)
{
	const EnumString *esp = spec;
	while(esp){
		if(num == esp->num){
			result = esp->str;
			return true;
		}
		esp++;
	}
	return false;
}*/

static int getenumfield(lua_State *L, int table,
		const char *fieldname, const EnumString *spec, int default_)
{
	int result = default_;
	string_to_enum(spec, result,
			getstringfield_default(L, table, fieldname, ""));
	return result;
}

static void setfloatfield(lua_State *L, int table,
		const char *fieldname, float value)
{
	lua_pushnumber(L, value);
	if(table < 0)
		table -= 1;
	lua_setfield(L, table, fieldname);
}

static void warn_if_field_exists(lua_State *L, int table,
		const char *fieldname, const std::string &message)
{
	lua_getfield(L, table, fieldname);
	if(!lua_isnil(L, -1)){
		infostream<<script_get_backtrace(L)<<std::endl;
		infostream<<"WARNING: field \""<<fieldname<<"\": "
				<<message<<std::endl;
	}
	lua_pop(L, 1);
}

/*
	Inventory stuff
*/

static void inventory_set_list_from_lua(Inventory *inv, const char *name,
		lua_State *L, int tableindex, IGameDef *gamedef, int forcesize=-1)
{
	if(tableindex < 0)
		tableindex = lua_gettop(L) + 1 + tableindex;
	// If nil, delete list
	if(lua_isnil(L, tableindex)){
		inv->deleteList(name);
		return;
	}
	// Otherwise set list
	std::list<std::string> items;
	luaL_checktype(L, tableindex, LUA_TTABLE);
	int table = tableindex;
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TSTRING);
		std::string itemstring = luaL_checkstring(L, -1);
		items.push_back(itemstring);
		// removes value, keeps key for next iteration
		lua_pop(L, 1);
	}
	int listsize = (forcesize != -1) ? forcesize : items.size();
	InventoryList *invlist = inv->addList(name, listsize);
	int index = 0;
	for(std::list<std::string>::const_iterator
			i = items.begin(); i != items.end(); i++){
		if(forcesize != -1 && index == forcesize)
			break;
		const std::string &itemstring = *i;
		InventoryItem *newitem = NULL;
		if(itemstring != "")
			newitem = InventoryItem::deSerialize(itemstring,
					gamedef);
		InventoryItem *olditem = invlist->changeItem(index, newitem);
		delete olditem;
		index++;
	}
	while(forcesize != -1 && index < forcesize){
		InventoryItem *olditem = invlist->changeItem(index, NULL);
		delete olditem;
		index++;
	}
}

static void inventory_get_list_to_lua(Inventory *inv, const char *name,
		lua_State *L)
{
	InventoryList *invlist = inv->getList(name);
	if(invlist == NULL){
		lua_pushnil(L);
		return;
	}
	// Get the table insert function
	lua_getglobal(L, "table");
	lua_getfield(L, -1, "insert");
	int table_insert = lua_gettop(L);
	// Create and fill table
	lua_newtable(L);
	int table = lua_gettop(L);
	for(u32 i=0; i<invlist->getSize(); i++){
		InventoryItem *item = invlist->getItem(i);
		lua_pushvalue(L, table_insert);
		lua_pushvalue(L, table);
		if(item == NULL){
			lua_pushstring(L, "");
		} else {
			lua_pushstring(L, item->getItemString().c_str());
		}
		if(lua_pcall(L, 2, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
	}
}

static void push_stack_item(lua_State *L, InventoryItem *item0)
{
	if(item0 == NULL){
		lua_pushnil(L);
	}
	else if(std::string("MaterialItem") == item0->getName()){
		MaterialItem *item = (MaterialItem*)item0;
		lua_newtable(L);
		lua_pushstring(L, "node");
		lua_setfield(L, -2, "type");
		lua_pushstring(L, item->getNodeName().c_str());
		lua_setfield(L, -2, "name");
	}
	else if(std::string("CraftItem") == item0->getName()){
		CraftItem *item = (CraftItem*)item0;
		lua_newtable(L);
		lua_pushstring(L, "craft");
		lua_setfield(L, -2, "type");
		lua_pushstring(L, item->getSubName().c_str());
		lua_setfield(L, -2, "name");
	}
	else if(std::string("ToolItem") == item0->getName()){
		ToolItem *item = (ToolItem*)item0;
		lua_newtable(L);
		lua_pushstring(L, "tool");
		lua_setfield(L, -2, "type");
		lua_pushstring(L, item->getToolName().c_str());
		lua_setfield(L, -2, "name");
		lua_pushstring(L, itos(item->getWear()).c_str());
		lua_setfield(L, -2, "wear");
	}
	else{
		errorstream<<"push_stack_item: Unknown item name: \""
				<<item0->getName()<<"\""<<std::endl;
		lua_pushnil(L);
	}
}

static InventoryItem* check_stack_item(lua_State *L, int index)
{
	if(index < 0)
		index = lua_gettop(L) + 1 + index;
	if(lua_isnil(L, index))
		return NULL;
	if(!lua_istable(L, index))
		throw LuaError(L, "check_stack_item: Item not nil or table");
	// A very crappy implementation for now
	// Will be replaced when unified namespace for items is made
	std::string type = getstringfield_default(L, index, "type", "");
	std::string name = getstringfield_default(L, index, "name", "");
	std::string num = getstringfield_default(L, index, "wear", "_");
	if(num == "_")
		num = 1;
	std::string itemstring = type + " \"" + name + "\" " + num;
	try{
		return InventoryItem::deSerialize(itemstring, get_server(L));
	}catch(SerializationError &e){
		throw LuaError(L, std::string("check_stack_item: "
				"internal error (itemstring=\"")+itemstring+"\")");
	}
}

/*
	ToolDiggingProperties
*/

static ToolDiggingProperties read_tool_digging_properties(
		lua_State *L, int table)
{
	ToolDiggingProperties prop;
	getfloatfield(L, table, "full_punch_interval", prop.full_punch_interval);
	getfloatfield(L, table, "basetime", prop.basetime);
	getfloatfield(L, table, "dt_weight", prop.dt_weight);
	getfloatfield(L, table, "dt_crackiness", prop.dt_crackiness);
	getfloatfield(L, table, "dt_crumbliness", prop.dt_crumbliness);
	getfloatfield(L, table, "dt_cuttability", prop.dt_cuttability);
	getfloatfield(L, table, "basedurability", prop.basedurability);
	getfloatfield(L, table, "dd_weight", prop.dd_weight);
	getfloatfield(L, table, "dd_crackiness", prop.dd_crackiness);
	getfloatfield(L, table, "dd_crumbliness", prop.dd_crumbliness);
	getfloatfield(L, table, "dd_cuttability", prop.dd_cuttability);
	return prop;
}

static void set_tool_digging_properties(lua_State *L, int table,
		const ToolDiggingProperties &prop)
{
	setfloatfield(L, table, "full_punch_interval", prop.full_punch_interval);
	setfloatfield(L, table, "basetime", prop.basetime);
	setfloatfield(L, table, "dt_weight", prop.dt_weight);
	setfloatfield(L, table, "dt_crackiness", prop.dt_crackiness);
	setfloatfield(L, table, "dt_crumbliness", prop.dt_crumbliness);
	setfloatfield(L, table, "dt_cuttability", prop.dt_cuttability);
	setfloatfield(L, table, "basedurability", prop.basedurability);
	setfloatfield(L, table, "dd_weight", prop.dd_weight);
	setfloatfield(L, table, "dd_crackiness", prop.dd_crackiness);
	setfloatfield(L, table, "dd_crumbliness", prop.dd_crumbliness);
	setfloatfield(L, table, "dd_cuttability", prop.dd_cuttability);
}

static void push_tool_digging_properties(lua_State *L,
		const ToolDiggingProperties &prop)
{
	lua_newtable(L);
	set_tool_digging_properties(L, -1, prop);
}

/*
	ToolDefinition
*/

static ToolDefinition read_tool_definition(lua_State *L, int table)
{
	ToolDefinition def;
	getstringfield(L, table, "image", def.imagename);
	def.properties = read_tool_digging_properties(L, table);
	return def;
}

static void push_tool_definition(lua_State *L, const ToolDefinition &def)
{
	lua_newtable(L);
	lua_pushstring(L, def.imagename.c_str());
	lua_setfield(L, -2, "image");
	set_tool_digging_properties(L, -1, def.properties);
}

/*
	EnumString definitions
*/

struct EnumString es_DrawType[] =
{
	{NDT_NORMAL, "normal"},
	{NDT_AIRLIKE, "airlike"},
	{NDT_LIQUID, "liquid"},
	{NDT_FLOWINGLIQUID, "flowingliquid"},
	{NDT_GLASSLIKE, "glasslike"},
	{NDT_ALLFACES, "allfaces"},
	{NDT_ALLFACES_OPTIONAL, "allfaces_optional"},
	{NDT_TORCHLIKE, "torchlike"},
	{NDT_SIGNLIKE, "signlike"},
	{NDT_PLANTLIKE, "plantlike"},
	{NDT_FENCELIKE, "fencelike"},
	{NDT_RAILLIKE, "raillike"},
	{0, NULL},
};

struct EnumString es_ContentParamType[] =
{
	{CPT_NONE, "none"},
	{CPT_LIGHT, "light"},
	{CPT_MINERAL, "mineral"},
	{CPT_FACEDIR_SIMPLE, "facedir_simple"},
	{0, NULL},
};

struct EnumString es_LiquidType[] =
{
	{LIQUID_NONE, "none"},
	{LIQUID_FLOWING, "flowing"},
	{LIQUID_SOURCE, "source"},
	{0, NULL},
};

struct EnumString es_NodeBoxType[] =
{
	{NODEBOX_REGULAR, "regular"},
	{NODEBOX_FIXED, "fixed"},
	{NODEBOX_WALLMOUNTED, "wallmounted"},
	{0, NULL},
};

struct EnumString es_Diggability[] =
{
	{DIGGABLE_NOT, "not"},
	{DIGGABLE_NORMAL, "normal"},
	{DIGGABLE_CONSTANT, "constant"},
	{0, NULL},
};

/*
	Getters for stuff in main tables
*/

static void objectref_get(lua_State *L, u16 id)
{
	// Get minetest.object_refs[i]
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "object_refs");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushnumber(L, id);
	lua_gettable(L, -2);
	lua_remove(L, -2); // object_refs
	lua_remove(L, -2); // minetest
}

static void luaentity_get(lua_State *L, u16 id)
{
	// Get minetest.luaentities[i]
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "luaentities");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushnumber(L, id);
	lua_gettable(L, -2);
	lua_remove(L, -2); // luaentities
	lua_remove(L, -2); // minetest
}

/*
	Object wrappers
*/

#define method(class, name) {#name, class::l_##name}

/*
	ItemStack
*/

class ItemStack
{
private:
	InventoryItem *m_stack;

	static const char className[];
	static const luaL_reg methods[];

	// Exported functions
	
	// garbage collector
	static int gc_object(lua_State *L) {
		ItemStack *o = *(ItemStack **)(lua_touserdata(L, 1));
		delete o;
		return 0;
	}

	// peek_item(self)
	static int l_peek_item(lua_State *L)
	{
		ItemStack *o = checkobject(L, 1);
		push_stack_item(L, o->m_stack);
		return 1;
	}

	// take_item(self)
	static int l_take_item(lua_State *L)
	{
		ItemStack *o = checkobject(L, 1);
		push_stack_item(L, o->m_stack);
		if(o->m_stack->getCount() <= 1){
			delete o->m_stack;
			o->m_stack = NULL;
		} else {
			o->m_stack->remove(1);
		}
		return 1;
	}

	// put_item(self, item) -> true/false
	static int l_put_item(lua_State *L)
	{
		ItemStack *o = checkobject(L, 1);
		InventoryItem *item = check_stack_item(L, 2);
		if(!item){ // nil can always be inserted
			lua_pushboolean(L, true);
			return 1;
		}
		if(!item->addableTo(o->m_stack)){
			lua_pushboolean(L, false);
			return 1;
		}
		o->m_stack->add(1);
		delete item;
		lua_pushboolean(L, true);
		return 1;
	}

	// put_stackstring(self, stackstring) -> true/false
	static int l_put_stackstring(lua_State *L)
	{
		ItemStack *o = checkobject(L, 1);
		std::string stackstring = luaL_checkstring(L, 2);
		try{
			InventoryItem *item = InventoryItem::deSerialize(stackstring,
					get_server(L));
			if(!item->addableTo(o->m_stack)){
				lua_pushboolean(L, false);
				return 1;
			}
			o->m_stack->add(1);
			delete item;
			lua_pushboolean(L, true);
			return 1;
		}
		catch(SerializationError &e){
			lua_pushboolean(L, false);
			return 1;
		}
	}

public:
	ItemStack(InventoryItem *item=NULL):
		m_stack(item)
	{
	}

	~ItemStack()
	{
		delete m_stack;
	}

	static ItemStack* checkobject(lua_State *L, int narg)
	{
		luaL_checktype(L, narg, LUA_TUSERDATA);
		void *ud = luaL_checkudata(L, narg, className);
		if(!ud) luaL_typerror(L, narg, className);
		return *(ItemStack**)ud;  // unbox pointer
	}

	InventoryItem* getItemCopy()
	{
		if(!m_stack)
			return NULL;
		return m_stack->clone();
	}
	
	// Creates an ItemStack and leaves it on top of stack
	static int create_object(lua_State *L)
	{
		InventoryItem *item = NULL;
		if(lua_isstring(L, 1)){
			std::string itemstring = lua_tostring(L, 1);
			if(itemstring != ""){
				try{
					IGameDef *gdef = get_server(L);
					item = InventoryItem::deSerialize(itemstring, gdef);
				}catch(SerializationError &e){
				}
			}
		}
		ItemStack *o = new ItemStack(item);
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
		return 1;
	}
	// Not callable from Lua
	static int create(lua_State *L, InventoryItem *item)
	{
		ItemStack *o = new ItemStack(item);
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
		return 1;
	}

	static void Register(lua_State *L)
	{
		lua_newtable(L);
		int methodtable = lua_gettop(L);
		luaL_newmetatable(L, className);
		int metatable = lua_gettop(L);

		lua_pushliteral(L, "__metatable");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);  // hide metatable from Lua getmetatable()

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, gc_object);
		lua_settable(L, metatable);

		lua_pop(L, 1);  // drop metatable

		luaL_openlib(L, 0, methods, 0);  // fill methodtable
		lua_pop(L, 1);  // drop methodtable

		// Can be created from Lua (ItemStack::create(itemstring))
		lua_register(L, className, create_object);
	}
};
const char ItemStack::className[] = "ItemStack";
const luaL_reg ItemStack::methods[] = {
	method(ItemStack, peek_item),
	method(ItemStack, take_item),
	method(ItemStack, put_item),
	method(ItemStack, put_stackstring),
	{0,0}
};

/*
	InvRef
*/

class InvRef
{
private:
	InventoryLocation m_loc;

	static const char className[];
	static const luaL_reg methods[];

	static InvRef *checkobject(lua_State *L, int narg)
	{
		luaL_checktype(L, narg, LUA_TUSERDATA);
		void *ud = luaL_checkudata(L, narg, className);
		if(!ud) luaL_typerror(L, narg, className);
		return *(InvRef**)ud;  // unbox pointer
	}
	
	static Inventory* getinv(lua_State *L, InvRef *ref)
	{
		return get_server(L)->getInventory(ref->m_loc);
	}

	static InventoryList* getlist(lua_State *L, InvRef *ref,
			const char *listname)
	{
		Inventory *inv = getinv(L, ref);
		if(!inv)
			return NULL;
		return inv->getList(listname);
	}

	static InventoryItem* getitem(lua_State *L, InvRef *ref,
			const char *listname, int i)
	{
		InventoryList *list = getlist(L, ref, listname);
		if(!list)
			return NULL;
		return list->getItem(i);
	}

	static void reportInventoryChange(lua_State *L, InvRef *ref)
	{
		// Inform other things that the inventory has changed
		get_server(L)->setInventoryModified(ref->m_loc);
	}
	
	// Exported functions
	
	// garbage collector
	static int gc_object(lua_State *L) {
		InvRef *o = *(InvRef **)(lua_touserdata(L, 1));
		delete o;
		return 0;
	}

	// get_size(self, listname)
	static int l_get_size(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		InventoryList *list = getlist(L, ref, listname);
		if(list){
			lua_pushinteger(L, list->getSize());
		} else {
			lua_pushinteger(L, 0);
		}
		return 1;
	}

	// set_size(self, listname, size)
	static int l_set_size(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		int newsize = luaL_checknumber(L, 3);
		Inventory *inv = getinv(L, ref);
		if(newsize == 0){
			inv->deleteList(listname);
			reportInventoryChange(L, ref);
			return 0;
		}
		InventoryList *list = inv->getList(listname);
		if(list){
			list->setSize(newsize);
		} else {
			list = inv->addList(listname, newsize);
		}
		reportInventoryChange(L, ref);
		return 0;
	}

	// get_stack(self, listname, i)
	static int l_get_stack(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		int i = luaL_checknumber(L, 3) - 1;
		InventoryItem *item = getitem(L, ref, listname, i);
		if(!item){
			ItemStack::create(L, NULL);
			return 1;
		}
		ItemStack::create(L, item->clone());
		return 1;
	}

	// set_stack(self, listname, i, stack)
	static int l_set_stack(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		int i = luaL_checknumber(L, 3) - 1;
		ItemStack *stack = ItemStack::checkobject(L, 4);
		InventoryList *list = getlist(L, ref, listname);
		if(!list){
			lua_pushboolean(L, false);
			return 1;
		}
		InventoryItem *newitem = stack->getItemCopy();
		InventoryItem *olditem = list->changeItem(i, newitem);
		bool success = (olditem != newitem);
		delete olditem;
		lua_pushboolean(L, success);
		reportInventoryChange(L, ref);
		return 1;
	}

	// get_list(self, listname) -> list or nil
	static int l_get_list(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		Inventory *inv = getinv(L, ref);
		inventory_get_list_to_lua(inv, listname, L);
		return 1;
	}

	// set_list(self, listname, list)
	static int l_set_list(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		Inventory *inv = getinv(L, ref);
		InventoryList *list = inv->getList(listname);
		if(list)
			inventory_set_list_from_lua(inv, listname, L, 3,
					get_server(L), list->getSize());
		else
			inventory_set_list_from_lua(inv, listname, L, 3,
					get_server(L));
		reportInventoryChange(L, ref);
		return 0;
	}

	// autoinsert_stack(self, listname, stack)
	static int l_autoinsert_stack(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		ItemStack *stack = ItemStack::checkobject(L, 3);
		InventoryList *list = getlist(L, ref, listname);
		if(!list){
			lua_pushboolean(L, false);
			return 1;
		}
		InventoryItem *item = stack->getItemCopy();
		if(list->roomForItem(item)){
			delete list->addItem(item);
			lua_pushboolean(L, true);
			reportInventoryChange(L, ref);
		} else {
			delete item;
			lua_pushboolean(L, false);
		}
		return 1;
	}

	// autoinsert_stackstring(self, listname, stackstring)
	static int l_autoinsert_stackstring(lua_State *L)
	{
		InvRef *ref = checkobject(L, 1);
		const char *listname = luaL_checkstring(L, 2);
		const char *stackstring = luaL_checkstring(L, 3);
		InventoryList *list = getlist(L, ref, listname);
		if(!list){
			lua_pushboolean(L, false);
			return 1;
		}
		InventoryItem *item = InventoryItem::deSerialize(stackstring,
				get_server(L));
		if(list->roomForItem(item)){
			delete list->addItem(item);
			lua_pushboolean(L, true);
			reportInventoryChange(L, ref);
		} else {
			delete item;
			lua_pushboolean(L, false);
		}
		return 1;
	}

public:
	InvRef(const InventoryLocation &loc):
		m_loc(loc)
	{
	}

	~InvRef()
	{
	}

	// Creates an InvRef and leaves it on top of stack
	// Not callable from Lua; all references are created on the C side.
	static void create(lua_State *L, const InventoryLocation &loc)
	{
		InvRef *o = new InvRef(loc);
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
	}
	static void createPlayer(lua_State *L, Player *player)
	{
		InventoryLocation loc;
		loc.setPlayer(player->getName());
		create(L, loc);
	}
	static void createNodeMeta(lua_State *L, v3s16 p)
	{
		InventoryLocation loc;
		loc.setNodeMeta(p);
		create(L, loc);
	}

	static void Register(lua_State *L)
	{
		lua_newtable(L);
		int methodtable = lua_gettop(L);
		luaL_newmetatable(L, className);
		int metatable = lua_gettop(L);

		lua_pushliteral(L, "__metatable");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);  // hide metatable from Lua getmetatable()

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, gc_object);
		lua_settable(L, metatable);

		lua_pop(L, 1);  // drop metatable

		luaL_openlib(L, 0, methods, 0);  // fill methodtable
		lua_pop(L, 1);  // drop methodtable

		// Cannot be created from Lua
		//lua_register(L, className, create_object);
	}
};
const char InvRef::className[] = "InvRef";
const luaL_reg InvRef::methods[] = {
	method(InvRef, get_size),
	method(InvRef, set_size),
	method(InvRef, get_stack),
	method(InvRef, set_stack),
	method(InvRef, get_list),
	method(InvRef, set_list),
	method(InvRef, autoinsert_stack),
	method(InvRef, autoinsert_stackstring),
	{0,0}
};

/*
	NodeMetaRef
*/

class NodeMetaRef
{
private:
	v3s16 m_p;
	ServerEnvironment *m_env;

	static const char className[];
	static const luaL_reg methods[];

	static NodeMetaRef *checkobject(lua_State *L, int narg)
	{
		luaL_checktype(L, narg, LUA_TUSERDATA);
		void *ud = luaL_checkudata(L, narg, className);
		if(!ud) luaL_typerror(L, narg, className);
		return *(NodeMetaRef**)ud;  // unbox pointer
	}
	
	static NodeMetadata* getmeta(NodeMetaRef *ref)
	{
		NodeMetadata *meta = ref->m_env->getMap().getNodeMetadata(ref->m_p);
		return meta;
	}

	/*static IGenericNodeMetadata* getgenericmeta(NodeMetaRef *ref)
	{
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL)
			return NULL;
		if(meta->typeId() != NODEMETA_GENERIC)
			return NULL;
		return (IGenericNodeMetadata*)meta;
	}*/

	static void reportMetadataChange(NodeMetaRef *ref)
	{
		// Inform other things that the metadata has changed
		v3s16 blockpos = getNodeBlockPos(ref->m_p);
		MapEditEvent event;
		event.type = MEET_BLOCK_NODE_METADATA_CHANGED;
		event.p = blockpos;
		ref->m_env->getMap().dispatchEvent(&event);
		// Set the block to be saved
		MapBlock *block = ref->m_env->getMap().getBlockNoCreateNoEx(blockpos);
		if(block)
			block->raiseModified(MOD_STATE_WRITE_NEEDED,
					"NodeMetaRef::reportMetadataChange");
	}
	
	// Exported functions
	
	// garbage collector
	static int gc_object(lua_State *L) {
		NodeMetaRef *o = *(NodeMetaRef **)(lua_touserdata(L, 1));
		delete o;
		return 0;
	}

	// get_type(self)
	static int l_get_type(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL){
			lua_pushnil(L);
			return 1;
		}
		// Do it
		lua_pushstring(L, meta->typeName());
		return 1;
	}

	// allows_text_input(self)
	static int l_allows_text_input(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		lua_pushboolean(L, meta->allowsTextInput());
		return 1;
	}

	// set_text(self, text)
	static int l_set_text(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string text = luaL_checkstring(L, 2);
		meta->setText(text);
		reportMetadataChange(ref);
		return 0;
	}

	// get_text(self)
	static int l_get_text(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string text = meta->getText();
		lua_pushstring(L, text.c_str());
		return 1;
	}

	// get_owner(self)
	static int l_get_owner(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string owner = meta->getOwner();
		lua_pushstring(L, owner.c_str());
		return 1;
	}

	/* IGenericNodeMetadata interface */
	
	// set_infotext(self, text)
	static int l_set_infotext(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string text = luaL_checkstring(L, 2);
		meta->setInfoText(text);
		reportMetadataChange(ref);
		return 0;
	}

	// get_inventory(self)
	static int l_get_inventory(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		InvRef::createNodeMeta(L, ref->m_p);
		return 1;
	}

	// deprecated: inventory_set_list(self, name, {item1, item2, ...})
	static int l_inventory_set_list(lua_State *L)
	{
		infostream<<"Deprecated: inventory_set_list"<<std::endl;
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		Inventory *inv = meta->getInventory();
		const char *name = luaL_checkstring(L, 2);
		inventory_set_list_from_lua(inv, name, L, 3,
				ref->m_env->getGameDef());
		reportMetadataChange(ref);
		return 0;
	}

	// deprecated: inventory_get_list(self, name)
	static int l_inventory_get_list(lua_State *L)
	{
		infostream<<"Deprecated: inventory_get_list"<<std::endl;
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		Inventory *inv = meta->getInventory();
		const char *name = luaL_checkstring(L, 2);
		inventory_get_list_to_lua(inv, name, L);
		return 1;
	}

	// set_inventory_draw_spec(self, text)
	static int l_set_inventory_draw_spec(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string text = luaL_checkstring(L, 2);
		meta->setInventoryDrawSpec(text);
		reportMetadataChange(ref);
		return 0;
	}

	// set_allow_text_input(self, text)
	static int l_set_allow_text_input(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		bool b = lua_toboolean(L, 2);
		meta->setAllowTextInput(b);
		reportMetadataChange(ref);
		return 0;
	}

	// set_allow_removal(self, text)
	static int l_set_allow_removal(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		bool b = lua_toboolean(L, 2);
		meta->setRemovalDisabled(!b);
		reportMetadataChange(ref);
		return 0;
	}

	// set_enforce_owner(self, text)
	static int l_set_enforce_owner(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		bool b = lua_toboolean(L, 2);
		meta->setEnforceOwner(b);
		reportMetadataChange(ref);
		return 0;
	}

	// is_inventory_modified(self)
	static int l_is_inventory_modified(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		lua_pushboolean(L, meta->isInventoryModified());
		return 1;
	}

	// reset_inventory_modified(self)
	static int l_reset_inventory_modified(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		meta->resetInventoryModified();
		reportMetadataChange(ref);
		return 0;
	}

	// is_text_modified(self)
	static int l_is_text_modified(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		lua_pushboolean(L, meta->isTextModified());
		return 1;
	}

	// reset_text_modified(self)
	static int l_reset_text_modified(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		meta->resetTextModified();
		reportMetadataChange(ref);
		return 0;
	}

	// set_string(self, name, var)
	static int l_set_string(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string name = luaL_checkstring(L, 2);
		size_t len = 0;
		const char *s = lua_tolstring(L, 3, &len);
		std::string str(s, len);
		meta->setString(name, str);
		reportMetadataChange(ref);
		return 0;
	}

	// get_string(self, name)
	static int l_get_string(lua_State *L)
	{
		NodeMetaRef *ref = checkobject(L, 1);
		NodeMetadata *meta = getmeta(ref);
		if(meta == NULL) return 0;
		// Do it
		std::string name = luaL_checkstring(L, 2);
		std::string str = meta->getString(name);
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

public:
	NodeMetaRef(v3s16 p, ServerEnvironment *env):
		m_p(p),
		m_env(env)
	{
	}

	~NodeMetaRef()
	{
	}

	// Creates an NodeMetaRef and leaves it on top of stack
	// Not callable from Lua; all references are created on the C side.
	static void create(lua_State *L, v3s16 p, ServerEnvironment *env)
	{
		NodeMetaRef *o = new NodeMetaRef(p, env);
		//infostream<<"NodeMetaRef::create: o="<<o<<std::endl;
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
	}

	static void Register(lua_State *L)
	{
		lua_newtable(L);
		int methodtable = lua_gettop(L);
		luaL_newmetatable(L, className);
		int metatable = lua_gettop(L);

		lua_pushliteral(L, "__metatable");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);  // hide metatable from Lua getmetatable()

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, gc_object);
		lua_settable(L, metatable);

		lua_pop(L, 1);  // drop metatable

		luaL_openlib(L, 0, methods, 0);  // fill methodtable
		lua_pop(L, 1);  // drop methodtable

		// Cannot be created from Lua
		//lua_register(L, className, create_object);
	}
};
const char NodeMetaRef::className[] = "NodeMetaRef";
const luaL_reg NodeMetaRef::methods[] = {
	method(NodeMetaRef, get_type),
	method(NodeMetaRef, allows_text_input),
	method(NodeMetaRef, set_text),
	method(NodeMetaRef, get_text),
	method(NodeMetaRef, get_owner),
	method(NodeMetaRef, set_infotext),
	method(NodeMetaRef, get_inventory),
	method(NodeMetaRef, inventory_set_list), // deprecated
	method(NodeMetaRef, inventory_get_list), // deprecated
	method(NodeMetaRef, set_inventory_draw_spec),
	method(NodeMetaRef, set_allow_text_input),
	method(NodeMetaRef, set_allow_removal),
	method(NodeMetaRef, set_enforce_owner),
	method(NodeMetaRef, is_inventory_modified),
	method(NodeMetaRef, reset_inventory_modified),
	method(NodeMetaRef, is_text_modified),
	method(NodeMetaRef, reset_text_modified),
	method(NodeMetaRef, set_string),
	method(NodeMetaRef, get_string),
	{0,0}
};

/*
	ObjectRef
*/

class ObjectRef
{
private:
	ServerActiveObject *m_object;

	static const char className[];
	static const luaL_reg methods[];

	static ObjectRef *checkobject(lua_State *L, int narg)
	{
		luaL_checktype(L, narg, LUA_TUSERDATA);
		void *ud = luaL_checkudata(L, narg, className);
		if(!ud) luaL_typerror(L, narg, className);
		return *(ObjectRef**)ud;  // unbox pointer
	}
	
	static ServerActiveObject* getobject(ObjectRef *ref)
	{
		ServerActiveObject *co = ref->m_object;
		return co;
	}
	
	static LuaEntitySAO* getluaobject(ObjectRef *ref)
	{
		ServerActiveObject *obj = getobject(ref);
		if(obj == NULL)
			return NULL;
		if(obj->getType() != ACTIVEOBJECT_TYPE_LUAENTITY)
			return NULL;
		return (LuaEntitySAO*)obj;
	}
	
	static ServerRemotePlayer* getplayer(ObjectRef *ref)
	{
		ServerActiveObject *obj = getobject(ref);
		if(obj == NULL)
			return NULL;
		if(obj->getType() != ACTIVEOBJECT_TYPE_PLAYER)
			return NULL;
		return static_cast<ServerRemotePlayer*>(obj);
	}
	
	// Exported functions
	
	// garbage collector
	static int gc_object(lua_State *L) {
		ObjectRef *o = *(ObjectRef **)(lua_touserdata(L, 1));
		//infostream<<"ObjectRef::gc_object: o="<<o<<std::endl;
		delete o;
		return 0;
	}

	// remove(self)
	static int l_remove(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		infostream<<"ObjectRef::l_remove(): id="<<co->getId()<<std::endl;
		co->m_removed = true;
		return 0;
	}
	
	// getpos(self)
	// returns: {x=num, y=num, z=num}
	static int l_getpos(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		v3f pos = co->getBasePosition() / BS;
		lua_newtable(L);
		lua_pushnumber(L, pos.X);
		lua_setfield(L, -2, "x");
		lua_pushnumber(L, pos.Y);
		lua_setfield(L, -2, "y");
		lua_pushnumber(L, pos.Z);
		lua_setfield(L, -2, "z");
		return 1;
	}
	
	// setpos(self, pos)
	static int l_setpos(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		//LuaEntitySAO *co = getluaobject(ref);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// Do it
		co->setPos(pos);
		// Move player if applicable
		ServerRemotePlayer *player = getplayer(ref);
		if(player != NULL)
			get_server(L)->SendMovePlayer(player);
		return 0;
	}
	
	// moveto(self, pos, continuous=false)
	static int l_moveto(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		//LuaEntitySAO *co = getluaobject(ref);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// continuous
		bool continuous = lua_toboolean(L, 3);
		// Do it
		co->moveTo(pos, continuous);
		return 0;
	}

	// punch(self, puncher); puncher = an another ObjectRef
	static int l_punch(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ObjectRef *ref2 = checkobject(L, 2);
		ServerActiveObject *co = getobject(ref);
		ServerActiveObject *co2 = getobject(ref2);
		if(co == NULL) return 0;
		if(co2 == NULL) return 0;
		// Do it
		co->punch(co2);
		return 0;
	}

	// right_click(self, clicker); clicker = an another ObjectRef
	static int l_right_click(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ObjectRef *ref2 = checkobject(L, 2);
		ServerActiveObject *co = getobject(ref);
		ServerActiveObject *co2 = getobject(ref2);
		if(co == NULL) return 0;
		if(co2 == NULL) return 0;
		// Do it
		co->rightClick(co2);
		return 0;
	}

	// get_wield_digging_properties(self)
	static int l_get_wield_digging_properties(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// Do it
		ToolDiggingProperties prop;
		co->getWieldDiggingProperties(&prop);
		push_tool_digging_properties(L, prop);
		return 1;
	}

	// damage_wielded_item(self, amount)
	static int l_damage_wielded_item(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// Do it
		int amount = lua_tonumber(L, 2);
		co->damageWieldedItem(amount);
		return 0;
	}

	// add_to_inventory(self, itemstring)
	// returns: true if item was added, (false, "reason") otherwise
	static int l_add_to_inventory(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		luaL_checkstring(L, 2);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// itemstring
		const char *itemstring = luaL_checkstring(L, 2);
		infostream<<"ObjectRef::l_add_to_inventory(): id="<<co->getId()
				<<" itemstring=\""<<itemstring<<"\""<<std::endl;
		// Do it
		std::istringstream is(itemstring, std::ios::binary);
		ServerEnvironment *env = co->getEnv();
		assert(env);
		IGameDef *gamedef = env->getGameDef();
		try{
			InventoryItem *item = InventoryItem::deSerialize(is, gamedef);
			if(item->getCount() == 0)
				item->setCount(1);
			bool added = co->addToInventory(item);
			// Return
			lua_pushboolean(L, added);
			if(!added)
				lua_pushstring(L, "failed to add item");
			return 2;
		} catch(SerializationError &e){
			// Return
			lua_pushboolean(L, false);
			lua_pushstring(L, (std::string("Invalid item: ")
					+ e.what()).c_str());
			return 2;
		}
	}

	// add_to_inventory_later(self, itemstring)
	// returns: nil
	static int l_add_to_inventory_later(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		luaL_checkstring(L, 2);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		// itemstring
		const char *itemstring = luaL_checkstring(L, 2);
		infostream<<"ObjectRef::l_add_to_inventory_later(): id="<<co->getId()
				<<" itemstring=\""<<itemstring<<"\""<<std::endl;
		// Do it
		std::istringstream is(itemstring, std::ios::binary);
		ServerEnvironment *env = co->getEnv();
		assert(env);
		IGameDef *gamedef = env->getGameDef();
		InventoryItem *item = InventoryItem::deSerialize(is, gamedef);
		infostream<<"item="<<env<<std::endl;
		co->addToInventoryLater(item);
		// Return
		return 0;
	}

	// set_hp(self, hp)
	// hp = number of hitpoints (2 * number of hearts)
	// returns: nil
	static int l_set_hp(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		luaL_checknumber(L, 2);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		int hp = lua_tonumber(L, 2);
		infostream<<"ObjectRef::l_set_hp(): id="<<co->getId()
				<<" hp="<<hp<<std::endl;
		// Do it
		co->setHP(hp);
		// Return
		return 0;
	}

	// get_hp(self)
	// returns: number of hitpoints (2 * number of hearts)
	// 0 if not applicable to this type of object
	static int l_get_hp(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerActiveObject *co = getobject(ref);
		if(co == NULL) return 0;
		int hp = co->getHP();
		infostream<<"ObjectRef::l_get_hp(): id="<<co->getId()
				<<" hp="<<hp<<std::endl;
		// Return
		lua_pushnumber(L, hp);
		return 1;
	}

	/* LuaEntitySAO-only */

	// setvelocity(self, {x=num, y=num, z=num})
	static int l_setvelocity(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// Do it
		co->setVelocity(pos);
		return 0;
	}
	
	// getvelocity(self)
	static int l_getvelocity(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		v3f v = co->getVelocity();
		pushFloatPos(L, v);
		return 1;
	}
	
	// setacceleration(self, {x=num, y=num, z=num})
	static int l_setacceleration(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// Do it
		co->setAcceleration(pos);
		return 0;
	}
	
	// getacceleration(self)
	static int l_getacceleration(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		v3f v = co->getAcceleration();
		pushFloatPos(L, v);
		return 1;
	}
	
	// setyaw(self, radians)
	static int l_setyaw(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// pos
		float yaw = luaL_checknumber(L, 2) * core::RADTODEG;
		// Do it
		co->setYaw(yaw);
		return 0;
	}
	
	// getyaw(self)
	static int l_getyaw(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		float yaw = co->getYaw() * core::DEGTORAD;
		lua_pushnumber(L, yaw);
		return 1;
	}
	
	// settexturemod(self, mod)
	static int l_settexturemod(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		std::string mod = luaL_checkstring(L, 2);
		co->setTextureMod(mod);
		return 0;
	}
	
	// setsprite(self, p={x=0,y=0}, num_frames=1, framelength=0.2,
	//           select_horiz_by_yawpitch=false)
	static int l_setsprite(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		v2s16 p(0,0);
		if(!lua_isnil(L, 2))
			p = read_v2s16(L, 2);
		int num_frames = 1;
		if(!lua_isnil(L, 3))
			num_frames = lua_tonumber(L, 3);
		float framelength = 0.2;
		if(!lua_isnil(L, 4))
			framelength = lua_tonumber(L, 4);
		bool select_horiz_by_yawpitch = false;
		if(!lua_isnil(L, 5))
			select_horiz_by_yawpitch = lua_toboolean(L, 5);
		co->setSprite(p, num_frames, framelength, select_horiz_by_yawpitch);
		return 0;
	}

	// DEPRECATED
	// get_entity_name(self)
	static int l_get_entity_name(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		std::string name = co->getName();
		lua_pushstring(L, name.c_str());
		return 1;
	}
	
	// get_luaentity(self)
	static int l_get_luaentity(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		LuaEntitySAO *co = getluaobject(ref);
		if(co == NULL) return 0;
		// Do it
		luaentity_get(L, co->getId());
		return 1;
	}
	
	/* Player-only */
	
	// get_player_name(self)
	static int l_get_player_name(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL){
			lua_pushnil(L);
			return 1;
		}
		// Do it
		lua_pushstring(L, player->getName());
		return 1;
	}
	
	// get_inventory(self)
	static int l_get_inventory(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		InvRef::createPlayer(L, player);
		return 1;
	}

	// deprecated: inventory_set_list(self, name, {item1, item2, ...})
	static int l_inventory_set_list(lua_State *L)
	{
		infostream<<"Deprecated: inventory_set_list"<<std::endl;
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		const char *name = luaL_checkstring(L, 2);
		// Do it
		inventory_set_list_from_lua(&player->inventory, name, L, 3,
				player->getEnv()->getGameDef(), PLAYER_INVENTORY_SIZE);
		player->m_inventory_not_sent = true;
		return 0;
	}

	// deprecated: inventory_get_list(self, name)
	static int l_inventory_get_list(lua_State *L)
	{
		infostream<<"Deprecated: inventory_get_list"<<std::endl;
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		const char *name = luaL_checkstring(L, 2);
		// Do it
		inventory_get_list_to_lua(&player->inventory, name, L);
		return 1;
	}

	// get_wielded_itemstring(self)
	static int l_get_wielded_itemstring(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		InventoryItem *item = player->getWieldedItem();
		if(item == NULL){
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, item->getItemString().c_str());
		return 1;
	}

	// get_wielded_item(self)
	static int l_get_wielded_item(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		InventoryItem *item0 = player->getWieldedItem();
		push_stack_item(L, item0);
		return 1;
	}

	// get_look_dir(self)
	static int l_get_look_dir(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		float pitch = player->getRadPitch();
		float yaw = player->getRadYaw();
		v3f v(cos(pitch)*cos(yaw), sin(pitch), cos(pitch)*sin(yaw));
		push_v3f(L, v);
		return 1;
	}

	// get_look_pitch(self)
	static int l_get_look_pitch(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		lua_pushnumber(L, player->getRadPitch());
		return 1;
	}

	// get_look_yaw(self)
	static int l_get_look_yaw(lua_State *L)
	{
		ObjectRef *ref = checkobject(L, 1);
		ServerRemotePlayer *player = getplayer(ref);
		if(player == NULL) return 0;
		// Do it
		lua_pushnumber(L, player->getRadYaw());
		return 1;
	}

public:
	ObjectRef(ServerActiveObject *object):
		m_object(object)
	{
		//infostream<<"ObjectRef created for id="<<m_object->getId()<<std::endl;
	}

	~ObjectRef()
	{
		/*if(m_object)
			infostream<<"ObjectRef destructing for id="
					<<m_object->getId()<<std::endl;
		else
			infostream<<"ObjectRef destructing for id=unknown"<<std::endl;*/
	}

	// Creates an ObjectRef and leaves it on top of stack
	// Not callable from Lua; all references are created on the C side.
	static void create(lua_State *L, ServerActiveObject *object)
	{
		ObjectRef *o = new ObjectRef(object);
		//infostream<<"ObjectRef::create: o="<<o<<std::endl;
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
	}

	static void set_null(lua_State *L)
	{
		ObjectRef *o = checkobject(L, -1);
		o->m_object = NULL;
	}
	
	static void Register(lua_State *L)
	{
		lua_newtable(L);
		int methodtable = lua_gettop(L);
		luaL_newmetatable(L, className);
		int metatable = lua_gettop(L);

		lua_pushliteral(L, "__metatable");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);  // hide metatable from Lua getmetatable()

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, gc_object);
		lua_settable(L, metatable);

		lua_pop(L, 1);  // drop metatable

		luaL_openlib(L, 0, methods, 0);  // fill methodtable
		lua_pop(L, 1);  // drop methodtable

		// Cannot be created from Lua
		//lua_register(L, className, create_object);
	}
};
const char ObjectRef::className[] = "ObjectRef";
const luaL_reg ObjectRef::methods[] = {
	// ServerActiveObject
	method(ObjectRef, remove),
	method(ObjectRef, getpos),
	method(ObjectRef, setpos),
	method(ObjectRef, moveto),
	method(ObjectRef, punch),
	method(ObjectRef, right_click),
	method(ObjectRef, get_wield_digging_properties),
	method(ObjectRef, damage_wielded_item),
	method(ObjectRef, add_to_inventory),
	method(ObjectRef, add_to_inventory_later),
	method(ObjectRef, set_hp),
	method(ObjectRef, get_hp),
	// LuaEntitySAO-only
	method(ObjectRef, setvelocity),
	method(ObjectRef, getvelocity),
	method(ObjectRef, setacceleration),
	method(ObjectRef, getacceleration),
	method(ObjectRef, setyaw),
	method(ObjectRef, getyaw),
	method(ObjectRef, settexturemod),
	method(ObjectRef, setsprite),
	method(ObjectRef, get_entity_name),
	method(ObjectRef, get_luaentity),
	// Player-only
	method(ObjectRef, get_player_name),
	method(ObjectRef, get_inventory),
	method(ObjectRef, inventory_set_list), // deprecated
	method(ObjectRef, inventory_get_list), // deprecated
	method(ObjectRef, get_wielded_itemstring),
	method(ObjectRef, get_wielded_item),
	method(ObjectRef, get_look_dir),
	method(ObjectRef, get_look_pitch),
	method(ObjectRef, get_look_yaw),
	{0,0}
};

// Creates a new anonymous reference if id=0
static void objectref_get_or_create(lua_State *L,
		ServerActiveObject *cobj)
{
	if(cobj->getId() == 0){
		ObjectRef::create(L, cobj);
	} else {
		objectref_get(L, cobj->getId());
	}
}

/*
	EnvRef
*/

class EnvRef
{
private:
	ServerEnvironment *m_env;

	static const char className[];
	static const luaL_reg methods[];

	static EnvRef *checkobject(lua_State *L, int narg)
	{
		luaL_checktype(L, narg, LUA_TUSERDATA);
		void *ud = luaL_checkudata(L, narg, className);
		if(!ud) luaL_typerror(L, narg, className);
		return *(EnvRef**)ud;  // unbox pointer
	}
	
	// Exported functions

	// EnvRef:add_node(pos, node)
	// pos = {x=num, y=num, z=num}
	static int l_add_node(lua_State *L)
	{
		//infostream<<"EnvRef::l_add_node()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3s16 pos = read_v3s16(L, 2);
		// content
		MapNode n = readnode(L, 3, env->getGameDef()->ndef());
		// Do it
		bool succeeded = env->getMap().addNodeWithEvent(pos, n);
		lua_pushboolean(L, succeeded);
		return 1;
	}

	// EnvRef:remove_node(pos)
	// pos = {x=num, y=num, z=num}
	static int l_remove_node(lua_State *L)
	{
		//infostream<<"EnvRef::l_remove_node()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3s16 pos = read_v3s16(L, 2);
		// Do it
		bool succeeded = env->getMap().removeNodeWithEvent(pos);
		lua_pushboolean(L, succeeded);
		return 1;
	}

	// EnvRef:get_node(pos)
	// pos = {x=num, y=num, z=num}
	static int l_get_node(lua_State *L)
	{
		//infostream<<"EnvRef::l_get_node()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3s16 pos = read_v3s16(L, 2);
		// Do it
		MapNode n = env->getMap().getNodeNoEx(pos);
		// Return node
		pushnode(L, n, env->getGameDef()->ndef());
		return 1;
	}

	// EnvRef:get_node_or_nil(pos)
	// pos = {x=num, y=num, z=num}
	static int l_get_node_or_nil(lua_State *L)
	{
		//infostream<<"EnvRef::l_get_node()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3s16 pos = read_v3s16(L, 2);
		// Do it
		try{
			MapNode n = env->getMap().getNode(pos);
			// Return node
			pushnode(L, n, env->getGameDef()->ndef());
			return 1;
		} catch(InvalidPositionException &e)
		{
			lua_pushnil(L);
			return 1;
		}
	}

	// EnvRef:get_node_light(pos, timeofday)
	// pos = {x=num, y=num, z=num}
	// timeofday: nil = current time, 0 = night, 0.5 = day
	static int l_get_node_light(lua_State *L)
	{
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// Do it
		v3s16 pos = read_v3s16(L, 2);
		u32 time_of_day = env->getTimeOfDay();
		if(lua_isnumber(L, 3))
			time_of_day = 24000.0 * lua_tonumber(L, 3);
		time_of_day %= 24000;
		u32 dnr = time_to_daynight_ratio(time_of_day);
		MapNode n = env->getMap().getNodeNoEx(pos);
		try{
			MapNode n = env->getMap().getNode(pos);
			INodeDefManager *ndef = env->getGameDef()->ndef();
			lua_pushinteger(L, n.getLightBlend(dnr, ndef));
			return 1;
		} catch(InvalidPositionException &e)
		{
			lua_pushnil(L);
			return 1;
		}
	}

	// EnvRef:add_entity(pos, entityname)
	// pos = {x=num, y=num, z=num}
	static int l_add_entity(lua_State *L)
	{
		//infostream<<"EnvRef::l_add_entity()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// content
		const char *name = luaL_checkstring(L, 3);
		// Do it
		ServerActiveObject *obj = new LuaEntitySAO(env, pos, name, "");
		int objectid = env->addActiveObject(obj);
		// If failed to add, return nothing (reads as nil)
		if(objectid == 0)
			return 0;
		// Return ObjectRef
		objectref_get_or_create(L, obj);
		return 1;
	}

	// EnvRef:add_item(pos, inventorystring)
	// pos = {x=num, y=num, z=num}
	static int l_add_item(lua_State *L)
	{
		infostream<<"EnvRef::l_add_item()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// inventorystring
		const char *inventorystring = luaL_checkstring(L, 3);
		// Do it
		ServerActiveObject *obj = new ItemSAO(env, pos, inventorystring);
		env->addActiveObject(obj);
		return 0;
	}

	// EnvRef:add_rat(pos)
	// pos = {x=num, y=num, z=num}
	static int l_add_rat(lua_State *L)
	{
		infostream<<"EnvRef::l_add_rat()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// Do it
		ServerActiveObject *obj = new RatSAO(env, pos);
		env->addActiveObject(obj);
		return 0;
	}

	// EnvRef:add_firefly(pos)
	// pos = {x=num, y=num, z=num}
	static int l_add_firefly(lua_State *L)
	{
		infostream<<"EnvRef::l_add_firefly()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// pos
		v3f pos = checkFloatPos(L, 2);
		// Do it
		ServerActiveObject *obj = new FireflySAO(env, pos);
		env->addActiveObject(obj);
		return 0;
	}

	// EnvRef:get_meta(pos)
	static int l_get_meta(lua_State *L)
	{
		//infostream<<"EnvRef::l_get_meta()"<<std::endl;
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// Do it
		v3s16 p = read_v3s16(L, 2);
		NodeMetaRef::create(L, p, env);
		return 1;
	}

	// EnvRef:get_player_by_name(name)
	static int l_get_player_by_name(lua_State *L)
	{
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// Do it
		const char *name = luaL_checkstring(L, 2);
		ServerRemotePlayer *player =
				static_cast<ServerRemotePlayer*>(env->getPlayer(name));
		if(player == NULL){
			lua_pushnil(L);
			return 1;
		}
		// Put player on stack
		objectref_get_or_create(L, player);
		return 1;
	}

	// EnvRef:get_objects_inside_radius(pos, radius)
	static int l_get_objects_inside_radius(lua_State *L)
	{
		// Get the table insert function
		lua_getglobal(L, "table");
		lua_getfield(L, -1, "insert");
		int table_insert = lua_gettop(L);
		// Get environemnt
		EnvRef *o = checkobject(L, 1);
		ServerEnvironment *env = o->m_env;
		if(env == NULL) return 0;
		// Do it
		v3f pos = checkFloatPos(L, 2);
		float radius = luaL_checknumber(L, 3) * BS;
		std::set<u16> ids = env->getObjectsInsideRadius(pos, radius);
		lua_newtable(L);
		int table = lua_gettop(L);
		for(std::set<u16>::const_iterator
				i = ids.begin(); i != ids.end(); i++){
			ServerActiveObject *obj = env->getActiveObject(*i);
			// Insert object reference into table
			lua_pushvalue(L, table_insert);
			lua_pushvalue(L, table);
			objectref_get_or_create(L, obj);
			if(lua_pcall(L, 2, 0, 0))
				script_error(L, "error: %s", lua_tostring(L, -1));
		}
		return 1;
	}

	static int gc_object(lua_State *L) {
		EnvRef *o = *(EnvRef **)(lua_touserdata(L, 1));
		delete o;
		return 0;
	}

public:
	EnvRef(ServerEnvironment *env):
		m_env(env)
	{
		infostream<<"EnvRef created"<<std::endl;
	}

	~EnvRef()
	{
		infostream<<"EnvRef destructing"<<std::endl;
	}

	// Creates an EnvRef and leaves it on top of stack
	// Not callable from Lua; all references are created on the C side.
	static void create(lua_State *L, ServerEnvironment *env)
	{
		EnvRef *o = new EnvRef(env);
		//infostream<<"EnvRef::create: o="<<o<<std::endl;
		*(void **)(lua_newuserdata(L, sizeof(void *))) = o;
		luaL_getmetatable(L, className);
		lua_setmetatable(L, -2);
	}

	static void set_null(lua_State *L)
	{
		EnvRef *o = checkobject(L, -1);
		o->m_env = NULL;
	}
	
	static void Register(lua_State *L)
	{
		lua_newtable(L);
		int methodtable = lua_gettop(L);
		luaL_newmetatable(L, className);
		int metatable = lua_gettop(L);

		lua_pushliteral(L, "__metatable");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);  // hide metatable from Lua getmetatable()

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, methodtable);
		lua_settable(L, metatable);

		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, gc_object);
		lua_settable(L, metatable);

		lua_pop(L, 1);  // drop metatable

		luaL_openlib(L, 0, methods, 0);  // fill methodtable
		lua_pop(L, 1);  // drop methodtable

		// Cannot be created from Lua
		//lua_register(L, className, create_object);
	}
};
const char EnvRef::className[] = "EnvRef";
const luaL_reg EnvRef::methods[] = {
	method(EnvRef, add_node),
	method(EnvRef, remove_node),
	method(EnvRef, get_node),
	method(EnvRef, get_node_or_nil),
	method(EnvRef, get_node_light),
	method(EnvRef, add_entity),
	method(EnvRef, add_item),
	method(EnvRef, add_rat),
	method(EnvRef, add_firefly),
	method(EnvRef, get_meta),
	method(EnvRef, get_player_by_name),
	method(EnvRef, get_objects_inside_radius),
	{0,0}
};

/*
	Global functions
*/

static int l_register_nodedef_defaults(lua_State *L)
{
	luaL_checktype(L, 1, LUA_TTABLE);

	lua_pushvalue(L, 1); // Explicitly put parameter 1 on top of stack
	lua_setfield(L, LUA_REGISTRYINDEX, "minetest_nodedef_default");

	return 0;
}

// Register new object prototype
// register_entity(name, prototype)
static int l_register_entity(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	check_modname_prefix(L, name);
	//infostream<<"register_entity: "<<name<<std::endl;
	luaL_checktype(L, 2, LUA_TTABLE);

	// Get minetest.registered_entities
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_entities");
	luaL_checktype(L, -1, LUA_TTABLE);
	int registered_entities = lua_gettop(L);
	lua_pushvalue(L, 2); // Object = param 2 -> stack top
	// registered_entities[name] = object
	lua_setfield(L, registered_entities, name.c_str());
	
	// Get registered object to top of stack
	lua_pushvalue(L, 2);

	// Set name field
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "name");
	
	// Set __index to point to itself
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	// Set metatable.__index = metatable
	luaL_getmetatable(L, "minetest.entity");
	lua_pushvalue(L, -1); // duplicate metatable
	lua_setfield(L, -2, "__index");
	// Set object metatable
	lua_setmetatable(L, -2);

	return 0; /* number of results */
}

class LuaABM : public ActiveBlockModifier
{
private:
	lua_State *m_lua;
	int m_id;

	std::set<std::string> m_trigger_contents;
	std::set<std::string> m_required_neighbors;
	float m_trigger_interval;
	u32 m_trigger_chance;
public:
	LuaABM(lua_State *L, int id,
			const std::set<std::string> &trigger_contents,
			const std::set<std::string> &required_neighbors,
			float trigger_interval, u32 trigger_chance):
		m_lua(L),
		m_id(id),
		m_trigger_contents(trigger_contents),
		m_required_neighbors(required_neighbors),
		m_trigger_interval(trigger_interval),
		m_trigger_chance(trigger_chance)
	{
	}
	virtual std::set<std::string> getTriggerContents()
	{
		return m_trigger_contents;
	}
	virtual std::set<std::string> getRequiredNeighbors()
	{
		return m_required_neighbors;
	}
	virtual float getTriggerInterval()
	{
		return m_trigger_interval;
	}
	virtual u32 getTriggerChance()
	{
		return m_trigger_chance;
	}
	virtual void trigger(ServerEnvironment *env, v3s16 p, MapNode n,
			u32 active_object_count, u32 active_object_count_wider)
	{
		lua_State *L = m_lua;
	
		realitycheck(L);
		assert(lua_checkstack(L, 20));
		StackUnroller stack_unroller(L);

		// Get minetest.registered_abms
		lua_getglobal(L, "minetest");
		lua_getfield(L, -1, "registered_abms");
		luaL_checktype(L, -1, LUA_TTABLE);
		int registered_abms = lua_gettop(L);

		// Get minetest.registered_abms[m_id]
		lua_pushnumber(L, m_id);
		lua_gettable(L, registered_abms);
		if(lua_isnil(L, -1))
			assert(0);
		
		// Call action
		luaL_checktype(L, -1, LUA_TTABLE);
		lua_getfield(L, -1, "action");
		luaL_checktype(L, -1, LUA_TFUNCTION);
		push_v3s16(L, p);
		pushnode(L, n, env->getGameDef()->ndef());
		lua_pushnumber(L, active_object_count);
		lua_pushnumber(L, active_object_count_wider);
		if(lua_pcall(L, 4, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
	}
};

// register_abm({...})
static int l_register_abm(lua_State *L)
{
	//infostream<<"register_abm"<<std::endl;
	luaL_checktype(L, 1, LUA_TTABLE);

	// Get minetest.registered_abms
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_abms");
	luaL_checktype(L, -1, LUA_TTABLE);
	int registered_abms = lua_gettop(L);

	int id = 1;
	// Find free id
	for(;;){
		lua_pushnumber(L, id);
		lua_gettable(L, registered_abms);
		if(lua_isnil(L, -1))
			break;
		lua_pop(L, 1);
		id++;
	}
	lua_pop(L, 1);

	infostream<<"register_abm: id="<<id<<std::endl;

	// registered_abms[id] = spec
	lua_pushnumber(L, id);
	lua_pushvalue(L, 1);
	lua_settable(L, registered_abms);
	
	return 0; /* number of results */
}

// register_tool(name, {lots of stuff})
static int l_register_tool(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	check_modname_prefix(L, name);
	//infostream<<"register_tool: "<<name<<std::endl;
	luaL_checktype(L, 2, LUA_TTABLE);
	int table = 2;

	// Get the writable tool definition manager from the server
	IWritableToolDefManager *tooldef =
			get_server(L)->getWritableToolDefManager();
	
	ToolDefinition def = read_tool_definition(L, table);

	tooldef->registerTool(name, def);
	return 0; /* number of results */
}

// register_craftitem(name, {lots of stuff})
static int l_register_craftitem(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	check_modname_prefix(L, name);
	//infostream<<"register_craftitem: "<<name<<std::endl;
	luaL_checktype(L, 2, LUA_TTABLE);
	int table = 2;

	// Get the writable CraftItem definition manager from the server
	IWritableCraftItemDefManager *craftitemdef =
			get_server(L)->getWritableCraftItemDefManager();
	
	// Check if on_drop is defined
	lua_getfield(L, table, "on_drop");
	bool got_on_drop = !lua_isnil(L, -1);
	lua_pop(L, 1);

	// Check if on_use is defined
	lua_getfield(L, table, "on_use");
	bool got_on_use = !lua_isnil(L, -1);
	lua_pop(L, 1);

	CraftItemDefinition def;
	
	getstringfield(L, table, "image", def.imagename);
	getstringfield(L, table, "cookresult_itemstring", def.cookresult_item);
	getfloatfield(L, table, "furnace_cooktime", def.furnace_cooktime);
	getfloatfield(L, table, "furnace_burntime", def.furnace_burntime);
	def.usable = getboolfield_default(L, table, "usable", got_on_use);
	getboolfield(L, table, "liquids_pointable", def.liquids_pointable);
	def.dropcount = getintfield_default(L, table, "dropcount", def.dropcount);
	def.stack_max = getintfield_default(L, table, "stack_max", def.stack_max);

	// If an on_drop callback is defined, force dropcount to 1
	if (got_on_drop)
		def.dropcount = 1;

	// Register it
	craftitemdef->registerCraftItem(name, def);

	lua_pushvalue(L, table);
	scriptapi_add_craftitem(L, name.c_str());

	return 0; /* number of results */
}

// register_node(name, {lots of stuff})
static int l_register_node(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	check_modname_prefix(L, name);
	//infostream<<"register_node: "<<name<<std::endl;
	luaL_checktype(L, 2, LUA_TTABLE);
	int nodedef_table = 2;

	// Get the writable node definition manager from the server
	IWritableNodeDefManager *nodedef =
			get_server(L)->getWritableNodeDefManager();
	
	// Get default node definition from registry
	lua_getfield(L, LUA_REGISTRYINDEX, "minetest_nodedef_default");
	int nodedef_default = lua_gettop(L);

	/*
		Add to minetest.registered_nodes with default as metatable
	*/
	
	// Get the node definition table given as parameter
	lua_pushvalue(L, nodedef_table);

	// Set __index to point to itself
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");

	// Set nodedef_default as metatable for the definition
	lua_pushvalue(L, nodedef_default);
	lua_setmetatable(L, nodedef_table);
	
	// minetest.registered_nodes[name] = nodedef
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_nodes");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushstring(L, name.c_str());
	lua_pushvalue(L, nodedef_table);
	lua_settable(L, -3);

	/*
		Create definition
	*/
	
	ContentFeatures f;

	// Default to getting the corresponding NodeItem when dug
	f.dug_item = std::string("NodeItem \"")+name+"\" 1";
	
	// Default to unknown_block.png as all textures
	f.setAllTextures("unknown_block.png");

	/*
		Read definiton from Lua
	*/

	f.name = name;
	
	/* Visual definition */

	f.drawtype = (NodeDrawType)getenumfield(L, nodedef_table, "drawtype", es_DrawType,
			NDT_NORMAL);
	getfloatfield(L, nodedef_table, "visual_scale", f.visual_scale);

	lua_getfield(L, nodedef_table, "tile_images");
	if(lua_istable(L, -1)){
		int table = lua_gettop(L);
		lua_pushnil(L);
		int i = 0;
		while(lua_next(L, table) != 0){
			// key at index -2 and value at index -1
			if(lua_isstring(L, -1))
				f.tname_tiles[i] = lua_tostring(L, -1);
			else
				f.tname_tiles[i] = "";
			// removes value, keeps key for next iteration
			lua_pop(L, 1);
			i++;
			if(i==6){
				lua_pop(L, 1);
				break;
			}
		}
		// Copy last value to all remaining textures
		if(i >= 1){
			std::string lastname = f.tname_tiles[i-1];
			while(i < 6){
				f.tname_tiles[i] = lastname;
				i++;
			}
		}
	}
	lua_pop(L, 1);

	getstringfield(L, nodedef_table, "inventory_image", f.tname_inventory);

	lua_getfield(L, nodedef_table, "special_materials");
	if(lua_istable(L, -1)){
		int table = lua_gettop(L);
		lua_pushnil(L);
		int i = 0;
		while(lua_next(L, table) != 0){
			// key at index -2 and value at index -1
			int smtable = lua_gettop(L);
			std::string tname = getstringfield_default(
					L, smtable, "image", "");
			bool backface_culling = getboolfield_default(
					L, smtable, "backface_culling", true);
			MaterialSpec mspec(tname, backface_culling);
			f.setSpecialMaterial(i, mspec);
			// removes value, keeps key for next iteration
			lua_pop(L, 1);
			i++;
			if(i==6){
				lua_pop(L, 1);
				break;
			}
		}
	}
	lua_pop(L, 1);

	f.alpha = getintfield_default(L, nodedef_table, "alpha", 255);

	/* Other stuff */
	
	lua_getfield(L, nodedef_table, "post_effect_color");
	if(!lua_isnil(L, -1))
		f.post_effect_color = readARGB8(L, -1);
	lua_pop(L, 1);

	f.param_type = (ContentParamType)getenumfield(L, nodedef_table, "paramtype",
			es_ContentParamType, CPT_NONE);
	
	// True for all ground-like things like stone and mud, false for eg. trees
	getboolfield(L, nodedef_table, "is_ground_content", f.is_ground_content);
	f.light_propagates = (f.param_type == CPT_LIGHT);
	warn_if_field_exists(L, nodedef_table, "light_propagates",
			"deprecated: determined from paramtype");
	getboolfield(L, nodedef_table, "sunlight_propagates", f.sunlight_propagates);
	// This is used for collision detection.
	// Also for general solidness queries.
	getboolfield(L, nodedef_table, "walkable", f.walkable);
	// Player can point to these
	getboolfield(L, nodedef_table, "pointable", f.pointable);
	// Player can dig these
	getboolfield(L, nodedef_table, "diggable", f.diggable);
	// Player can climb these
	getboolfield(L, nodedef_table, "climbable", f.climbable);
	// Player can build on these
	getboolfield(L, nodedef_table, "buildable_to", f.buildable_to);
	// If true, param2 is set to direction when placed. Used for torches.
	// NOTE: the direction format is quite inefficient and should be changed
	getboolfield(L, nodedef_table, "wall_mounted", f.wall_mounted);
	// Whether this content type often contains mineral.
	// Used for texture atlas creation.
	// Currently only enabled for CONTENT_STONE.
	getboolfield(L, nodedef_table, "often_contains_mineral", f.often_contains_mineral);
	// Inventory item string as which the node appears in inventory when dug.
	// Mineral overrides this.
	getstringfield(L, nodedef_table, "dug_item", f.dug_item);
	// Extra dug item and its rarity
	getstringfield(L, nodedef_table, "extra_dug_item", f.extra_dug_item);
	// Usual get interval for extra dug item
	getintfield(L, nodedef_table, "extra_dug_item_rarity", f.extra_dug_item_rarity);
	// Metadata name of node (eg. "furnace")
	getstringfield(L, nodedef_table, "metadata_name", f.metadata_name);
	// Whether the node is non-liquid, source liquid or flowing liquid
	f.liquid_type = (LiquidType)getenumfield(L, nodedef_table, "liquidtype",
			es_LiquidType, LIQUID_NONE);
	// If the content is liquid, this is the flowing version of the liquid.
	getstringfield(L, nodedef_table, "liquid_alternative_flowing",
			f.liquid_alternative_flowing);
	// If the content is liquid, this is the source version of the liquid.
	getstringfield(L, nodedef_table, "liquid_alternative_source",
			f.liquid_alternative_source);
	// Viscosity for fluid flow, ranging from 1 to 7, with
	// 1 giving almost instantaneous propagation and 7 being
	// the slowest possible
	f.liquid_viscosity = getintfield_default(L, nodedef_table,
			"liquid_viscosity", f.liquid_viscosity);
	// Amount of light the node emits
	f.light_source = getintfield_default(L, nodedef_table,
			"light_source", f.light_source);
	f.damage_per_second = getintfield_default(L, nodedef_table,
			"damage_per_second", f.damage_per_second);
	
	lua_getfield(L, nodedef_table, "selection_box");
	if(lua_istable(L, -1)){
		f.selection_box.type = (NodeBoxType)getenumfield(L, -1, "type",
				es_NodeBoxType, NODEBOX_REGULAR);

		lua_getfield(L, -1, "fixed");
		if(lua_istable(L, -1))
			f.selection_box.fixed = read_aabbox3df32(L, -1, BS);
		lua_pop(L, 1);

		lua_getfield(L, -1, "wall_top");
		if(lua_istable(L, -1))
			f.selection_box.wall_top = read_aabbox3df32(L, -1, BS);
		lua_pop(L, 1);

		lua_getfield(L, -1, "wall_bottom");
		if(lua_istable(L, -1))
			f.selection_box.wall_bottom = read_aabbox3df32(L, -1, BS);
		lua_pop(L, 1);

		lua_getfield(L, -1, "wall_side");
		if(lua_istable(L, -1))
			f.selection_box.wall_side = read_aabbox3df32(L, -1, BS);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	lua_getfield(L, nodedef_table, "material");
	if(lua_istable(L, -1)){
		f.material.diggability = (Diggability)getenumfield(L, -1, "diggability",
				es_Diggability, DIGGABLE_NORMAL);
		
		getfloatfield(L, -1, "constant_time", f.material.constant_time);
		getfloatfield(L, -1, "weight", f.material.weight);
		getfloatfield(L, -1, "crackiness", f.material.crackiness);
		getfloatfield(L, -1, "crumbliness", f.material.crumbliness);
		getfloatfield(L, -1, "cuttability", f.material.cuttability);
		getfloatfield(L, -1, "flammability", f.material.flammability);
	}
	lua_pop(L, 1);

	getstringfield(L, nodedef_table, "cookresult_itemstring", f.cookresult_item);
	getfloatfield(L, nodedef_table, "furnace_cooktime", f.furnace_cooktime);
	getfloatfield(L, nodedef_table, "furnace_burntime", f.furnace_burntime);
	
	/*
		Register it
	*/
	
	nodedef->set(name, f);
	
	return 0; /* number of results */
}

// alias_node(name, convert_to_name)
static int l_alias_node(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	std::string convert_to = luaL_checkstring(L, 2);

	// Get the writable node definition manager from the server
	IWritableNodeDefManager *nodedef =
			get_server(L)->getWritableNodeDefManager();
	
	nodedef->setAlias(name, convert_to);
	
	return 0; /* number of results */
}

// alias_tool(name, convert_to_name)
static int l_alias_tool(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	std::string convert_to = luaL_checkstring(L, 2);

	// Get the writable tool definition manager from the server
	IWritableToolDefManager *tooldef =
			get_server(L)->getWritableToolDefManager();
	
	tooldef->setAlias(name, convert_to);

	return 0; /* number of results */
}

// alias_craftitem(name, convert_to_name)
static int l_alias_craftitem(lua_State *L)
{
	std::string name = luaL_checkstring(L, 1);
	std::string convert_to = luaL_checkstring(L, 2);

	// Get the writable CraftItem definition manager from the server
	IWritableCraftItemDefManager *craftitemdef =
			get_server(L)->getWritableCraftItemDefManager();
	
	craftitemdef->setAlias(name, convert_to);

	return 0; /* number of results */
}

// register_craft({output=item, recipe={{item00,item10},{item01,item11}})
static int l_register_craft(lua_State *L)
{
	//infostream<<"register_craft"<<std::endl;
	luaL_checktype(L, 1, LUA_TTABLE);
	int table0 = 1;

	// Get the writable craft definition manager from the server
	IWritableCraftDefManager *craftdef =
			get_server(L)->getWritableCraftDefManager();
	
	std::string output;
	int width = 0;
	std::vector<std::string> input;

	lua_getfield(L, table0, "output");
	luaL_checktype(L, -1, LUA_TSTRING);
	if(lua_isstring(L, -1))
		output = lua_tostring(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, table0, "recipe");
	luaL_checktype(L, -1, LUA_TTABLE);
	if(lua_istable(L, -1)){
		int table1 = lua_gettop(L);
		lua_pushnil(L);
		int rowcount = 0;
		while(lua_next(L, table1) != 0){
			int colcount = 0;
			// key at index -2 and value at index -1
			luaL_checktype(L, -1, LUA_TTABLE);
			if(lua_istable(L, -1)){
				int table2 = lua_gettop(L);
				lua_pushnil(L);
				while(lua_next(L, table2) != 0){
					// key at index -2 and value at index -1
					luaL_checktype(L, -1, LUA_TSTRING);
					input.push_back(lua_tostring(L, -1));
					// removes value, keeps key for next iteration
					lua_pop(L, 1);
					colcount++;
				}
			}
			if(rowcount == 0){
				width = colcount;
			} else {
				if(colcount != width){
					std::string error;
					error += "Invalid crafting recipe (output=\""
							+ output + "\")";
					throw LuaError(L, error);
				}
			}
			// removes value, keeps key for next iteration
			lua_pop(L, 1);
			rowcount++;
		}
	}
	lua_pop(L, 1);

	CraftDefinition def(output, width, input);
	craftdef->registerCraft(def);

	return 0; /* number of results */
}

// setting_get(name)
static int l_setting_get(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	try{
		std::string value = g_settings->get(name);
		lua_pushstring(L, value.c_str());
	} catch(SettingNotFoundException &e){
		lua_pushnil(L);
	}
	return 1;
}

// setting_getbool(name)
static int l_setting_getbool(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	try{
		bool value = g_settings->getBool(name);
		lua_pushboolean(L, value);
	} catch(SettingNotFoundException &e){
		lua_pushnil(L);
	}
	return 1;
}

// chat_send_all(text)
static int l_chat_send_all(lua_State *L)
{
	const char *text = luaL_checkstring(L, 1);
	// Get server from registry
	Server *server = get_server(L);
	// Send
	server->notifyPlayers(narrow_to_wide(text));
	return 0;
}

// chat_send_player(name, text)
static int l_chat_send_player(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	const char *text = luaL_checkstring(L, 2);
	// Get server from registry
	Server *server = get_server(L);
	// Send
	server->notifyPlayer(name, narrow_to_wide(text));
	return 0;
}

// get_player_privs(name, text)
static int l_get_player_privs(lua_State *L)
{
	const char *name = luaL_checkstring(L, 1);
	// Get server from registry
	Server *server = get_server(L);
	// Do it
	lua_newtable(L);
	int table = lua_gettop(L);
	u64 privs_i = server->getPlayerAuthPrivs(name);
	// Special case for the "name" setting (local player / server owner)
	if(name == g_settings->get("name"))
		privs_i = PRIV_ALL;
	std::set<std::string> privs_s = privsToSet(privs_i);
	for(std::set<std::string>::const_iterator
			i = privs_s.begin(); i != privs_s.end(); i++){
		lua_pushboolean(L, true);
		lua_setfield(L, table, i->c_str());
	}
	lua_pushvalue(L, table);
	return 1;
}


// get_player_meta(player_name,meta_name,type)
// types: string, int, double, bool, v3s16, v3f, v3fpos
static int l_get_player_meta(lua_State *L)
{
	const std::string player_name = luaL_checkstring(L, 1);
	const std::string meta_name = luaL_checkstring(L, 2);
	const std::string type = luaL_checkstring(L, 3);

	try{

		ServerEnvironment* env = get_env(L);
		Player* player = env->getPlayer(player_name.c_str());
		if(!player) throw BaseException("");

		if(type=="string"){
			const std::string val = env->getPlayerMeta<std::string>(*player,meta_name);
			lua_pushstring(L,val.c_str());
			return 1;
		}

		if(type=="int"){
			int val = env->getPlayerMeta<int>(*player,meta_name);
			lua_pushinteger(L,val);
			return 1;
		}

		if(type=="double"){
			double val = env->getPlayerMeta<double>(*player,meta_name);
			lua_pushnumber(L,val);
			return 1;
		}

		if(type=="bool"){
			int val = env->getPlayerMeta<int>(*player,meta_name);
			lua_pushboolean(L,val != 0);
			return 1;
		}

		if(type=="v3s16"){
			v3s16 val = env->getPlayerMeta<v3s16>(*player,meta_name);
			push_v3s16(L,val);
			return 1;
		}

		if(type=="v3f"){
			v3f val = env->getPlayerMeta<v3f>(*player,meta_name);
			push_v3f(L,val);
			return 1;
		}

		if(type=="v3fpos"){
			v3f val = env->getPlayerMeta<v3f>(*player,meta_name);
			pushFloatPos(L,val);
			return 1;
		}

	}catch(std::exception&){}

	//we shall not be here if no error
	lua_pushnil(L);
	return 1;
}


// set_player_meta(player_name,meta_name,type,value)
// types: string, int, double, bool, v3s16, v3f, v3fpos
static int l_set_player_meta(lua_State *L)
{
	const std::string player_name = luaL_checkstring(L, 1);
	const std::string meta_name = luaL_checkstring(L, 2);
	const std::string type = luaL_checkstring(L, 3);

	try{

		ServerEnvironment* env = get_env(L);
		Player* player = env->getPlayer(player_name.c_str());
		if(!player) throw BaseException("");

		if(type=="string"){
			const std::string val = luaL_checkstring(L, 4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="int"){
			const int val = luaL_checkint(L, 4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="double"){
			const double val = luaL_checknumber(L, 4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="bool"){
			if(!lua_isboolean(L,4)) return luaL_error(L,"bool expected");
			const int val = lua_toboolean(L, 4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="v3s16"){
			v3s16 val = check_v3s16(L,4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="v3f"){
			v3f val = check_v3f(L,4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

		if(type=="v3fpos"){
			v3f val = checkFloatPos(L,4);
			env->setPlayerMeta(*player,meta_name,val);
			return 0;
		}

	}catch(std::exception&){}

	//we shall not be here if no error
	return luaL_error(L,"set_player_meta - error occured");
}


// get_inventory(location)
static int l_get_inventory(lua_State *L)
{
	InventoryLocation loc;

	std::string type = checkstringfield(L, 1, "type");
	if(type == "player"){
		std::string name = checkstringfield(L, 1, "name");
		loc.setPlayer(name);
	} else if(type == "node"){
		lua_getfield(L, 1, "pos");
		v3s16 pos = check_v3s16(L, -1);
		loc.setNodeMeta(pos);
	}
	
	if(get_server(L)->getInventory(loc) != NULL)
		InvRef::create(L, loc);
	else
		lua_pushnil(L);
	return 1;
}

// get_modpath(modname)
static int l_get_modpath(lua_State *L)
{
	const char *modname = luaL_checkstring(L, 1);
	// Get server from registry
	lua_getfield(L, LUA_REGISTRYINDEX, "minetest_server");
	Server *server = (Server*)lua_touserdata(L, -1);
	// Do it
	const ModSpec *mod = server->getModSpec(modname);
	if(!mod){
		lua_pushnil(L);
		return 1;
	}
	lua_pushstring(L, mod->path.c_str());
	return 1;
}

static const struct luaL_Reg minetest_f [] = {
	{"register_nodedef_defaults", l_register_nodedef_defaults},
	{"register_entity", l_register_entity},
	{"register_tool", l_register_tool},
	{"register_craftitem", l_register_craftitem},
	{"register_node", l_register_node},
	{"register_craft", l_register_craft},
	{"register_abm", l_register_abm},
	{"alias_node", l_alias_node},
	{"alias_tool", l_alias_tool},
	{"alias_craftitem", l_alias_craftitem},
	{"setting_get", l_setting_get},
	{"setting_getbool", l_setting_getbool},
	{"chat_send_all", l_chat_send_all},
	{"chat_send_player", l_chat_send_player},
	{"get_player_privs", l_get_player_privs},
	{"get_player_meta", l_get_player_meta},
	{"set_player_meta", l_set_player_meta},
	{"get_inventory", l_get_inventory},
	{"get_modpath", l_get_modpath},
	{NULL, NULL}
};

/*
	LuaEntity functions
*/

static const struct luaL_Reg minetest_entity_m [] = {
	{NULL, NULL}
};

/*
	Main export function
*/

void scriptapi_export(lua_State *L, Server *server)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_export"<<std::endl;
	StackUnroller stack_unroller(L);

	// Store server as light userdata in registry
	lua_pushlightuserdata(L, server);
	lua_setfield(L, LUA_REGISTRYINDEX, "minetest_server");

	// Store nil as minetest_nodedef_defaults in registry
	lua_pushnil(L);
	lua_setfield(L, LUA_REGISTRYINDEX, "minetest_nodedef_default");
	
	// Register global functions in table minetest
	lua_newtable(L);
	luaL_register(L, NULL, minetest_f);
	lua_setglobal(L, "minetest");
	
	// Get the main minetest table
	lua_getglobal(L, "minetest");

	// Add tables to minetest
	
	lua_newtable(L);
	lua_setfield(L, -2, "registered_nodes");
	lua_newtable(L);
	lua_setfield(L, -2, "registered_entities");
	lua_newtable(L);
	lua_setfield(L, -2, "registered_craftitems");
	lua_newtable(L);
	lua_setfield(L, -2, "registered_abms");
	
	lua_newtable(L);
	lua_setfield(L, -2, "object_refs");
	lua_newtable(L);
	lua_setfield(L, -2, "luaentities");

	// Create entity prototype
	luaL_newmetatable(L, "minetest.entity");
	// metatable.__index = metatable
	lua_pushvalue(L, -1); // Duplicate metatable
	lua_setfield(L, -2, "__index");
	// Put functions in metatable
	luaL_register(L, NULL, minetest_entity_m);
	// Put other stuff in metatable
	
	// Register wrappers
	ItemStack::Register(L);
	InvRef::Register(L);
	NodeMetaRef::Register(L);
	ObjectRef::Register(L);
	EnvRef::Register(L);
}

bool scriptapi_loadmod(lua_State *L, const std::string &scriptpath,
		const std::string &modname)
{
	ModNameStorer modnamestorer(L, modname);

	if(!string_allowed(modname, "abcdefghijklmnopqrstuvwxyz"
			"0123456789_")){
		errorstream<<"Error loading mod \""<<modname
				<<"\": modname does not follow naming conventions: "
				<<"Only chararacters [a-z0-9_] are allowed."<<std::endl;
		return false;
	}
	
	bool success = false;

	try{
		success = script_load(L, scriptpath.c_str());
	}
	catch(LuaError &e){
		errorstream<<"Error loading mod \""<<modname
				<<"\": "<<e.what()<<std::endl;
	}

	return success;
}

void scriptapi_add_environment(lua_State *L, ServerEnvironment *env)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_add_environment"<<std::endl;
	StackUnroller stack_unroller(L);

	// Create EnvRef on stack
	EnvRef::create(L, env);
	int envref = lua_gettop(L);

	// minetest.env = envref
	lua_getglobal(L, "minetest");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushvalue(L, envref);
	lua_setfield(L, -2, "env");

	// Store environment as light userdata in registry
	lua_pushlightuserdata(L, env);
	lua_setfield(L, LUA_REGISTRYINDEX, "minetest_env");

	/*
		Add ActiveBlockModifiers to environment
	*/

	// Get minetest.registered_abms
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_abms");
	luaL_checktype(L, -1, LUA_TTABLE);
	int registered_abms = lua_gettop(L);
	
	if(lua_istable(L, registered_abms)){
		int table = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, table) != 0){
			// key at index -2 and value at index -1
			int id = lua_tonumber(L, -2);
			int current_abm = lua_gettop(L);

			std::set<std::string> trigger_contents;
			lua_getfield(L, current_abm, "nodenames");
			if(lua_istable(L, -1)){
				int table = lua_gettop(L);
				lua_pushnil(L);
				while(lua_next(L, table) != 0){
					// key at index -2 and value at index -1
					luaL_checktype(L, -1, LUA_TSTRING);
					trigger_contents.insert(lua_tostring(L, -1));
					// removes value, keeps key for next iteration
					lua_pop(L, 1);
				}
			} else if(lua_isstring(L, -1)){
				trigger_contents.insert(lua_tostring(L, -1));
			}
			lua_pop(L, 1);

			std::set<std::string> required_neighbors;
			lua_getfield(L, current_abm, "neighbors");
			if(lua_istable(L, -1)){
				int table = lua_gettop(L);
				lua_pushnil(L);
				while(lua_next(L, table) != 0){
					// key at index -2 and value at index -1
					luaL_checktype(L, -1, LUA_TSTRING);
					required_neighbors.insert(lua_tostring(L, -1));
					// removes value, keeps key for next iteration
					lua_pop(L, 1);
				}
			} else if(lua_isstring(L, -1)){
				required_neighbors.insert(lua_tostring(L, -1));
			}
			lua_pop(L, 1);

			float trigger_interval = 10.0;
			getfloatfield(L, current_abm, "interval", trigger_interval);

			int trigger_chance = 50;
			getintfield(L, current_abm, "chance", trigger_chance);

			LuaABM *abm = new LuaABM(L, id, trigger_contents,
					required_neighbors, trigger_interval, trigger_chance);
			
			env->addActiveBlockModifier(abm);

			// removes value, keeps key for next iteration
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}

#if 0
// Dump stack top with the dump2 function
static void dump2(lua_State *L, const char *name)
{
	// Dump object (debug)
	lua_getglobal(L, "dump2");
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_pushvalue(L, -2); // Get previous stack top as first parameter
	lua_pushstring(L, name);
	if(lua_pcall(L, 2, 0, 0))
		script_error(L, "error: %s", lua_tostring(L, -1));
}
#endif

/*
	object_reference
*/

void scriptapi_add_object_reference(lua_State *L, ServerActiveObject *cobj)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_add_object_reference: id="<<cobj->getId()<<std::endl;
	StackUnroller stack_unroller(L);

	// Create object on stack
	ObjectRef::create(L, cobj); // Puts ObjectRef (as userdata) on stack
	int object = lua_gettop(L);

	// Get minetest.object_refs table
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "object_refs");
	luaL_checktype(L, -1, LUA_TTABLE);
	int objectstable = lua_gettop(L);
	
	// object_refs[id] = object
	lua_pushnumber(L, cobj->getId()); // Push id
	lua_pushvalue(L, object); // Copy object to top of stack
	lua_settable(L, objectstable);
}

void scriptapi_rm_object_reference(lua_State *L, ServerActiveObject *cobj)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_rm_object_reference: id="<<cobj->getId()<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.object_refs table
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "object_refs");
	luaL_checktype(L, -1, LUA_TTABLE);
	int objectstable = lua_gettop(L);
	
	// Get object_refs[id]
	lua_pushnumber(L, cobj->getId()); // Push id
	lua_gettable(L, objectstable);
	// Set object reference to NULL
	ObjectRef::set_null(L);
	lua_pop(L, 1); // pop object

	// Set object_refs[id] = nil
	lua_pushnumber(L, cobj->getId()); // Push id
	lua_pushnil(L);
	lua_settable(L, objectstable);
}

bool scriptapi_on_chat_message(lua_State *L, const std::string &name,
		const std::string &message)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	StackUnroller stack_unroller(L);

	// Get minetest.registered_on_chat_messages
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_chat_messages");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		lua_pushstring(L, name.c_str());
		lua_pushstring(L, message.c_str());
		if(lua_pcall(L, 2, 1, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		bool ate = lua_toboolean(L, -1);
		lua_pop(L, 1);
		if(ate)
			return true;
		// value removed, keep key for next iteration
	}
	return false;
}

/*
	misc
*/

void scriptapi_on_newplayer(lua_State *L, ServerActiveObject *player)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	StackUnroller stack_unroller(L);

	// Get minetest.registered_on_newplayers
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_newplayers");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		objectref_get_or_create(L, player);
		if(lua_pcall(L, 1, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

void scriptapi_on_dieplayer(lua_State *L, ServerActiveObject *player)
{
    realitycheck(L);
    assert(lua_checkstack(L, 20));
    StackUnroller stack_unroller(L);
    
    // Get minetest.registered_on_dieplayers
    lua_getglobal(L, "minetest");
    lua_getfield(L, -1, "registered_on_dieplayers");
    luaL_checktype(L, -1, LUA_TTABLE);
    int table = lua_gettop(L);
    // Foreach
    lua_pushnil(L);
    while(lua_next(L, table) != 0){
        // key at index -2 and value at index -1
       luaL_checktype(L, -1, LUA_TFUNCTION);
        // Call function
       objectref_get_or_create(L, player);
        if(lua_pcall(L, 1, 0, 0))
            script_error(L, "error: %s", lua_tostring(L, -1));
        // value removed, keep key for next iteration
    }
}


bool scriptapi_on_respawnplayer(lua_State *L, ServerActiveObject *player)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	StackUnroller stack_unroller(L);

	bool positioning_handled_by_some = false;

	// Get minetest.registered_on_respawnplayers
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_respawnplayers");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		objectref_get_or_create(L, player);
		if(lua_pcall(L, 1, 1, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		bool positioning_handled = lua_toboolean(L, -1);
		lua_pop(L, 1);
		if(positioning_handled)
			positioning_handled_by_some = true;
		// value removed, keep key for next iteration
	}
	return positioning_handled_by_some;
}

void scriptapi_get_creative_inventory(lua_State *L, ServerRemotePlayer *player)
{
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "creative_inventory");
	luaL_checktype(L, -1, LUA_TTABLE);
	inventory_set_list_from_lua(&player->inventory, "main", L, -1,
			player->getEnv()->getGameDef(), PLAYER_INVENTORY_SIZE);
}

/*
	craftitem
*/

static void pushPointedThing(lua_State *L, const PointedThing& pointed)
{
	lua_newtable(L);
	if(pointed.type == POINTEDTHING_NODE)
	{
		lua_pushstring(L, "node");
		lua_setfield(L, -2, "type");
		push_v3s16(L, pointed.node_undersurface);
		lua_setfield(L, -2, "under");
		push_v3s16(L, pointed.node_abovesurface);
		lua_setfield(L, -2, "above");
	}
	else if(pointed.type == POINTEDTHING_OBJECT)
	{
		lua_pushstring(L, "object");
		lua_setfield(L, -2, "type");
		objectref_get(L, pointed.object_id);
		lua_setfield(L, -2, "ref");
	}
	else
	{
		lua_pushstring(L, "nothing");
		lua_setfield(L, -2, "type");
	}
}

void scriptapi_add_craftitem(lua_State *L, const char *name)
{
	StackUnroller stack_unroller(L);
	assert(lua_gettop(L) > 0);

	// Set minetest.registered_craftitems[name] = table on top of stack
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_craftitems");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushvalue(L, -3); // push another reference to the table to be registered
	lua_setfield(L, -2, name); // set minetest.registered_craftitems[name]
}

static bool get_craftitem_callback(lua_State *L, const char *name,
		const char *callbackname)
{
	// Get minetest.registered_craftitems[name][callbackname]
	// If that is nil or on error, return false and stack is unchanged
	// If that is a function, returns true and pushes the
	// function onto the stack

	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_craftitems");
	lua_remove(L, -2);
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_getfield(L, -1, name);
	lua_remove(L, -2);
	// Should be a table
	if(lua_type(L, -1) != LUA_TTABLE)
	{
		errorstream<<"CraftItem name \""<<name<<"\" not defined"<<std::endl;
		lua_pop(L, 1);
		return false;
	}
	lua_getfield(L, -1, callbackname);
	lua_remove(L, -2);
	// Should be a function or nil
	if(lua_type(L, -1) == LUA_TFUNCTION)
	{
		return true;
	}
	else if(lua_isnil(L, -1))
	{
		lua_pop(L, 1);
		return false;
	}
	else
	{
		errorstream<<"CraftItem name \""<<name<<"\" callback \""
			<<callbackname<<" is not a function"<<std::endl;
		lua_pop(L, 1);
		return false;
	}
}

bool scriptapi_craftitem_on_drop(lua_State *L, const char *name,
		ServerActiveObject *dropper, v3f pos,
		bool &callback_exists)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_craftitem_on_drop"<<std::endl;
	StackUnroller stack_unroller(L);

	bool result = false;
	callback_exists = get_craftitem_callback(L, name, "on_drop");
	if(callback_exists)
	{
		// Call function
		lua_pushstring(L, name);
		objectref_get_or_create(L, dropper);
		pushFloatPos(L, pos);
		if(lua_pcall(L, 3, 1, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		result = lua_toboolean(L, -1);
	}
	return result;
}

bool scriptapi_craftitem_on_place_on_ground(lua_State *L, const char *name,
		ServerActiveObject *placer, v3f pos,
		bool &callback_exists)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_craftitem_on_place_on_ground"<<std::endl;
	StackUnroller stack_unroller(L);

	bool result = false;
	callback_exists = get_craftitem_callback(L, name, "on_place_on_ground");
	if(callback_exists)
	{
		// Call function
		lua_pushstring(L, name);
		objectref_get_or_create(L, placer);
		pushFloatPos(L, pos);
		if(lua_pcall(L, 3, 1, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		result = lua_toboolean(L, -1);
	}
	return result;
}

bool scriptapi_craftitem_on_use(lua_State *L, const char *name,
		ServerActiveObject *user, const PointedThing& pointed,
		bool &callback_exists)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_craftitem_on_use"<<std::endl;
	StackUnroller stack_unroller(L);

	bool result = false;
	callback_exists = get_craftitem_callback(L, name, "on_use");
	if(callback_exists)
	{
		// Call function
		lua_pushstring(L, name);
		objectref_get_or_create(L, user);
		pushPointedThing(L, pointed);
		if(lua_pcall(L, 3, 1, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		result = lua_toboolean(L, -1);
	}
	return result;
}

/*
	environment
*/

void scriptapi_environment_step(lua_State *L, float dtime)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_environment_step"<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.registered_globalsteps
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_globalsteps");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		lua_pushnumber(L, dtime);
		if(lua_pcall(L, 1, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

void scriptapi_environment_on_placenode(lua_State *L, v3s16 p, MapNode newnode,
		ServerActiveObject *placer)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_environment_on_placenode"<<std::endl;
	StackUnroller stack_unroller(L);

	// Get the writable node definition manager from the server
	IWritableNodeDefManager *ndef =
			get_server(L)->getWritableNodeDefManager();
	
	// Get minetest.registered_on_placenodes
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_placenodes");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		push_v3s16(L, p);
		pushnode(L, newnode, ndef);
		objectref_get_or_create(L, placer);
		if(lua_pcall(L, 3, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

void scriptapi_environment_on_dignode(lua_State *L, v3s16 p, MapNode oldnode,
		ServerActiveObject *digger)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_environment_on_dignode"<<std::endl;
	StackUnroller stack_unroller(L);

	// Get the writable node definition manager from the server
	IWritableNodeDefManager *ndef =
			get_server(L)->getWritableNodeDefManager();
	
	// Get minetest.registered_on_dignodes
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_dignodes");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		push_v3s16(L, p);
		pushnode(L, oldnode, ndef);
		objectref_get_or_create(L, digger);
		if(lua_pcall(L, 3, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

void scriptapi_environment_on_punchnode(lua_State *L, v3s16 p, MapNode node,
		ServerActiveObject *puncher)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_environment_on_punchnode"<<std::endl;
	StackUnroller stack_unroller(L);

	// Get the writable node definition manager from the server
	IWritableNodeDefManager *ndef =
			get_server(L)->getWritableNodeDefManager();
	
	// Get minetest.registered_on_punchnodes
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_punchnodes");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		push_v3s16(L, p);
		pushnode(L, node, ndef);
		objectref_get_or_create(L, puncher);
		if(lua_pcall(L, 3, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

void scriptapi_environment_on_generated(lua_State *L, v3s16 minp, v3s16 maxp)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_environment_on_generated"<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.registered_on_generateds
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_on_generateds");
	luaL_checktype(L, -1, LUA_TTABLE);
	int table = lua_gettop(L);
	// Foreach
	lua_pushnil(L);
	while(lua_next(L, table) != 0){
		// key at index -2 and value at index -1
		luaL_checktype(L, -1, LUA_TFUNCTION);
		// Call function
		push_v3s16(L, minp);
		push_v3s16(L, maxp);
		if(lua_pcall(L, 2, 0, 0))
			script_error(L, "error: %s", lua_tostring(L, -1));
		// value removed, keep key for next iteration
	}
}

/*
	luaentity
*/

bool scriptapi_luaentity_add(lua_State *L, u16 id, const char *name,
		const std::string &staticdata)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_luaentity_add: id="<<id<<" name=\""
			<<name<<"\""<<std::endl;
	StackUnroller stack_unroller(L);
	
	// Get minetest.registered_entities[name]
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "registered_entities");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushstring(L, name);
	lua_gettable(L, -2);
	// Should be a table, which we will use as a prototype
	//luaL_checktype(L, -1, LUA_TTABLE);
	if(lua_type(L, -1) != LUA_TTABLE){
		errorstream<<"LuaEntity name \""<<name<<"\" not defined"<<std::endl;
		return false;
	}
	int prototype_table = lua_gettop(L);
	//dump2(L, "prototype_table");
	
	// Create entity object
	lua_newtable(L);
	int object = lua_gettop(L);

	// Set object metatable
	lua_pushvalue(L, prototype_table);
	lua_setmetatable(L, -2);
	
	// Add object reference
	// This should be userdata with metatable ObjectRef
	objectref_get(L, id);
	luaL_checktype(L, -1, LUA_TUSERDATA);
	if(!luaL_checkudata(L, -1, "ObjectRef"))
		luaL_typerror(L, -1, "ObjectRef");
	lua_setfield(L, -2, "object");

	// minetest.luaentities[id] = object
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "luaentities");
	luaL_checktype(L, -1, LUA_TTABLE);
	lua_pushnumber(L, id); // Push id
	lua_pushvalue(L, object); // Copy object to top of stack
	lua_settable(L, -3);
	
	// Get on_activate function
	lua_pushvalue(L, object);
	lua_getfield(L, -1, "on_activate");
	if(!lua_isnil(L, -1)){
		luaL_checktype(L, -1, LUA_TFUNCTION);
		lua_pushvalue(L, object); // self
		lua_pushlstring(L, staticdata.c_str(), staticdata.size());
		// Call with 2 arguments, 0 results
		if(lua_pcall(L, 2, 0, 0))
			script_error(L, "error running function %s:on_activate: %s\n",
					name, lua_tostring(L, -1));
	}
	
	return true;
}

void scriptapi_luaentity_rm(lua_State *L, u16 id)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_luaentity_rm: id="<<id<<std::endl;

	// Get minetest.luaentities table
	lua_getglobal(L, "minetest");
	lua_getfield(L, -1, "luaentities");
	luaL_checktype(L, -1, LUA_TTABLE);
	int objectstable = lua_gettop(L);
	
	// Set luaentities[id] = nil
	lua_pushnumber(L, id); // Push id
	lua_pushnil(L);
	lua_settable(L, objectstable);
	
	lua_pop(L, 2); // pop luaentities, minetest
}

std::string scriptapi_luaentity_get_staticdata(lua_State *L, u16 id)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_luaentity_get_staticdata: id="<<id<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.luaentities[id]
	luaentity_get(L, id);
	int object = lua_gettop(L);
	
	// Get get_staticdata function
	lua_pushvalue(L, object);
	lua_getfield(L, -1, "get_staticdata");
	if(lua_isnil(L, -1))
		return "";
	
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_pushvalue(L, object); // self
	// Call with 1 arguments, 1 results
	if(lua_pcall(L, 1, 1, 0))
		script_error(L, "error running function get_staticdata: %s\n",
				lua_tostring(L, -1));
	
	size_t len=0;
	const char *s = lua_tolstring(L, -1, &len);
	return std::string(s, len);
}

void scriptapi_luaentity_get_properties(lua_State *L, u16 id,
		LuaEntityProperties *prop)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	infostream<<"scriptapi_luaentity_get_properties: id="<<id<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.luaentities[id]
	luaentity_get(L, id);
	//int object = lua_gettop(L);

	/* Read stuff */
	
	getboolfield(L, -1, "physical", prop->physical);

	getfloatfield(L, -1, "weight", prop->weight);

	lua_getfield(L, -1, "collisionbox");
	if(lua_istable(L, -1))
		prop->collisionbox = read_aabbox3df32(L, -1, 1.0);
	lua_pop(L, 1);

	getstringfield(L, -1, "visual", prop->visual);
	
	lua_getfield(L, -1, "visual_size");
	if(lua_istable(L, -1))
		prop->visual_size = read_v2f(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "textures");
	if(lua_istable(L, -1)){
		prop->textures.clear();
		int table = lua_gettop(L);
		lua_pushnil(L);
		while(lua_next(L, table) != 0){
			// key at index -2 and value at index -1
			if(lua_isstring(L, -1))
				prop->textures.push_back(lua_tostring(L, -1));
			else
				prop->textures.push_back("");
			// removes value, keeps key for next iteration
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	
	lua_getfield(L, -1, "spritediv");
	if(lua_istable(L, -1))
		prop->spritediv = read_v2s16(L, -1);
	lua_pop(L, 1);

	lua_getfield(L, -1, "initial_sprite_basepos");
	if(lua_istable(L, -1))
		prop->initial_sprite_basepos = read_v2s16(L, -1);
	lua_pop(L, 1);
}

void scriptapi_luaentity_step(lua_State *L, u16 id, float dtime)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_luaentity_step: id="<<id<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.luaentities[id]
	luaentity_get(L, id);
	int object = lua_gettop(L);
	// State: object is at top of stack
	// Get step function
	lua_getfield(L, -1, "on_step");
	if(lua_isnil(L, -1))
		return;
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_pushvalue(L, object); // self
	lua_pushnumber(L, dtime); // dtime
	// Call with 2 arguments, 0 results
	if(lua_pcall(L, 2, 0, 0))
		script_error(L, "error running function 'on_step': %s\n", lua_tostring(L, -1));
}

// Calls entity:on_punch(ObjectRef puncher, time_from_last_punch)
void scriptapi_luaentity_punch(lua_State *L, u16 id,
		ServerActiveObject *puncher, float time_from_last_punch)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_luaentity_step: id="<<id<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.luaentities[id]
	luaentity_get(L, id);
	int object = lua_gettop(L);
	// State: object is at top of stack
	// Get function
	lua_getfield(L, -1, "on_punch");
	if(lua_isnil(L, -1))
		return;
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_pushvalue(L, object); // self
	objectref_get_or_create(L, puncher); // Clicker reference
	lua_pushnumber(L, time_from_last_punch);
	// Call with 2 arguments, 0 results
	if(lua_pcall(L, 3, 0, 0))
		script_error(L, "error running function 'on_punch': %s\n", lua_tostring(L, -1));
}

// Calls entity:on_rightclick(ObjectRef clicker)
void scriptapi_luaentity_rightclick(lua_State *L, u16 id,
		ServerActiveObject *clicker)
{
	realitycheck(L);
	assert(lua_checkstack(L, 20));
	//infostream<<"scriptapi_luaentity_step: id="<<id<<std::endl;
	StackUnroller stack_unroller(L);

	// Get minetest.luaentities[id]
	luaentity_get(L, id);
	int object = lua_gettop(L);
	// State: object is at top of stack
	// Get function
	lua_getfield(L, -1, "on_rightclick");
	if(lua_isnil(L, -1))
		return;
	luaL_checktype(L, -1, LUA_TFUNCTION);
	lua_pushvalue(L, object); // self
	objectref_get_or_create(L, clicker); // Clicker reference
	// Call with 2 arguments, 0 results
	if(lua_pcall(L, 2, 0, 0))
		script_error(L, "error running function 'on_rightclick': %s\n", lua_tostring(L, -1));
}

