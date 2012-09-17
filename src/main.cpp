/*************************************************************************
 *   Copyright (C) 2011-2012 by Paul-Louis Ageneau                       *
 *   paul-louis (at) ageneau (dot) org                                   *
 *                                                                       *
 *   This file is part of Arcanet.                                       *
 *                                                                       *
 *   Arcanet is free software: you can redistribute it and/or modify     *
 *   it under the terms of the GNU Affero General Public License as      *
 *   published by the Free Software Foundation, either version 3 of      *
 *   the License, or (at your option) any later version.                 *
 *                                                                       *
 *   Arcanet is distributed in the hope that it will be useful, but      *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the        *
 *   GNU Affero General Public License for more details.                 *
 *                                                                       *
 *   You should have received a copy of the GNU Affero General Public    *
 *   License along with Arcanet.                                         *
 *   If not, see <http://www.gnu.org/licenses/>.                         *
 *************************************************************************/

#include "main.h"
#include "map.h"
#include "sha512.h"
#include "store.h"
#include "tracker.h"
#include "http.h"
#include "config.h"
#include "core.h"
#include "user.h"
#include "directory.h"

using namespace arc;

Mutex arc::LogMutex;

int main(int argc, char** argv)
{
	srand(time(NULL));
	
	Config::Put("tracker", "127.0.0.1:2000");
	Config::Put("port", "8480");
	Config::Put("interface_port", "8080");
	Config::Put("profiles_dir", "profiles");
	Config::Put("static_dir", "static");
	
	StringMap args;
	String last;
	for(int i=1; i<argc; ++i)
	{
		String str(argv[i]);
		if(str.empty()) continue;
		if(str[0] == '-')
		{
			if(!last.empty()) args[last] = "";
		  
			if(str[0] == '-') str.ignore();
			if(!str.empty() && str[0] == '-') str.ignore();
			if(str.empty())
			{
				std::cerr<<"Invalid option: "<<argv[i]<<std::endl;
				return 1;
			}
			last = str;
		}
		else {
			if(last.empty()) args[str] = "";
			else {
				args[last] = str;
				last.clear();
			}
		}
	}
	
	Tracker *tracker = NULL;
	if(args.contains("tracker"))
	{
	  	int port;
		args["tracker"] >> port;
		tracker = new Tracker(port);
		tracker->start();
	}
	
	// Starting interface
	String sifport = Config::Get("interface_port");
	if(args.contains("ifport")) sifport = args["ifport"];
	int ifport;
	sifport >> ifport;
	Interface::Instance = new Interface(ifport);
	Interface::Instance->start();
	
	// Starting core
	String sport = Config::Get("port");
	if(args.contains("port")) sport = args["port"];
	int port;
	sport >> port;
	Core::Instance = new Core(port);
	Core::Instance->start();

	Directory profilesDir(Config::Get("profiles_dir"));
	while(profilesDir.nextFile())
	{
		if(profilesDir.fileIsDir())
		{
			String name = profilesDir.fileName();
			User *user;
			
			try {
				user = new User(name);	
			}
			catch(const std::exception &e)
			{
				Log("main", "ERROR: Unable to load user \"" + name + "\": " + e.what());
				continue;
			}
			
			user->start();
			sleep(1);
		}
	}
	
	String usersFileName = "users.txt";
	File usersFile;
	
	if(File::Exist(usersFileName))
	{
		usersFile.open(usersFileName, File::Read);
		String line;
		while(usersFile.readLine(line))
		{
			String &name = line;
			String password = name.cut(' ');
			name.trim();
			password.trim();
			User *user = new User(name, password);
			user->start();
			sleep(1);
		}
		usersFile.close();
	}
	usersFile.open(usersFileName, File::Truncate);
	usersFile.close();
	
	Core::Instance->join();
	return 0;
}

