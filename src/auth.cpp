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

#include "auth.h"
#include <fstream>
#include <jmutexautolock.h>
//#include "main.h" // for g_settings
#include <sstream>
#include "strfnd.h"
#include "debug.h"
#include "db.h"

std::set<std::string> privsToSet(u64 privs)
{
	std::set<std::string> s;
	if(privs & PRIV_INTERACT)
		s.insert("interact");
	if(privs & PRIV_TELEPORT)
		s.insert("teleport");
	if(privs & PRIV_SETTIME)
		s.insert("settime");
	if(privs & PRIV_PRIVS)
		s.insert("privs");
	if(privs & PRIV_SHOUT)
		s.insert("shout");
	if(privs & PRIV_BAN)
		s.insert("ban");
	if(privs & PRIV_GIVE)
		s.insert("give");
	if(privs & PRIV_PASSWORD)
		s.insert("password");
	return s;
}

// Convert a privileges value into a human-readable string,
// with each component separated by a comma.
std::string privsToString(u64 privs)
{
	std::ostringstream os(std::ios_base::binary);
	if(privs & PRIV_INTERACT)
		os<<"interact,";
	if(privs & PRIV_TELEPORT)
		os<<"teleport,";
	if(privs & PRIV_SETTIME)
		os<<"settime,";
	if(privs & PRIV_PRIVS)
		os<<"privs,";
	if(privs & PRIV_SHOUT)
		os<<"shout,";
	if(privs & PRIV_BAN)
		os<<"ban,";
	if(privs & PRIV_GIVE)
		os<<"give,";
	if(privs & PRIV_PASSWORD)
		os<<"password,";
	if(os.tellp())
	{
		// Drop the trailing comma. (Why on earth can't
		// you truncate a C++ stream anyway???)
		std::string tmp = os.str();
		return tmp.substr(0, tmp.length() -1);
	}
	return os.str();
}

// Converts a comma-seperated list of privilege values into a
// privileges value. The reverse of privsToString(). Returns
// PRIV_INVALID if there is anything wrong with the input.
u64 stringToPrivs(std::string str)
{
	u64 privs=0;
	Strfnd f(str);
	while(f.atend() == false)
	{
		std::string s = trim(f.next(","));
		if(s == "build")
			privs |= PRIV_INTERACT;
		else if(s == "interact")
			privs |= PRIV_INTERACT;
		else if(s == "teleport")
			privs |= PRIV_TELEPORT;
		else if(s == "settime")
			privs |= PRIV_SETTIME;
		else if(s == "privs")
			privs |= PRIV_PRIVS;
		else if(s == "shout")
			privs |= PRIV_SHOUT;
		else if(s == "ban")
			privs |= PRIV_BAN;
		else if(s == "give")
			privs |= PRIV_GIVE;
		else if(s == "password")
			privs |= PRIV_PASSWORD;
		else
			return PRIV_INVALID;
	}
	return privs;
}

AuthManager::AuthManager(Database* database, const std::string& authfilepath):
		m_authfilepath(authfilepath),
		m_modified(false),
		m_database(database),
		m_authtable( NULL )
{
	m_mutex.Init();

	if(m_database!=NULL)init();
}

AuthManager::~AuthManager()
{
	save();
}

void AuthManager::init(Database* database, const std::string& authfilepath)
{
	//JMutexAutoLock lock(m_mutex);

	if(database!=NULL) m_database = database;
	if(authfilepath!="") m_authfilepath = authfilepath;
	assert(m_database!=NULL);

	m_authtable = &m_database->getTable<std::string,std::string>("auth");

	try{
		load();
	}
	catch(SerializationError &e)
	{
		dstream<<"WARNING: AuthManager: load error"<<std::endl;
	}
}

void AuthManager::load()
{
	JMutexAutoLock lock(m_mutex);
	
	dstream<<"AuthManager: loading from DB"<<std::endl;

	core::list<std::string> playerlist;
	if(!m_authtable->getKeys(playerlist)) throw SerializationError("AuthManager::load(): Couldn't read keys from auth DB");

	if(playerlist.size()>0){
		for(core::list<std::string>::ConstIterator it=playerlist.begin(); it!=playerlist.end(); it++)
		{
			const std::string& name = *it;

			std::string line;
			if(!m_authtable->getNoEx(name,line))continue;

			std::istringstream iss(line);

			// Read password
			std::string pwd;
			std::getline(iss, pwd, ':');

			// Read privileges
			std::string stringprivs;
			std::getline(iss, stringprivs, ':');
			u64 privs = stringToPrivs(stringprivs);
			
			// Store it
			AuthData ad;
			ad.pwd = pwd;
			ad.privs = privs;
			m_authdata[name] = ad;
		}
	}else{
		//database is empty, try to load old way from file
		
		dstream<<"AuthManager: loading from "<<m_authfilepath<<std::endl;
		std::ifstream is(m_authfilepath.c_str(), std::ios::binary);
		if(is.good())
			for(;;)
			{
				if(is.eof() || is.good() == false)
					break;

				// Read a line
				std::string line;
				std::getline(is, line, '\n');

				std::istringstream iss(line);
				
				// Read name
				std::string name;
				std::getline(iss, name, ':');

				// Read password
				std::string pwd;
				std::getline(iss, pwd, ':');

				// Read privileges
				std::string stringprivs;
				std::getline(iss, stringprivs, ':');
				u64 privs = stringToPrivs(stringprivs);
				
				// Store it
				AuthData ad;
				ad.pwd = pwd;
				ad.privs = privs;
				m_authdata[name] = ad;
			}
	}

	m_modified = false;
}

void AuthManager::save()
{
	JMutexAutoLock lock(m_mutex);
	
	dstream<<"AuthManager: saving to DB"<<std::endl;
	
	for(core::map<std::string, AuthData>::Iterator
			i = m_authdata.getIterator();
			i.atEnd()==false; i++)
	{
		std::string name = i.getNode()->getKey();
		if(name == "")
			continue;
		AuthData ad = i.getNode()->getValue();
		
		std::ostringstream os;
		os<<ad.pwd<<":"<<privsToString(ad.privs)<<"\n";
		m_authtable->put(name,os.str());
	}

	m_modified = false;
}

bool AuthManager::exists(const std::string &username)
{
	JMutexAutoLock lock(m_mutex);
	
	core::map<std::string, AuthData>::Node *n;
	n = m_authdata.find(username);
	if(n == NULL)
		return false;
	return true;
}

void AuthManager::set(const std::string &username, AuthData ad)
{
	JMutexAutoLock lock(m_mutex);
	
	m_authdata[username] = ad;

	m_modified = true;
}

void AuthManager::add(const std::string &username)
{
	JMutexAutoLock lock(m_mutex);
	
	m_authdata[username] = AuthData();

	m_modified = true;
}

std::string AuthManager::getPassword(const std::string &username)
{
	JMutexAutoLock lock(m_mutex);
	
	core::map<std::string, AuthData>::Node *n;
	n = m_authdata.find(username);
	if(n == NULL)
		throw AuthNotFoundException("");
	
	return n->getValue().pwd;
}

void AuthManager::setPassword(const std::string &username,
		const std::string &password)
{
	JMutexAutoLock lock(m_mutex);
	
	core::map<std::string, AuthData>::Node *n;
	n = m_authdata.find(username);
	if(n == NULL)
		throw AuthNotFoundException("");
	
	AuthData ad = n->getValue();
	ad.pwd = password;
	n->setValue(ad);

	m_modified = true;
}

u64 AuthManager::getPrivs(const std::string &username)
{
	JMutexAutoLock lock(m_mutex);
	
	core::map<std::string, AuthData>::Node *n;
	n = m_authdata.find(username);
	if(n == NULL)
		throw AuthNotFoundException("");
	
	return n->getValue().privs;
}

void AuthManager::setPrivs(const std::string &username, u64 privs)
{
	JMutexAutoLock lock(m_mutex);
	
	core::map<std::string, AuthData>::Node *n;
	n = m_authdata.find(username);
	if(n == NULL)
		throw AuthNotFoundException("");
	
	AuthData ad = n->getValue();
	ad.privs = privs;
	n->setValue(ad);

	m_modified = true;
}

bool AuthManager::isModified()
{
	JMutexAutoLock lock(m_mutex);
	return m_modified;
}


