/*
Minetest-c55
Copyright (C) 2010-2011 celeron55, Perttu Ahola <celeron55@gmail.com>

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

#include "serverobject.h"
#include <fstream>
#include "environment.h"

ServerActiveObject::ServerActiveObject(ServerEnvironment *env, u16 id, v3f pos):
	ActiveObject(id),
	m_known_by_count(0),
	m_removed(false),
	m_env(env),
	m_base_position(pos)
{
}

ServerActiveObject::~ServerActiveObject()
{
}

/*
	TestSAO
*/

TestSAO::TestSAO(ServerEnvironment *env, u16 id, v3f pos):
	ServerActiveObject(env, id, pos),
	m_timer1(0),
	m_age(0)
{
}

void TestSAO::step(float dtime, Queue<ActiveObjectMessage> &messages)
{
	m_age += dtime;
	if(m_age > 10)
	{
		m_removed = true;
		return;
	}

	m_base_position.Y += dtime * BS * 2;
	if(m_base_position.Y > 8*BS)
		m_base_position.Y = 2*BS;

	m_timer1 -= dtime;
	if(m_timer1 < 0.0)
	{
		m_timer1 += 0.125;
		//dstream<<"TestSAO: id="<<getId()<<" sending data"<<std::endl;

		std::string data;

		data += itos(0); // 0 = position
		data += " ";
		data += itos(m_base_position.X);
		data += " ";
		data += itos(m_base_position.Y);
		data += " ";
		data += itos(m_base_position.Z);

		ActiveObjectMessage aom(getId(), false, data);
		messages.push_back(aom);
	}
}

/*
	LuaSAO
*/

extern "C"{
#include "lstring.h"
}

/*
	object_set_base_position(self, x,y,z)
	x,y,z = object_get_base_position(self)
	object_add_message(self, data)
	object_remove(self)
*/

/*
	object_set_base_position(self, x, y, z)
*/
static int lf_object_set_base_position(lua_State *L)
{
	// 4: z
	lua_Number z = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 3: y
	lua_Number y = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 2: x
	lua_Number x = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 1: self
	LuaSAO *self = (LuaSAO*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	assert(self);
	
	self->setBasePosition(v3f(x*BS,y*BS,z*BS));
	
	return 0; // Number of return values
}

/*
	object_get_base_position(self)
*/
static int lf_object_get_base_position(lua_State *L)
{
	// 1: self
	LuaSAO *self = (LuaSAO*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	assert(self);
	
	v3f pos = self->getBasePosition();

	lua_pushnumber(L, pos.X/BS);
	lua_pushnumber(L, pos.Y/BS);
	lua_pushnumber(L, pos.Z/BS);
	return 3; // Number of return values
}

/*
	object_add_message(self, string data)
	lf = luafunc
*/
static int lf_object_add_message(lua_State *L)
{
	// 2: data
	size_t datalen = 0;
	const char *data_c = lua_tolstring(L, -1, &datalen);
	lua_pop(L, 1);
	// 1: self
	LuaSAO *self = (LuaSAO*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	assert(self);
	assert(data_c);
	
	std::string data(data_c, datalen);
	//dstream<<"object_add_message: data="<<data<<std::endl;
	
	// Create message and add to queue
	ActiveObjectMessage aom(self->getId());
	aom.reliable = true;
	aom.datastring = data;
	self->m_message_queue.push_back(aom);

	return 0; // Number of return values
}

/*
	object_get_node(x,y,z)
*/
static int lf_object_get_node(lua_State *L)
{
	// 4: z
	lua_Number z = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 3: y
	lua_Number y = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 2: x
	lua_Number x = lua_tonumber(L, -1);
	lua_pop(L, 1);
	// 1: self
	LuaSAO *self = (LuaSAO*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	assert(self);

	v3s16 pos = floatToInt(v3f(x,y,z), 1.0);

	/*dstream<<"Checking node from pos=("<<pos.X<<","<<pos.Y<<","<<pos.Z
			<<")"<<std::endl;*/
	
	// Get the node
	MapNode n(CONTENT_IGNORE);
	n = self->getEnv()->getMap().getNodeNoEx(pos);

	// Create a table with some data about the node
	lua_newtable(L);
	lua_pushstring(L, "content");
	lua_pushinteger(L, n.d);
	lua_settable(L, -3);
	lua_pushstring(L, "walkable");
	lua_pushboolean(L, content_features(n.d).walkable);
	lua_settable(L, -3);
	
	// Return the table
	return 1;
}

/*
	object_remove(x,y,z)
*/
static int lf_object_remove(lua_State *L)
{
	// 1: self
	LuaSAO *self = (LuaSAO*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	
	assert(self);

	self->m_removed = true;

	return 0;
}

LuaSAO::LuaSAO(ServerEnvironment *env, u16 id, v3f pos):
	ServerActiveObject(env, id, pos),
	L(NULL)
{
	dstream<<"LuaSAO::LuaSAO(): id="<<id<<std::endl;
	L = lua_open();
	assert(L);
	
	// Load libraries
	luaopen_base(L);
	luaopen_table(L);
	luaopen_string(L);
	luaopen_math(L);
	
	// Add globals
	//lua_pushlightuserdata(L, this);
	//lua_setglobal(L, "self");
	
	// Register functions
	lua_register(L, "object_set_base_position", lf_object_set_base_position);
	lua_register(L, "object_get_base_position", lf_object_get_base_position);
	lua_register(L, "object_add_message", lf_object_add_message);
	lua_register(L, "object_get_node", lf_object_get_node);
	lua_register(L, "object_remove", lf_object_remove);
}

LuaSAO::~LuaSAO()
{
	lua_close(L);
}

std::string LuaSAO::getClientInitializationData()
{
	/*
		Read client-side script from file
	*/
	
	std::string relative_path;
	relative_path += "luaobjects/";
	relative_path += m_script_name;
	relative_path += "/client.lua";
	std::string full_path = porting::getDataPath(relative_path.c_str());
	std::string script;
	std::ifstream file(full_path.c_str(), std::ios::binary | std::ios::ate);
	int size = file.tellg();
	SharedBuffer<char> buf(size);
	file.seekg(0, std::ios::beg);
	file.read(&buf[0], size);
	file.close();
	
	/*
		Create data string
	*/
	std::string data;

	// Append script
	std::string script_string(&buf[0], buf.getSize());
	data += serializeLongString(script_string);

	/*
		Get data from server-side script for inclusion
	*/
	std::string other_data;
	
	do{

		const char *funcname = "get_client_init_data";
		lua_getglobal(L, funcname);
		if(!lua_isfunction(L,-1))
		{
			lua_pop(L,1);
			dstream<<"WARNING: LuaSAO: Function not found: "
					<<funcname<<std::endl;
			break;
		}
		
		// Parameters:
		// 1: self
		lua_pushlightuserdata(L, this);
		
		// Call (1 parameters, 1 result)
		if(lua_pcall(L, 1, 1, 0))
		{
			dstream<<"WARNING: LuaSAO: Error running function "
					<<funcname<<": "
					<<lua_tostring(L,-1)<<std::endl;
			break;
		}

		// Retrieve result
		if(!lua_isstring(L,-1))
		{
			dstream<<"WARNING: LuaSAO: Function "<<funcname
					<<" didn't return a string"<<std::endl;
			break;
		}
		
		size_t cs_len = 0;
		const char *cs = lua_tolstring(L, -1, &cs_len);
		lua_pop(L,1);

		other_data = std::string(cs, cs_len);

	}while(0);
	
	data += serializeLongString(other_data);

	return data;
}

std::string LuaSAO::getServerInitializationData()
{
	std::string data;
	
	// Script name
	data.append(serializeString(m_script_name));

	/*
		Get data from server-side script for inclusion
	*/
	std::string other_data;
	
	do{

		const char *funcname = "get_server_init_data";
		lua_getglobal(L, funcname);
		if(!lua_isfunction(L,-1))
		{
			lua_pop(L,1);
			dstream<<"WARNING: LuaSAO: Function not found: "
					<<funcname<<std::endl;
			break;
		}
		
		// Parameters:
		// 1: self
		lua_pushlightuserdata(L, this);
		
		// Call (1 parameters, 1 result)
		if(lua_pcall(L, 1, 1, 0))
		{
			dstream<<"WARNING: LuaSAO: Error running function "
					<<funcname<<": "
					<<lua_tostring(L,-1)<<std::endl;
			break;
		}

		// Retrieve result
		if(!lua_isstring(L,-1))
		{
			dstream<<"WARNING: LuaSAO: Function "<<funcname
					<<" didn't return a string"<<std::endl;
			break;
		}
		
		size_t cs_len = 0;
		const char *cs = lua_tolstring(L, -1, &cs_len);
		lua_pop(L,1);

		other_data = std::string(cs, cs_len);

	}while(0);
	
	data += serializeLongString(other_data);

	return data;
}

void LuaSAO::initialize(const std::string &data)
{
	std::istringstream is(data, std::ios::binary);
	std::string script_name = deSerializeString(is);
	std::string other = deSerializeLongString(is);

	loadScripts(script_name);

	/*
		Call initialize(self, data) in the script
	*/
	
	const char *funcname = "initialize";
	lua_getglobal(L, funcname);
	if(!lua_isfunction(L,-1))
	{
		lua_pop(L,1);
		dstream<<"WARNING: LuaSAO: Function not found: "
				<<funcname<<std::endl;
		return;
	}
	
	// Parameters:
	// 1: self
	lua_pushlightuserdata(L, this);
	// 2: data (other)
	lua_pushlstring(L, other.c_str(), other.size());
	
	// Call (2 parameters, 0 result)
	if(lua_pcall(L, 2, 0, 0))
	{
		dstream<<"WARNING: LuaSAO: Error running function "
				<<funcname<<": "
				<<lua_tostring(L,-1)<<std::endl;
		return;
	}
}

void LuaSAO::loadScripts(const std::string &script_name)
{
	m_script_name = script_name;
	
	std::string relative_path;
	relative_path += "luaobjects/";
	relative_path += script_name;
	std::string server_file = relative_path + "/server.lua";
	std::string server_path = porting::getDataPath(server_file.c_str());

	// Load the file
	int ret;
	ret = luaL_loadfile(L, server_path.c_str());
	if(ret)
	{
		const char *message = lua_tostring(L, -1);
		lua_pop(L, 1);
		dstream<<"LuaSAO::loadScript(): lua_loadfile failed: "
				<<message<<std::endl;
		assert(0);
		return;
	}
	ret = lua_pcall(L, 0, 0, 0);
	if(ret)
	{
		const char *message = lua_tostring(L, -1);
		lua_pop(L, 1);
		dstream<<"LuaSAO::loadScript(): lua_pcall failed: "
				<<message<<std::endl;
		assert(0);
		return;
	}
}

void LuaSAO::step(float dtime, Queue<ActiveObjectMessage> &messages)
{
	lua_getglobal(L, "step");
	if(!lua_isfunction(L,-1))
	{
		lua_pop(L,1);
		dstream<<"WARNING: LuaSAO::step(): step function not found"<<std::endl;
		return;
	}
	
	// Parameters:
	// 1: self
	lua_pushlightuserdata(L, this);
	// 2: dtime
	lua_pushnumber(L, dtime);
	
	// Call (2 parameters, 0 result)
	if(lua_pcall(L, 2, 0, 0))
	{
		dstream<<"WARNING: LuaSAO::step(): Error running function step(): "
				<<lua_tostring(L,-1)<<std::endl;
		return;
	}

	// Move messages
	while(m_message_queue.size() != 0)
	{
		messages.push_back(m_message_queue.pop_front());
	}
}



