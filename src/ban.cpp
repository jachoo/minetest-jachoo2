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

#include "ban.h"
#include <fstream>
#include <jmutexautolock.h>
#include <sstream>
#include <set>
#include "strfnd.h"
#include "debug.h"
#include "db.h"

BanManager::BanManager(Database* database, const std::string& file):
		m_modified(false),
		m_banfilepath(file),
		m_database(database),
		m_bantable(NULL)
{
	m_mutex.Init();

	if(m_database!=NULL) init();
}

BanManager::~BanManager()
{
	save();
}

void BanManager::init(Database* database, const std::string& file)
{
	if(database!=NULL)m_database = database;
	if(file!="")m_banfilepath = file;
	assert(m_database != NULL);

	m_bantable = &database->getTable<std::string,std::string>("ipban");

	try{
		load();
	}
	catch(SerializationError &e)
	{
		dstream<<"WARNING: BanManager: load error "<<std::endl;
	}
}

void BanManager::load()
{
	JMutexAutoLock lock(m_mutex);
	
	dstream<<"BanManager: loading from DB"<<std::endl;

	core::list<std::string> iplist;
	if(!m_bantable->getKeys(iplist)) throw SerializationError("BanManager::load(): Couldn't read keys from ban DB");
	
	if(iplist.size()>0){
		for(core::list<std::string>::ConstIterator it=iplist.begin(); it!=iplist.end(); it++)
		{
			const std::string& ip = *it;

			std::string name;
			if(!m_bantable->getNoEx(ip,name))continue;

			if(ip.empty())
				continue;

			m_ips[ip] = name;
		}
	} else {
		//database is empty, try to load old way from file
		dstream<<"BanManager: loading from "<<m_banfilepath<<std::endl;
		std::ifstream is(m_banfilepath.c_str(), std::ios::binary);
		if(is.good())
			for(;;)
			{
				if(is.eof() || is.good() == false)
					break;
				std::string line;
				std::getline(is, line, '\n');
				Strfnd f(line);
				std::string ip = trim(f.next("|"));
				std::string name = trim(f.next("|"));
				if(ip.empty())
					continue;
				m_ips[ip] = name;
			}
	}
	m_modified = false;
}

void BanManager::save()
{
	JMutexAutoLock lock(m_mutex);
	
	dstream<<"BanManager: saving to DB"<<std::endl;

	for(std::map<std::string, std::string>::iterator
			i = m_ips.begin();
			i != m_ips.end(); i++)
	{
		m_bantable->put(i->first,i->second);
	}
	m_modified = false;
}

bool BanManager::isIpBanned(const std::string &ip)
{
	JMutexAutoLock lock(m_mutex);
	return m_ips.find(ip) != m_ips.end();
}

std::string BanManager::getBanDescription(const std::string &ip_or_name)
{
	JMutexAutoLock lock(m_mutex);
	std::string s = "";
	for(std::map<std::string, std::string>::iterator
			i = m_ips.begin();
			i != m_ips.end(); i++)
	{
		if(i->first == ip_or_name || i->second == ip_or_name
				|| ip_or_name == "")
			s += i->first + "|" + i->second + ", ";
	}
	s = s.substr(0, s.size()-2);
	return s;
}

std::string BanManager::getBanName(const std::string &ip)
{
	JMutexAutoLock lock(m_mutex);
	std::map<std::string, std::string>::iterator i = m_ips.find(ip);
	if(i == m_ips.end())
		return "";
	return i->second;
}

void BanManager::add(const std::string &ip, const std::string &name)
{
	JMutexAutoLock lock(m_mutex);
	m_ips[ip] = name;
	m_modified = true;
}

void BanManager::remove(const std::string &ip_or_name)
{
	JMutexAutoLock lock(m_mutex);
	//m_ips.erase(m_ips.find(ip));
	// Find out all ip-name pairs that match the ip or name
	std::set<std::string> ips_to_delete;
	for(std::map<std::string, std::string>::iterator
			i = m_ips.begin();
			i != m_ips.end(); i++)
	{
		if(i->first == ip_or_name || i->second == ip_or_name)
			ips_to_delete.insert(i->first);
	}
	// Erase them
	for(std::set<std::string>::iterator
			i = ips_to_delete.begin();
			i != ips_to_delete.end(); i++)
	{
		m_ips.erase(*i);
	}
	m_modified = true;
}
	

bool BanManager::isModified()
{
	JMutexAutoLock lock(m_mutex);
	return m_modified;
}

