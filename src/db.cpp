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

#include "db.h"

using namespace std;

/* bool */

template<> const std::string DBTypeTraits<bool>::name = "INT";
template<> void DBTypeTraits<bool>::getColumn(sqlite3_stmt *stmt, int iCol, bool& val)
{
	val = sqlite3_column_int(stmt,iCol) != 0;
}
template<> void DBTypeTraits<bool>::bind(sqlite3_stmt* stmt, int num, const bool& val)
{
	bool d = sqlite3_bind_int(stmt,num,val?1:0);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* int */

template<> const std::string DBTypeTraits<int>::name = "INT";
template<> void DBTypeTraits<int>::getColumn(sqlite3_stmt *stmt, int iCol, int& val)
{
	val = sqlite3_column_int(stmt,iCol);
}
template<> void DBTypeTraits<int>::bind(sqlite3_stmt* stmt, int num, const int& val)
{
	int d = sqlite3_bind_int(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* unsigned int */

template<> const std::string DBTypeTraits<unsigned int>::name = "INT";
template<> void DBTypeTraits<unsigned int>::getColumn(sqlite3_stmt *stmt, int iCol, unsigned int& val)
{
	val = sqlite3_column_int(stmt,iCol);
}
template<> void DBTypeTraits<unsigned int>::bind(sqlite3_stmt* stmt, int num, const unsigned int& val)
{
	int d = sqlite3_bind_int(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}


/* u64, db_key */

template<> const std::string DBTypeTraits<u64>::name = "INT";
template<> void DBTypeTraits<u64>::getColumn(sqlite3_stmt *stmt, int iCol, u64& val)
{
	val = sqlite3_column_int64(stmt,iCol);
}
template<> void DBTypeTraits<u64>::bind(sqlite3_stmt* stmt, int num, const u64& val)
{
	u64 d = sqlite3_bind_int64(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

template<> const std::string DBTypeTraits<db_key>::name = "INT";
template<> void DBTypeTraits<db_key>::getColumn(sqlite3_stmt *stmt, int iCol, db_key& val)
{
	val = sqlite3_column_int64(stmt,iCol);
}
template<> void DBTypeTraits<db_key>::bind(sqlite3_stmt* stmt, int num, const db_key& val)
{
	db_key d = sqlite3_bind_int64(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* v3s16 */

template<> const std::string DBTypeTraits<v3s16>::name = "INT";
template<> void DBTypeTraits<v3s16>::getColumn(sqlite3_stmt *stmt, int iCol, v3s16& val)
{
	val = DBKey( sqlite3_column_int64(stmt,iCol) );
}
template<> void DBTypeTraits<v3s16>::bind(sqlite3_stmt* stmt, int num, const v3s16& val)
{
	int d = sqlite3_bind_int64(stmt,num,DBKey(val));
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* v2s16 */

template<> const std::string DBTypeTraits<v2s16>::name = "INT";
template<> void DBTypeTraits<v2s16>::getColumn(sqlite3_stmt *stmt, int iCol, v2s16 & val)
{
	val = DBKey( sqlite3_column_int64(stmt,iCol) );
}
template<> void DBTypeTraits<v2s16>::bind(sqlite3_stmt* stmt, int num, const v2s16& val)
{
	int d = sqlite3_bind_int64(stmt,num,DBKey(val));
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* v3f */

template<> const std::string DBTypeTraits<v3f>::name = "BLOB";
template<> void DBTypeTraits<v3f>::getColumn(sqlite3_stmt *stmt, int iCol, v3f& val)
{
	if(sqlite3_column_bytes(stmt,iCol) != 12) throw DatabaseException("Value is not v3f");
	const f32* data = (const f32*)sqlite3_column_blob(stmt,iCol);
	val.X = data[0];
	val.Y = data[1];
	val.Z = data[2];
}
template<> void DBTypeTraits<v3f>::bind(sqlite3_stmt* stmt, int num, const v3f& val)
{
	const float data[3] = {val.X, val.Y, val.Z};
	int d = sqlite3_bind_blob(stmt,num,data,12,SQLITE_TRANSIENT);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* string - stored as BLOB (!) */

template<> const std::string DBTypeTraits<std::string>::name = "BLOB";
template<> void DBTypeTraits<std::string>::getColumn(sqlite3_stmt *stmt, int iCol, std::string& val)
{
	const char * data = (const char *)sqlite3_column_blob(stmt,iCol);
	size_t len = sqlite3_column_bytes(stmt,iCol);

	val.assign(data,len);
}
template<> void DBTypeTraits<std::string>::bind(sqlite3_stmt* stmt, int num, const std::string& val)
{
	int d = sqlite3_bind_blob(stmt,num,val.c_str(),val.length(),NULL);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* float */

template<> const std::string DBTypeTraits<float>::name = "REAL";
template<> void DBTypeTraits<float>::getColumn(sqlite3_stmt *stmt, int iCol, float& val)
{
	val = (float)sqlite3_column_double(stmt,iCol);
}
template<> void DBTypeTraits<float>::bind(sqlite3_stmt* stmt, int num, const float& val)
{
	float d = sqlite3_bind_double(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}

/* double */

template<> const std::string DBTypeTraits<double>::name = "REAL";
template<> void DBTypeTraits<double>::getColumn(sqlite3_stmt *stmt, int iCol, double& val)
{
	val = sqlite3_column_double(stmt,iCol);
}
template<> void DBTypeTraits<double>::bind(sqlite3_stmt* stmt, int num, const double& val)
{
	double d = sqlite3_bind_double(stmt,num,val);
	if(d != SQLITE_OK) throw DatabaseException("Bind error");
}



/* ITable */


ITable::ITable(sqlite3* database, const std::string& name, const std::string& key, const std::string& data, bool old_names):
	m_database(database),
	name(name),
	key_name(key),
	data_name(data),
	old_names(old_names)
{
	assert(database);

	create();

	int d;
	std::string q;

	std::string id_name = old_names ? "pos" : "id";

	q = "SELECT `data` FROM `"+name+"` WHERE `"+id_name+"`=? LIMIT 1";
	d = sqlite3_prepare_v2(m_database,q.c_str(), -1, &m_read, NULL);
	if(d != SQLITE_OK) {
		//infostream<<"WARNING: Database read statment failed to prepare: "<<sqlite3_errmsg(m_database)<<std::endl;
		throw FileNotGoodException("Cannot prepare read statement");
	}
	
	q = "REPLACE INTO `"+name+"` ("+id_name+",data) VALUES(?, ?)";
	d = sqlite3_prepare_v2(m_database,q.c_str(), -1, &m_write, NULL);
	if(d != SQLITE_OK) {
		//infostream<<"WARNING: Database write statment failed to prepare: "<<sqlite3_errmsg(m_database)<<std::endl;
		throw FileNotGoodException("Cannot prepare write statement");
	}
	
	q = "SELECT `"+id_name+"` FROM `"+name+"`";
	d = sqlite3_prepare_v2(m_database,q.c_str(), -1, &m_list, NULL);
	if(d != SQLITE_OK) {
		//infostream<<"WARNING: Database list statment failed to prepare: "<<sqlite3_errmsg(m_database)<<std::endl;
		throw FileNotGoodException("Cannot prepare read statement");
	}
}

ITable::~ITable() {
	if(m_read)
		sqlite3_finalize(m_read);
	if(m_write)
		sqlite3_finalize(m_write);
	if(m_list)
		sqlite3_finalize(m_list);
}

//creates the table or returns false if failed
bool ITable::createNoEx()
{
	std::string id_name = old_names ? "pos" : "id";
	std::string query =
		"CREATE TABLE IF NOT EXISTS `" + name + "` ("
		"`"+id_name+"` " + key_name + " NOT NULL PRIMARY KEY,"
		"`data` " + data_name +
		");";

	return exec(query);
}

bool ITable::exec(const std::string& query)
{
	assert(m_database);
	int e = sqlite3_exec(m_database,query.c_str(), NULL, NULL, NULL);
	return e == SQLITE_OK;
}


/* Database */


Database::Database(const std::string& file)
{
	m_is_new = false;

	int d = sqlite3_open_v2(file.c_str(), &m_database, SQLITE_OPEN_READWRITE, NULL);
	
	if(d != SQLITE_OK) {
		//can't open a file. try to create it.
		m_is_new = true;
		d = sqlite3_open_v2(file.c_str(), &m_database, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	}

	if(d != SQLITE_OK) {
		//infostream<<"WARNING: Database failed to open: "<<sqlite3_errmsg(m_database)<<std::endl;
		throw FileNotGoodException("Cannot create/open database file");
	}

	//begin first transaction
	begin();
}

Database::~Database()
{
	tables.clear(); //finalize all queries to tables
	
	commit(); //commit changes

	if(m_database)
		sqlite3_close(m_database);
}

//creates or loads a typeless table
//if old_names=true, then primary key will have name 'pos' instead of 'id'
ITable& Database::getTable(const std::string& name, bool old_names)
{
	SharedPtr<ITable>& ptr = tables[name];
	if(ptr==NULL)
		ptr = new ITable(m_database,name,"BLOB","BLOB",old_names);

	return *ptr;
}



