/*************************************************************************
 *   Copyright (C) 2011-2012 by Paul-Louis Ageneau                       *
 *   paul-louis (at) ageneau (dot) org                                   *
 *                                                                       *
 *   This file is part of TeapotNet.                                     *
 *                                                                       *
 *   TeapotNet is free software: you can redistribute it and/or modify   *
 *   it under the terms of the GNU Affero General Public License as      *
 *   published by the Free Software Foundation, either version 3 of      *
 *   the License, or (at your option) any later version.                 *
 *                                                                       *
 *   TeapotNet is distributed in the hope that it will be useful, but    *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        *
 *   GNU Affero General Public License for more details.                 *
 *                                                                       *
 *   You should have received a copy of the GNU Affero General Public    *
 *   License along with TeapotNet.                                       *
 *   If not, see <http://www.gnu.org/licenses/>.                         *
 *************************************************************************/

#ifndef TPN_STORE_H
#define TPN_STORE_H

#include "tpn/include.h"
#include "tpn/thread.h"
#include "tpn/synchronizable.h"
#include "tpn/serializable.h"
#include "tpn/resource.h"
#include "tpn/file.h"
#include "tpn/map.h"
#include "tpn/set.h"
#include "tpn/array.h"
#include "tpn/list.h"
#include "tpn/http.h"
#include "tpn/interface.h"
#include "tpn/mutex.h"
#include "tpn/database.h"

namespace tpn
{

class User;
  
class Store : public Task, protected Synchronizable, public HttpInterfaceable
{
public:
	static Store *GlobalInstance;
  	static bool Get(const ByteString &digest, Resource &resource);
	static const size_t ChunkSize;

	Store(User *user);
	~Store(void);
	
	User *user(void) const;
	String userName(void) const;
	
	void addDirectory(const String &name, String path);
	void removeDirectory(const String &name);
	void getDirectories(Array<String> &array) const;
	
	void save(void) const;
	void start(void);

	bool query(const Resource::Query &query, Resource &resource);
	bool query(const Resource::Query &query, Set<Resource> &resources);
	
	void http(const String &prefix, Http::Request &request);

private:
	static Map<ByteString,String> Resources;	// absolute path by data hash
	static Mutex ResourcesMutex;
	
	static const String CacheDirectoryName;
	
	bool prepareQuery(Database::Statement &statement, const Resource::Query &query, const String &fields, bool oneRowOnly = false);
	void update(const String &url, const String &path, int64_t parentId, bool computeDigests);
	String urlToPath(const String &url) const;
	String absolutePath(const String &path) const;
	void run(void);
	
	User *mUser;
	Database *mDatabase;
	String mFileName;
	String mBasePath;
	StringMap mDirectories;
	bool mRunning;
};

}

#endif
