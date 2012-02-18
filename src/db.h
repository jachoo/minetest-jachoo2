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

/* Author: Jan Cychnerski */

#ifndef DB_HEADER
#define DB_HEADER

#include <jmutex.h>
#include <jmutexautolock.h>
#include <jthread.h>
#include <iostream>
#include <sstream>
#include <map>

#include "common_irrlicht.h"
#include "exceptions.h"

#include "utility.h"

extern "C" {
	#include "sqlite3.h"
}

#define DBTYPE_BASE 0
#define DBTYPE_SERVER 1
#define DBTYPE_CLIENT 2

class DatabaseException : public BaseException {
public:
	DatabaseException() : BaseException("Database access error") {}
	DatabaseException(const char* msg) : BaseException(msg) {}
};

//type of 64-bit database key
typedef sqlite3_int64 db_key;

/* Some helper functions */

inline s32 unsignedToSigned(s32 i, s32 max_positive)
{
	if(i < max_positive)
		return i;
	else
		return i - 2*max_positive;
}

// modulo of a negative number does not work consistently in C
inline db_key pythonmodulo(db_key i, db_key mod)
{
	if(i >= 0)
		return i % mod;
	return mod - ((-i) % mod);
}

inline v3s16 getIntegerAsBlock(db_key i)
{
	s32 x = unsignedToSigned(pythonmodulo(i, 4096), 2048);
	i = (i - x) / 4096;
	s32 y = unsignedToSigned(pythonmodulo(i, 4096), 2048);
	i = (i - y) / 4096;
	s32 z = unsignedToSigned(pythonmodulo(i, 4096), 2048);
	return v3s16(x,y,z);
}

inline v2s16 getIntegerAsSector(db_key i)
{
	v3s16 v = getIntegerAsBlock(i);
	return v2s16(v.X,v.Z);
}

inline db_key getBlockAsInteger(const v3s16& pos) {
	return (db_key)pos.Z*16777216 +
		(db_key)pos.Y*4096 + (db_key)pos.X;
}

inline db_key getSectorAsInteger(const v2s16& pos) {
	return getBlockAsInteger(v3s16(pos.X,0,pos.Y));
}

//database INT key type with implicit constructors
struct DBKey {
	db_key i;

	DBKey(const db_key& i):i(i){}
	DBKey(const u64& i):i(i){}
	DBKey(const v3s16& i):i(getBlockAsInteger(i)){}
	DBKey(const v2s16& i):i(getSectorAsInteger(i)){}

	operator db_key() const { return i; }
	operator u64() const { return i; }
	operator v3s16() const { return getIntegerAsBlock(i); }
	operator v2s16() const { return getIntegerAsSector(i); }
};

//for future use - BLOB data is stored in minetest in std::string for now
typedef std::string binary_t;

//traits for types stored in databases
//available: int, u64, db_key, v3s16, v2s16, std::string (maybe more in db.cpp)
template<class T>
struct DBTypeTraits {
	static const std::string name;
	typedef T t;
	static inline T getColumn(sqlite3_stmt* stmt, int iCol) { T v; getColumn(stmt,iCol,v); return v; }
	static void getColumn(sqlite3_stmt* stmt, int iCol, T& val);
	static void bind(sqlite3_stmt* stmt, int num, const T& val);
};


//traits for database _key_ types (for future use)
template<class T>
struct DBKeyTypeTraits : public DBTypeTraits<T> {};

//traits for database _data_ types (for future use)
template<class T>
struct DBDataTypeTraits : public DBTypeTraits<T> {};


//base class for tables in db
class ITable {
public:
	ITable(sqlite3* database, const std::string& name, const std::string& key, const std::string& data, bool old_names = false);
	
	virtual ~ITable();

	//creates the table or returns false if failed
	virtual bool createNoEx();
	
	//creates the table or throws DatabaseException
	inline void create()
	{
		if(!createNoEx()) throw DatabaseException("Cannot create table!");
	}

	//inserts or replaces data in row with given key
	//if failed, returns false
	template<class Key, class Data>
	bool put(const Key& key, const Data& data)
	{
		DBKeyTypeTraits<Key>::bind(m_write,1,key);
		DBDataTypeTraits<Data>::bind(m_write,2,data);
		int d = sqlite3_step(m_write);
		sqlite3_reset(m_write);
		return d == SQLITE_DONE;
	}

	//loads data from row with given key to buffer 'data'
	//if failed, returns false ('data' will not be modified!)
	template<class Key, class Data>
	bool getNoEx(const Key& key, Data& data)
	{
		DBKeyTypeTraits<Key>::bind(m_read,1,key);
		
		if(sqlite3_step(m_read) != SQLITE_ROW){
			sqlite3_reset(m_read);
			return false;
		}

		DBDataTypeTraits<Data>::getColumn(m_read,0,data);

		sqlite3_reset(m_read);
		return true;
	}

	//loads and returns data from row with given key
	//if failed (i.e. key doesn't exist) throws DatabaseException
	//NOTE: reversed template arguments!
	template<class Data,class Key>
	Data get(const Key& key)
	{
		Data d;
		if(!getNoEx(key,d)) throw DatabaseException("Database read error");
		return d;
	}

	//inserts all ids from tabale to given list
	template<class Key>
	bool getKeys(core::list<Key>& list)
	{
		while(sqlite3_step(m_list) == SQLITE_ROW)
		{
			Key key = DBKeyTypeTraits<Key>::getColumn(m_list,0);
			list.push_back(key);
		}
		sqlite3_reset(m_list);
		return true;
	}

	const std::string name;			//name of the table
	const std::string key_name;		//name of sqlite key type
	const std::string data_name;	//name of sqlite data type
	const bool old_names;			//if true, primary key has name 'pos' and not 'id'

protected:
	sqlite3* m_database;
	sqlite3_stmt *m_read;
	sqlite3_stmt *m_write;
	sqlite3_stmt *m_list;

	bool exec(const std::string& query);
private:
	ITable(const ITable&); //disable copy constructor
};

//template class for tables in database
template<class Key, class Data = void>
class Table : protected ITable {
public:
	Table(sqlite3* database, const std::string& name, bool old_names = false):
	  ITable(database, name, key_traits::name, data_traits::name, old_names)
	{}

	//inserts or replaces data in row with given key
	//if failed, returns false
	bool put(const Key& key, const Data& data)
	{
		return ITable::put(key,data);
	}

	//loads data from row with given key to buffer 'data'
	//if failed, returns false ('data' will not be modified!)
	bool getNoEx(const Key& key, Data& data)
	{
		return ITable::getNoEx(key,data);
	}

	//loads and returns data from row with given key
	//if failed (i.e. key doesn't exist) throws DatabaseException
	inline Data get(const Key& key)
	{
		return ITable::get<Data>(key);
	}

	//inserts all ids from tabale to given list
	bool getKeys(core::list<Key>& list)
	{
		return ITable::getKeys(list);
	}

protected:
	typedef DBKeyTypeTraits<Key> key_traits;
	typedef DBDataTypeTraits<Data> data_traits;
};

//template class for tables in database with not data type defined
template<class Key>
class Table<Key,void> : protected ITable {
public:
	Table(sqlite3* database, const std::string& name, const std::string& data_type, bool old_names = false):
	  ITable(database, name, key_traits::name, data_type, old_names)
	{}

	//inserts or replaces data in row with given key
	//if failed, returns false
    template<class Data>
	bool put(const Key& key, const Data& data)
	{
		return ITable::put(key,data);
	}

	//loads data from row with given key to buffer 'data'
	//if failed, returns false ('data' will not be modified!)
	template<class Data>
	bool getNoEx(const Key& key, Data& data)
	{
		return ITable::getNoEx(key,data);
	}

	//loads and returns data from row with given key
	//if failed (i.e. key doesn't exist) throws DatabaseException
	template<class Data>
	inline Data get(const Key& key)
	{
		return ITable::get<Data>(key);
	}

	//inserts all ids from tabale to given list
	bool getKeys(core::list<Key>& list)
	{
		return ITable::getKeys(list);
	}

protected:
	typedef DBKeyTypeTraits<Key> key_traits;
};

//database interface
class Database {
public:
	Database(const std::string& file);

	~Database();

	//creates or loads a table with given key type, data type and name
	//if old_names=true, then primary key will have name 'pos' instead of 'id'
	//BE CAREFULL! if in the database exists a table with another key/data types - result is unpredictable!
	//sometimes it may then throw DatabaseException, but don't rely on this!
	template<class Key, class Data>
	Table<Key,Data>& getTable(const std::string& name, bool old_names = false)
	{
		SharedPtr<ITable>& ptr = tables[name];
		if(ptr==NULL)
			ptr = (ITable*) new Table<Key,Data>(m_database,name,old_names);

		if(typeid(Table<Key,Data>) != typeid(*ptr)) throw DatabaseException("Wrong key/data type(s)!");

		return (Table<Key,Data>&)*ptr;
	}

	//creates or loads a table with given key type, data type and name
	//if old_names=true, then primary key will have name 'pos' instead of 'id'
	//BE CAREFULL! if in the database exists a table with another key/data types - result is unpredictable!
	//sometimes it may then throw DatabaseException, but don't rely on this!
	template<class Key>
	Table<Key>& getTable(const std::string& name, bool old_names = false)
	{
		SharedPtr<ITable>& ptr = tables[name];
		if(ptr==NULL)
			ptr = (ITable*) new Table<Key>(m_database,name,"BLOB",old_names);

		if(typeid(Table<Key>) != typeid(*ptr)) throw DatabaseException("Wrong key/data type(s)!");

		return (Table<Key>&)*ptr;
	}

	//creates or loads a typeless table
	//if old_names=true, then primary key will have name 'pos' instead of 'id'
	ITable& getTable(const std::string& name, bool old_names = false);

	//commits all changes to database and begins a new transaction
	void sync()
	{
		commit();
		begin();
	}

	//returns true if database was created from scratch (i.e. no database file existed before)
	inline bool isNew() const
	{
		return m_is_new;
	}

private:
	sqlite3* m_database;
	std::map<std::string,SharedPtr<ITable> > tables;
	bool m_is_new;

	//commits a transaction
	void commit()
	{
		sqlite3_exec(m_database,"COMMIT;", NULL, NULL, NULL);
	}

	//begins a transaction
	void begin()
	{
		sqlite3_exec(m_database,"BEGIN;", NULL, NULL, NULL);
	}
};



#endif
