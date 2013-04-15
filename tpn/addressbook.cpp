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

#include "tpn/addressbook.h"
#include "tpn/user.h"
#include "tpn/core.h"
#include "tpn/sha512.h"
#include "tpn/config.h"
#include "tpn/file.h"
#include "tpn/directory.h"
#include "tpn/html.h"
#include "tpn/yamlserializer.h"
#include "tpn/jsonserializer.h"
#include "tpn/portmapping.h"
#include "tpn/mime.h"

namespace tpn
{

AddressBook::AddressBook(User *user) :
	mUser(user),
	mUpdateCount(0)
{
	Assert(mUser != NULL);
	mFileName = mUser->profilePath() + "contacts";
	
	Interface::Instance->add("/"+mUser->name()+"/contacts", this);
	
	if(File::Exist(mFileName))
	{
		try {
			File file(mFileName, File::Read);
			load(file);
			file.close();
		}
		catch(const Exception &e)
		{
			LogError("AddressBook", String("Loading failed: ") + e.what());
		}
	}
}

AddressBook::~AddressBook(void)
{
  	Interface::Instance->remove("/"+mUser->name()+"/contacts");
	
	clear();
}

User *AddressBook::user(void) const
{
 	return mUser; 
}

String AddressBook::userName(void) const
{
 	return mUser->name(); 
}

int AddressBook::unreadMessagesCount(void) const
{
	Synchronize(this);
  
	Array<Identifier> keys;
        mContacts.getKeys(keys);
        
	int count = 0;
        for(int i=0; i<keys.size(); ++i)
        {
                Contact *contact = NULL;
                if(mContacts.get(keys[i], contact))
                {
                        Desynchronize(this);
                        count+= contact->unreadMessagesCount();
                }
        }

	return count;
}

Identifier AddressBook::addContact(String name, const ByteString &secret)
{
	Synchronize(this);

	String tracker = name.cut('@');
	if(tracker.empty()) tracker = Config::Get("tracker");
	
	String uname = name;
	unsigned i = 1;
	while((mContactsByUniqueName.contains(uname) && !mContactsByUniqueName.get(uname)->isDeleted())
		|| uname == userName())	// userName reserved for self
	{
		uname = name;
		uname << ++i;
	}
	
	Contact *oldContact;
	if(mContactsByUniqueName.get(uname, oldContact))
	{
		mContacts.erase(oldContact->peering());
		mContactsByUniqueName.erase(oldContact->uniqueName());
		Core::Instance->unregisterPeering(oldContact->peering());
		Interface::Instance->remove(oldContact->urlPrefix(), oldContact);
		delete oldContact;
	}
	
	Contact *contact = new Contact(this, uname, name, tracker, secret);
	if(mContacts.get(contact->peering(), oldContact))
	{
		oldContact->copy(contact);
		delete contact;
		contact = oldContact;
	}

	mContacts.insert(contact->peering(), contact);
	mContactsByUniqueName.insert(contact->uniqueName(), contact);
	Interface::Instance->add(contact->urlPrefix(), contact);
	
	save();
	start();
	return contact->peering();
}

void AddressBook::removeContact(const Identifier &peering)
{
	Synchronize(this);
	
	Contact *contact;
	if(mContacts.get(peering, contact))
	{
		contact->setDeleted();
		Core::Instance->unregisterPeering(contact->peering());
		Interface::Instance->remove(contact->urlPrefix(), contact);
		save();
	}
}

AddressBook::Contact *AddressBook::getContact(const Identifier &peering)
{
	Synchronize(this);
  
	Contact *contact;
	if(mContacts.get(peering, contact)) return contact;
	else return NULL;
}

const AddressBook::Contact *AddressBook::getContact(const Identifier &peering) const
{
	Synchronize(this);
  
	Contact *contact;
	if(mContacts.contains(peering)) return mContacts.get(peering);
	else return NULL;
}

void AddressBook::getContacts(Array<AddressBook::Contact *> &array)
{
	Synchronize(this); 
  
	mContactsByUniqueName.getValues(array);
	
	Contact *self = getSelf();
	if(self) array.remove(self);
	
	int i=0; 
	while(i<array.size())
	{
		if(array[i]->isDeleted()) array.erase(i);
		else ++i;
	}
}

Identifier AddressBook::setSelf(const ByteString &secret)
{
	Synchronize(this);
  
	const String tracker = Config::Get("tracker");	
	Contact *self = new Contact(this, userName(), userName(), tracker, secret);

	Contact *tmp;
        if(mContacts.get(self->peering(), tmp))
        	if(tmp->uniqueName() != userName())
			throw Exception("a contact with the same peering already exists");
	
	Contact *oldSelf = getSelf();
        if(oldSelf)
	{
		mContacts.erase(oldSelf->peering());
                Core::Instance->unregisterPeering(oldSelf->peering());
                Interface::Instance->remove(oldSelf->urlPrefix(), oldSelf);
		
		oldSelf->copy(self);
		delete self;
		self = oldSelf;
	}

	mContacts.insert(self->peering(), self);
	mContactsByUniqueName.insert(self->uniqueName(), self);
	Interface::Instance->add(self->urlPrefix(), self);
	
	save();
	start();
	return self->peering();
}

AddressBook::Contact *AddressBook::getSelf(void)
{
	Synchronize(this);
	
	Contact *contact;
	if(mContactsByUniqueName.get(userName(), contact)) return contact;
	else return NULL;
}

const AddressBook::Contact *AddressBook::getSelf(void) const
{
	Synchronize(this);
	
	Contact *contact;
	if(mContactsByUniqueName.get(userName(), contact)) return contact;
	else return NULL;
}

void AddressBook::clear(void)
{
	Synchronize(this);
  
	for(Map<Identifier, Contact*>::const_iterator it = mContacts.begin();
		it != mContacts.end();
		++it)
	{
		const Contact *contact = it->second;
		delete contact;
	}
	
	mContacts.clear();
	mContactsByUniqueName.clear();
}

void AddressBook::load(Stream &stream)
{
	Synchronize(this);

	bool changed = false;

	Contact *contact = new Contact(this);
	
	//YamlSerializer serializer(&stream);
	//while(serializer.input(*contact))
	while(true)
	{
		// Not really clean but it protects from parse errors propagation
		YamlSerializer serializer(&stream);
		if(!serializer.input(*contact)) break;

		Contact *oldContact = NULL;
		if(mContactsByUniqueName.get(contact->uniqueName(), oldContact))
		{
			if(oldContact->time() >= contact->time())
			{
				oldContact->addAddresses(contact->addresses());
				continue;
			}
		
			mContacts.erase(oldContact->peering());
			Core::Instance->unregisterPeering(oldContact->peering());
			mContactsByUniqueName.erase(oldContact->uniqueName());
			
			oldContact->copy(contact);
			std::swap(oldContact, contact);
			delete oldContact;
		}
		
		mContacts.insert(contact->peering(), contact);
		mContactsByUniqueName.insert(contact->uniqueName(), contact);
		if(!contact->isDeleted()) Interface::Instance->add(contact->urlPrefix(), contact);
		changed = true;
		
		contact = new Contact(this);
	}
	delete contact;
	
	if(changed) start();
}

void AddressBook::save(Stream &stream) const
{
	Synchronize(this);
  
	YamlSerializer serializer(&stream);
	
	Array<Identifier> keys;
        mContacts.getKeys(keys);

        for(int i=0; i<keys.size(); ++i)
        {
                Contact *contact = NULL;
                if(mContacts.get(keys[i], contact))
                {
			Desynchronize(this);
			serializer.output(*contact);
		}
        }
}

void AddressBook::save(void) const
{
	String data;
	
	{
		Synchronize(this);
		save(data);
		SafeWriteFile file(mFileName);
		file.write(data);
		file.close();
	}
	
	const Contact *self = getSelf();
	if(self && self->isConnected())
	{
		try {
			Message message(data);
			message.setParameter("type", "contacts");
			message.send(self->peering());
		}
		catch(Exception &e)
		{
			LogWarn("AddressBook::Save", String("Contacts synchronization failed: ") + e.what()); 
		}
	}
}

void AddressBook::update(void)
{
	Synchronize(this);
	LogDebug("AddressBook::update", "Updating " + String::number(unsigned(mContacts.size())) + " contacts");
	
	Array<Identifier> keys;
	mContacts.getKeys(keys);
	std::random_shuffle(keys.begin(), keys.end());

	// Normal
	for(int i=0; i<keys.size(); ++i)
	{
		Contact *contact = NULL;
		if(mContacts.get(keys[i], contact))
		{
			if(contact->isDeleted()) continue;
			Desynchronize(this);
			contact->update(false);
		}
	}

	// Alternate
	// isPublicConnectable could be valid only for IPv6
	if(/*!Core::Instance->isPublicConnectable() &&*/ mUpdateCount > 0)
	{
		for(int i=0; i<keys.size(); ++i)
		{
			Contact *contact = NULL;
			if(mContacts.get(keys[i], contact))
			{
				if(contact->isDeleted()) continue;
				Desynchronize(this);
				contact->update(true);
			}
		}
	}
	
	LogDebug("AddressBook::update", "Finished");
	++mUpdateCount;
	save();
}

void AddressBook::http(const String &prefix, Http::Request &request)
{
	user()->setOnline();

	Synchronize(this);

	try {
		if(request.url.empty() || request.url == "/")
		{
			if(request.method == "POST")
			{
				try {
			  		String command = request.post["command"];
			  		if(command == "delete")
					{
				  		Identifier peering;
						request.post["argument"] >> peering;
						
						removeContact(peering);
					}
					else {
						String name, csecret;
						name = request.post["name"];
						csecret = request.post["secret"];
					  
						if(name.empty() || csecret.empty()) throw 400;
						
						ByteString secret;
						Sha512::Hash(csecret, secret, Sha512::CryptRounds);
						
						if(request.post.contains("self")) setSelf(secret);
						else addContact(name, secret);
					}
				}
				catch(const Exception &e)
				{
					LogWarn("AddressBook::http", e.what());
					throw 400;
				}				
				
				Http::Response response(request, 303);
				response.headers["Location"] = prefix + "/";
				response.send();
				return;
			}
			
			if(request.get.contains("json"))
			{
				Http::Response response(request, 200);
				response.headers["Content-Type"] = "application/json";
				response.send();

				Array<Identifier> keys;
 				mContacts.getKeys(keys);

				JsonSerializer json(response.sock);
				json.outputMapBegin();
				for(int i=0; i<keys.size(); ++i)
        			{
                			Contact *contact = NULL;
                			if(mContacts.get(keys[i], contact))
                			{
						if(contact->isDeleted()) continue;
                        			Desynchronize(this);

						StringMap map;
                                        	map["name"] << contact->name();
                                        	map["tracker"] << contact->tracker();
                                        	map["status"] << contact->status();
                                        	map["messages"] << contact->unreadMessagesCount();
						map["newmessages"] << contact->hasNewMessages();
                                        	json.outputMapElement(contact->uniqueName(), map);
					}
                		}
				json.outputMapEnd();
				return;
			}
			
			Http::Response response(request,200);
			response.send();
			
			Html page(response.sock);
			page.header("Contacts");
			
			Array<Contact*> contacts;
			getContacts(contacts);
			if(!contacts.empty())
			{
				page.open("div",".box");
				
				page.openForm(prefix+"/", "post", "executeForm");
				page.input("hidden", "command");
				page.input("hidden", "argument");
				page.closeForm();
				
				page.javascript("function deleteContact(name, identifier) {\n\
					if(confirm('Do you really want to delete '+name+' ?')) {\n\
						document.executeForm.command.value = 'delete';\n\
						document.executeForm.argument.value = identifier;\n\
						document.executeForm.submit();\n\
					}\n\
				}");
				
				page.open("table",".contacts");
				for(int i=0; i<contacts.size(); ++i)
				{
					Contact *contact = contacts[i];
					if(contact->isDeleted()) continue;
					String contactUrl = prefix + '/' + contact->uniqueName() + '/';
					
					page.open("tr");
					page.open("td",".name");
					page.link(contact->urlPrefix(), contact->name());
					page.close("td");
					page.open("td",".tracker");
					page.text(String("@") + contact->tracker());
					page.close("td");
					page.open("td",".checksum");
					page.text(String(" check: ")+String::hexa(contact->peeringChecksum(),8));
					page.close("td");
					page.open("td",".delete");
					page.openLink("javascript:deleteContact('"+contact->name()+"','"+contact->peering().toString()+"')");
					page.image("/delete.png", "Delete");
					page.closeLink();
					page.close("td");
					page.close("tr");
				}
				page.close("table");
				page.close("div");
			}
			
			page.openForm(prefix+"/","post");
			page.openFieldset("New contact");
			page.label("name","Name"); page.input("text","name"); page.br();
			page.label("secret","Secret"); page.input("text","secret","",true); page.br();
			page.label("add"); page.button("add","Add contact");
			page.closeFieldset();
			page.closeForm();
			
			page.openForm(prefix+"/","post");
			page.openFieldset("Personal secret");
			page.input("hidden","name",userName());
			page.input("hidden","self","true");
			if(getSelf()) page.text("Your personal secret is already set, but you can change it here.");
			else page.text("Set the same username and the same personal secret on multiple devices to enable automatic synchronization.");
			page.br();
			page.br();
			page.label("secret","Secret"); page.input("text","secret","",true); page.br();
			page.label("add"); page.button("add","Set secret");
			page.closeFieldset();
			page.closeForm();
			
			page.footer();
			return;
		}
	}
	catch(const Exception &e)
	{
		LogWarn("AddressBook::http",e.what());
		throw 500;	// Httpd handles integer exceptions
	}
	
	throw 404;
}

void AddressBook::run(void)
{
	update();
}

bool AddressBook::publish(const Identifier &remotePeering)
{
	try {
		String url("http://" + Config::Get("tracker") + "/tracker?id=" + remotePeering.toString());
		
		List<Address> list;
		Config::GetExternalAddresses(list);
		
		String addresses;
		for(	List<Address>::iterator it = list.begin();
			it != list.end();
			++it)
		{
			if(!addresses.empty()) addresses+= ',';
			addresses+= it->toString();
		}
		
		StringMap post;
		post["instance"] = Core::Instance->getName();
		post["port"] = Config::Get("port");
		post["addresses"] = addresses;
		
		if(!Core::Instance->isPublicConnectable())
		{
			list.clear();
			Core::Instance->getKnownPublicAdresses(list);
			
			String altAddresses;
			for(	List<Address>::iterator it = list.begin();
				it != list.end();
				++it)
			{
				if(!altAddresses.empty()) altAddresses+= ',';
				altAddresses+= it->toString();
			}
			
			post["alternate"] = altAddresses;
		}
		
		return (Http::Post(url, post) == 200);
	}
	catch(const NetException &e)
	{
		LogDebug("AddressBook::publish", e.what()); 
		return false;
	}
	catch(const std::exception &e)
	{
		LogWarn("AddressBook::publish", e.what()); 
		return false;
	}
}

bool AddressBook::query(const Identifier &peering, const String &tracker, AddressMap &output, bool alternate)
{
	output.clear();
  
	try {
	  	String url;
	  	if(tracker.empty()) url = "http://" + Config::Get("tracker") + "/tracker?id=" + peering.toString();
		else url = "http://" + tracker + "/tracker?id=" + peering.toString();
  		if(alternate) url+= "&alternate=1";
		  
		String tmp;
		if(Http::Get(url, &tmp) != 200) return false;
		tmp.trim();
		if(tmp.empty()) return false;
	
		YamlSerializer serializer(&tmp);
		typedef SerializableMap<String, SerializableArray<Address> > content_t;
		content_t content;
		
		while(true)
		try {
			serializer.input(content);
			break;
		}
		catch(const InvalidData &e)
		{
			 
		}
		
		for(content_t::const_iterator it = content.begin();
			it != content.end();
			++it)
		{
			const String &instance = it->first;
			const Array<Address> &addrs = it->second;
			for(int i=0; i<addrs.size(); ++i)
				output[instance].insert(addrs[i], Time::Now());
		}
		
		return !output.empty();
	}
	catch(const NetException &e)
	{
		return false;
	}
	catch(const std::exception &e)
	{
		LogWarn("AddressBook::query", e.what()); 
		return false;
	}
}

AddressBook::Contact::Contact(	AddressBook *addressBook, 
				const String &uname,
				const String &name,
			        const String &tracker,
			        const ByteString &secret) :
	mAddressBook(addressBook),
	mUniqueName(uname),
	mName(name),
	mTracker(tracker),
	mSecret(secret),
	mTime(Time::Now()),
	mDeleted(false),
	mFound(false)
{	
	Assert(addressBook != NULL);
	Assert(!uname.empty());
	Assert(!name.empty());
	Assert(!tracker.empty());
	Assert(!secret.empty());
	
	// Compute peering
	String agregate;
	agregate.writeLine(mSecret);
	agregate.writeLine(mAddressBook->userName());
	agregate.writeLine(mName);
	Sha512::Hash(agregate, mPeering, Sha512::CryptRounds);
	
	// Compute Remote peering
	agregate.clear();
	agregate.writeLine(mSecret);
	agregate.writeLine(mName);
	agregate.writeLine(mAddressBook->userName());
	Sha512::Hash(agregate, mRemotePeering, Sha512::CryptRounds);
}

AddressBook::Contact::Contact(AddressBook *addressBook) :
  	mAddressBook(addressBook),
	mMessagesCount(0),
	mHasNewMessages(false)
{
  
}

AddressBook::Contact::~Contact(void)
{
	Interface::Instance->remove(urlPrefix(), this);
}

String AddressBook::Contact::uniqueName(void) const
{
	Synchronize(this);
	return mUniqueName;
}

String AddressBook::Contact::name(void) const
{
	Synchronize(this);
	return mName;
}

String AddressBook::Contact::tracker(void) const
{
	Synchronize(this);
	return mTracker;
}

Identifier AddressBook::Contact::peering(void) const
{
	Synchronize(this);
	return mPeering;
}

Identifier AddressBook::Contact::remotePeering(void) const
{
	Synchronize(this);
	return mRemotePeering;
}

Time AddressBook::Contact::time(void) const
{
	Synchronize(this);
	return mTime; 
}

uint32_t AddressBook::Contact::peeringChecksum(void) const
{
	Synchronize(this);
	return mPeering.getDigest().checksum32() + mRemotePeering.getDigest().checksum32(); 
}

String AddressBook::Contact::urlPrefix(void) const
{
	Synchronize(this);
	if(mUniqueName.empty()) return "";
	if(isSelf()) return String("/")+mAddressBook->userName()+"/myself";
	return String("/")+mAddressBook->userName()+"/contacts/"+mUniqueName;
}

int AddressBook::Contact::unreadMessagesCount(void) const
{
	Synchronize(this);

	int count = 0;
	for(int i=mMessages.size()-1; i>=0; --i)
	{
		if(mMessages[i].isRead()) break;
		++count;
	}
	return count;
}

bool AddressBook::Contact::hasNewMessages(void)
{
	bool tmp = false;
	std::swap(mHasNewMessages, tmp);
	return tmp;
}

bool AddressBook::Contact::isSelf(void) const
{
	return (mUniqueName == mAddressBook->userName());
}

bool AddressBook::Contact::isFound(void) const
{
	return mFound;
}

bool AddressBook::Contact::isConnected(void) const
{
	Synchronize(this);
	return Core::Instance->hasPeer(mPeering); 
}

bool AddressBook::Contact::isConnected(const String &instance) const
{
	Synchronize(this);
	return Core::Instance->hasPeer(Identifier(mPeering, instance)); 
}

bool AddressBook::Contact::isOnline(void) const
{
	Synchronize(this);
	
	if(isSelf() && mAddressBook->user()->isOnline()) return true;
	if(!isConnected()) return false;
	if(!mInfo.contains("last")) return false;
	return (Time::Now()-Time(mInfo.get("last")) < 60);	// 60 sec
}

String AddressBook::Contact::status(void) const
{
	Synchronize(this);
	
	if(isSelf()) return "myself";
	if(isOnline()) return "online";
	else if(isConnected()) return "connected";
	else if(isFound()) return "found";
	else return "disconnected";
}

AddressBook::AddressMap AddressBook::Contact::addresses(void) const
{
	Synchronize(this);
	return mAddrs;
}

bool AddressBook::Contact::isDeleted(void) const
{
	Synchronize(this);
	return mDeleted;
}

void AddressBook::Contact::setDeleted(void)
{
	Synchronize(this);
	mDeleted = true;
	mTime = Time::Now();
}

void AddressBook::Contact::getInstances(Array<String> &array)
{
	mAddrs.getKeys(array);
}

bool AddressBook::Contact::addAddresses(const AddressMap &map)
{
	Synchronize(this);
  
	for(AddressMap::const_iterator it = map.begin();
		it != map.end();
		++it)
	{
		const String &instance = it->first;
		const AddressBlock &block = it->second;
		mAddrs.insert(instance, block);
	}
	
	return true;
}

bool AddressBook::Contact::connectAddress(const Address &addr, const String &instance, bool save)
{
 	Synchronize(this);
	
	if(addr.isNull()) return false;
	if(instance == Core::Instance->getName()) return false;
	
	LogDebug("AddressBook::Contact", "Connecting " + instance + " on " + addr.toString() + "...");
	
	Identifier identifier(mPeering, instance);
	try {
		Desynchronize(this);
		Socket *sock = new Socket(addr, 2000);	// TODO: timeout
		if(Core::Instance->addPeer(sock, identifier))
		{
			if(save) SynchronizeStatement(this, mAddrs[instance][addr] = Time::Now());	
			return true;
		}
	}
	catch(...)
	{
		return false;
	}

	// A node is running at this address but the user does not exist
	if(mAddrs.contains(instance)) mAddrs[instance].erase(addr);
	return false; 
}

bool AddressBook::Contact::connectAddresses(const AddressMap &map, bool save, bool shuffle)
{
	Synchronize(this);
  
	bool success = false;
	for(AddressMap::const_iterator it = map.begin();
		it != map.end();
		++it)
	{
		const String &instance = it->first;
		const AddressBlock &block = it->second;
	  
		if(instance == Core::Instance->getName()) continue;

	  	// TODO: look for a better address than the already connected one
		if(isConnected(instance)) 
		{
			// TODO: update time for currenly connected address
			success = true;
			continue;
		}

		Array<Address> addrs;
		block.getKeys(addrs);
		if(shuffle) std::random_shuffle(addrs.begin(), addrs.end());
		
		LogDebug("AddressBook::Contact", "Connecting instance " + instance + " for " + mName + " (" + String::number(addrs.size()) + " address(es))...");
		
		for(Array<Address>::const_reverse_iterator jt = addrs.rbegin();
			jt != addrs.rend();
			++jt)
		{
			if(connectAddress(*jt, instance, save))
			{
				success = true;
				break;
			}
		}
			
	}
	
	return success;
}

void AddressBook::Contact::update(bool alternate)
{
	Synchronize(this);
	if(mDeleted) return;
	Core::Instance->registerPeering(mPeering, mRemotePeering, mSecret, this);
	
	LogDebug("AddressBook::Contact", "Looking for " + mUniqueName);
	
	if(mPeering != mRemotePeering && Core::Instance->hasRegisteredPeering(mRemotePeering))	// the user is local
	{
		Identifier identifier(mPeering, Core::Instance->getName());
		if(!Core::Instance->hasPeer(identifier))
		{
			LogDebug("AddressBook::Contact", mUniqueName + " found locally");
			  
			Address addr("127.0.0.1", Config::Get("port"));
			try {
				Desynchronize(this);
				Socket *sock = new Socket(addr);
				Core::Instance->addPeer(sock, identifier);
			}
			catch(...)
			{
				LogDebug("AddressBook::Contact", "Warning: Unable to connect the local core");	 
			}
		}
	}
	
	if(!alternate) 
	{
		LogDebug("AddressBook::Contact", "Publishing to tracker " + mTracker + " for " + mUniqueName);
		Identifier remotePeering(mRemotePeering);
		DesynchronizeStatement(this, AddressBook::publish(remotePeering));
	}
		  
	LogDebug("AddressBook::Contact", "Querying tracker " + mTracker + " for " + mUniqueName);	
	AddressMap newAddrs;
	Identifier peering(mPeering);
	String tracker(mTracker);
	DesynchronizeStatement(this, AddressBook::query(peering, tracker, newAddrs, alternate));
	
	if(!newAddrs.empty())
	{
		if(!alternate) LogDebug("AddressBook::Contact", "Contact " + mName + " found (" + String::number(newAddrs.size()) + " instance(s))");
		else LogDebug("AddressBook::Contact", "Alternative addresses for " + mName + " found (" + String::number(newAddrs.size()) + " instance(s))");
		mFound = true;
		connectAddresses(newAddrs, !alternate, alternate);
	}
	else if(!alternate) 
	{
		mFound = false;
		AddressMap tmpAddrs(mAddrs);
		connectAddresses(tmpAddrs, true, false);
	}
	
	AddressMap::iterator it = mAddrs.begin();
	while(it != mAddrs.end())
	{
		const String &instance = it->first;
		AddressBlock &block = it->second;
		AddressBlock::iterator jt = block.begin();
		while(jt != block.end())
		{
			if(Time::Now() - jt->second >= 3600*24*8)	// 8 days
				block.erase(jt++);
			else jt++;
		}
		
		if(block.empty())
			mAddrs.erase(it++);
		else it++;
	}

        while(!mMessages.empty()
		&& mMessages.front().isRead()
		&& Time::Now() - mMessages.front().time() >= 7200)        // 2h
	{
		mMessages.pop_front();
	}
}

void AddressBook::Contact::connected(const Identifier &peering)
{
	Synchronize(this);
	Assert(peering == mPeering);	

	// Send info
	mAddressBook->user()->sendInfo(peering);

	// Send contacts if self
        if(isSelf())
        {
		Desynchronize(this);
                String data;
        	mAddressBook->save(data);
                Message message(data);
                message.setParameter("type", "contacts");
                message.send(peering);
	}
}

void AddressBook::Contact::disconnected(const Identifier &peering)
{
	Synchronize(this);
	Assert(peering == mPeering);
	
	//update(false);
	//update(true);
}

void AddressBook::Contact::message(Message *message)
{
	{
		Synchronize(this);
		Assert(message);
		Assert(message->receiver() == mPeering);
		if(mDeleted) return;
	}
	
	String type;
	message->parameters().get("type", type);
	
	if(type.empty() || type == "text")
	{
		Synchronize(this);
		
		mMessages.push_back(*message);
		++mMessagesCount;
		mHasNewMessages = true;
		notifyAll();
	}
	else if(type == "info")
	{
		Synchronize(this);
	  
		String data = message->content();
		YamlSerializer serializer(&data);
		StringMap info;
		serializer.input(info);
		
		// TODO: variables for time and last
		
		Time l1(info.getOrDefault("last", Time(0)));
		Time l2(mInfo.getOrDefault("last", Time(0)));
		l1 = std::min(l1, Time::Now());
		l2 = std::min(l2, Time::Now());
		String last = std::max(l1,l2).toString();
		
		Time t1(info.getOrDefault("time", Time(0)));
		Time t2(mInfo.getOrDefault("time", Time(0)));
		t1 = std::min(t1, Time::Now());
		t2 = std::min(t2, Time::Now());
		if(t1 > t2)
		{
			info["last"] = last;
			mInfo = info;
			
			if(isSelf())
			{
				Desynchronize(this);
				mAddressBook->user()->setInfo(info);
			}
		}
		else {
			mInfo["last"] = last;
		}
	}
	else if(type == "contacts")
	{
		if(SynchronizeTest(this, mUniqueName != mAddressBook->userName()))
		{
			LogWarn("AddressBook::Contact::message", "Received contacts update from other than self, dropping");
			return;
		}
		
		// DO NOT synchronize here, as the contact could disappear during load
		String data = message->content();
		mAddressBook->load(data);
	}
}

void AddressBook::Contact::request(Request *request)
{
	Assert(request);
	if(!mDeleted) request->execute(mAddressBook->user());
	else request->executeDummy();
}

void AddressBook::Contact::http(const String &prefix, Http::Request &request)
{
	mAddressBook->user()->setOnline();

	Synchronize(this);
	
	try {
		if(request.url.empty() || request.url == "/")
		{
			if(request.get.contains("json"))
			{
				Http::Response response(request, 200);
				response.headers["Content-Type"] = "application/json";
				response.send();
				
				JsonSerializer json(response.sock);
				json.outputMapBegin();
				json.outputMapElement(String("name"), mName);
				json.outputMapElement(String("tracker"), mTracker);
                                json.outputMapElement(String("status"), status());
                                json.outputMapElement(String("messages"), unreadMessagesCount());
				json.outputMapElement(String("newmessages"), hasNewMessages());
				
				Array<String> instances;
 				getInstances(instances);
				StringMap map;
				for(int i=0; i<instances.size(); ++i)
        			{
                			if(isConnected(instances[i])) map[instances[i]] = "connected";
					else map[instances[i]] = "disconnected";
                		}
				
                		json.outputMapElement(String("instances"), map);
				json.outputMapEnd();
				return;
			}
		  
			Http::Response response(request,200);
			response.send();

			Html page(response.sock);
			if(isSelf()) page.header("Myself");
			else page.header("Contact: "+mName);
				
			if(mUniqueName != mAddressBook->userName())
			{
				page.open("div", "topmenu");
				page.span("Status:", ".title");
				page.span("", "status.status");
				page.close("div");
			}
			
			page.open("div",".box");
				
			page.open("table", ".menu");
			page.open("tr");
			page.open("td");
				page.openLink(prefix + "/files/");
				page.image("/icon_files.png", "Files", ".bigicon");
				page.closeLink();
			page.close("td");
			page.open("td", ".title");
				page.text("Files");
			page.close("td");
			page.close("tr");
			page.open("tr");
			page.open("td");
				page.openLink(prefix + "/search/");
				page.image("/icon_search.png", "Search", ".bigicon");
				page.closeLink();
			page.close("td");
			page.open("td", ".title");
				page.text("Search");
			page.close("td");
			page.close("tr");
			
			if(mUniqueName != mAddressBook->userName())
			{
				page.open("tr");
				page.open("td");
					page.openLink(prefix + "/chat/");
					page.image("/icon_chat.png", "Chat", ".bigicon");
					page.closeLink();
				page.close("td");
				page.open("td",".title");
					page.text("Chat");
					page.span("", "messagescount.messagescount");
				page.close("td");
				page.close("tr");
			}
			
			page.close("table");
			page.close("div");
			
			page.open("div",".box");
			page.open("h2");
			page.text("Instances");
			page.close("h2");
			page.open("table", "instances");
			page.close("table");
			page.close("div");
			
			unsigned refreshPeriod = 5000;
			page.javascript("setInfoCallback(\""+prefix+"/?json\", "+String::number(refreshPeriod)+", function(info) {\n\
				transition($('#status'),\n\
					'<span class=\"'+info.status+'\">'+info.status.capitalize()+'</span>\\n');\n\
				var msg = '';\n\
				if(info.messages != 0) msg = ' ('+info.messages+')';\n\
				transition($('#messagescount'), msg);\n\
				$('#instances').empty();\n\
				if($.isEmptyObject(info.instances)) $('#instances').text('No connected instance');\n\
				else $.each(info.instances, function(instance, status) {\n\
					$('#instances').append($('<tr>')\n\
						.addClass(status)\n\
						.append($('<td>').addClass('name').text(instance))\n\
						.append($('<td>').addClass('status').text(status.capitalize())));\n\
				});\n\
			});");
			
			page.footer();
			return;
		}
		else {
			String url = request.url;
			String directory = url;
			directory.ignore();		// remove first '/'
			url = "/" + directory.cut('/');
			if(directory.empty()) throw 404;
			  
			if(request.get.contains("play"))
			{			  	
				String host;
				if(!request.headers.get("Host", host))
				host = String("localhost:") + Config::Get("interface_port");
					 
				Http::Response response(request, 200);
				response.headers["Content-Disposition"] = "attachment; filename=\"stream.m3u\"";
				response.headers["Content-Type"] = "audio/x-mpegurl";
				response.send();
				
				String link = "http://" + host + prefix + request.url + "?file=1";
				String instance;
				if(request.get.get("instance", instance))
					link+= "&instance=" + instance; 
				
				response.sock->writeLine("#EXTM3U");
				response.sock->writeLine(String("#EXTINF:-1, ") + APPNAME + " stream");
				response.sock->writeLine(link);
				return;
			}
			
			if(directory == "files")
			{
				String target(url);
				
				bool isFile = request.get.contains("file");
				
				if(isFile)
				{
					while(!target.empty() && target[target.size()-1] == '/')
						target.resize(target.size()-1);
				}
				else {
					if(target.empty() || target[target.size()-1] != '/') 
						target+= '/';
				}
				
				Request trequest(target, isFile);
				if(isFile) trequest.setContentSink(new TempFile);
				
				String instance;
				request.get.get("instance", instance);
				
				// Self
				if(isSelf()
					&& (instance.empty() || instance == Core::Instance->getName()))
				{
					trequest.execute(mAddressBook->user());
				}
				
				try {
					const unsigned timeout = Config::Get("request_timeout").toInt();
				  	if(!instance.empty()) trequest.submit(Identifier(mPeering, instance));
					else trequest.submit(mPeering);
					Desynchronize(this);
					trequest.wait(timeout);
				}
				catch(const Exception &e)
				{
					// If not self
					if(mUniqueName != mAddressBook->userName())
					{
						LogWarn("AddressBook::Contact::http", "Cannot send request, peer not connected");

						Http::Response response(request, 404);
						response.send();
						Html page(response.sock);
						page.header(response.message, true);
						page.open("div", "error");
						page.openLink("/");
						page.image("/error.png", "Error");
						page.closeLink();
						page.br();
						page.open("h1",".huge");
						page.text(String::number("Not connected"));
						page.close("h1");
						page.close("div");
						page.footer();
						return;
					}
				}
				
				if(trequest.responsesCount() == 0)
				{
					Http::Response response(request, 404);
					response.send();
					Html page(response.sock);
					page.header("No response", true, request.fullUrl);
					page.open("div", "error");
					page.openLink("/");
					page.image("/error.png", "Error");
					page.closeLink();
					page.br();
					page.open("h1",".huge");
					page.text(String::number("No response, retrying..."));
					page.close("h1");
					page.close("div");
					page.footer();
					return;
				}
				
				if(!trequest.isSuccessful()) throw 404;
					
				try {
					if(request.get.contains("file"))
					{
						for(int i=0; i<trequest.responsesCount(); ++i)
						{
					  		Request::Response *tresponse = trequest.response(i);
							if(tresponse->error()) continue;
							StringMap params = tresponse->parameters();
					 		if(!params.contains("name")) continue;
							ByteStream *content = tresponse->content();
							if(!content) continue;
							
							Time time = Time::Now();
							if(params.contains("time")) params.get("time").extract(time);
							
							Http::Response response(request,200);
							response.headers["Last-Modified"] = time.toHttpDate();
							if(params.contains("size")) response.headers["Content-Length"] = params.get("size");
							
							if(request.get.contains("download"))
							{
							 	response.headers["Content-Disposition"] = "attachment; filename=\"" + params.get("name") + "\"";
								response.headers["Content-Type"] = "application/octet-stream";
							}
							else {
								response.headers["Content-Disposition"] = "inline; filename=\"" + params.get("name") + "\"";
								response.headers["Content-Type"] = Mime::GetType(params.get("name"));
							}
														
							response.send();
							
							try {
								Desynchronize(this);
								response.sock->writeBinary(*content);
							}
							catch(const NetException &e)
							{
				  
							}
							
							return;
						}
						
						throw 404;
					}
					
					Http::Response response(request, 200);
					
					bool playlistMode = request.get.contains("playlist");
					if(playlistMode)
					{
					 	response.headers["Content-Disposition"] = "attachment; filename=\"playlist.m3u\"";
						response.headers["Content-Type"] = "audio/x-mpegurl";
					}
					
					response.send();
					
					Html page(response.sock);
					
					if(!playlistMode)
					{
						if(target.empty() || target == "/") page.header(mName+": Browse files");
						else page.header(mName+": "+target.substr(1));
						page.open("div","topmenu");
						page.link(prefix+"/search/","Search files");
						page.br();
						page.close("div");
					}
					
					page.listFilesFromRequest(trequest, prefix, request, mAddressBook->user(), playlistMode);
					
					if(!playlistMode) page.footer();
				}
				catch(const Exception &e)
				{
					LogWarn("AddressBook::Contact::http", String("Unable to access remote file or directory: ") + e.what());
				}
				
				return;
			}
			else if(directory == "search")
			{
				if(url != "/") throw 404;
				
				String query;
				if(request.post.contains("query"))
				{
					query = request.post.get("query");
					query.trim();
				}
				
				Http::Response response(request,200);
				response.send();
				
				Html page(response.sock);
				if(query.empty()) page.header(mName + ": Search");
				else page.header(mName + ": Searching " + query);
				page.openForm(prefix + "/search", "post", "searchForm");
				page.input("text","query",query);
				page.button("search","Search");
				page.closeForm();
				page.javascript("$(document).ready(function() { document.searchForm.query.focus(); });");
				page.br();
				
				if(query.empty())
				{
					page.footer();
					return;
				}
				
				Request trequest("search:"+query, false);	// no data
				try {
					trequest.submit(mPeering);
				}
				catch(const Exception &e)
				{
					LogWarn("AddressBook::Contact::http", "Cannot send request, peer not connected");
					page.open("div",".box");
					page.text("Not connected...");
					page.close("div");
					page.footer();
					return;
				}
				
				{
					const unsigned timeout = Config::Get("request_timeout").toInt();
					Desynchronize(this);
					trequest.wait(timeout);
				}
				
				page.listFilesFromRequest(trequest, prefix, request, mAddressBook->user());
				page.footer();
				return;
			}
			else if(directory == "chat")
			{
				if(isSelf()) throw 404;
			  
				if(url != "/")
				{
				  	url.ignore();
					unsigned count = 0;
					try { url>>count; }
					catch(...) { throw 404; }
					
					Http::Response response(request,200);
					response.send();
					
					if(count == mMessagesCount)
					{
						mHasNewMessages = false;
						wait(120000);
					}
					
					if(count < mMessagesCount && mMessagesCount-count <= mMessages.size())
					{
						Html html(response.sock);
						int i = mMessages.size() - (mMessagesCount-count);
						messageToHtml(html, mMessages[i], false);
						mMessages[i].markRead();
					}
					
					return;
				}
			  
				if(request.method == "POST")
				{
					if(request.post.contains("message") && !request.post["message"].empty())
					{
						try {
							Message message(request.post["message"]);
						  	message.send(mPeering);	// send to other
							
							// TODO
							//Contact *self = mAddressBook->getSelf();
							//if(self && self->isConnected()) message.send(self->peering());
							
							mMessages.push_back(Message(request.post["message"]));	// thus receiver is null
							++mMessagesCount;
							notifyAll();
							
							if(request.post.contains("ajax") && request.post["ajax"].toBool())	//ajax
							{
								Http::Response response(request, 200);
								response.send();
								/*Html html(response.sock);
								messageToHtml(html, mMessages.back(), false);
								mMessages.back().markRead();*/
							}
							else {	// form submit
							 	Http::Response response(request, 303);
								response.headers["Location"] = prefix + "/chat";
								response.send();
							}
						}
						catch(const Exception &e)
						{
							LogWarn("AddressBook::Contact::http", String("Cannot post message: ") + e.what());
							throw 409;
						}
						
						return;
					}
				}
			  
				bool isPopup = request.get.contains("popup");
			  
				Http::Response response(request,200);
				response.send();	
				
				Html page(response.sock);
				page.header("Chat with "+mName, isPopup);
				if(isPopup)
				{
					page.open("div","topmenu");
					page.open("b");
					page.text("Chat with "+mName+" - ");
					page.span("", "status.status");
					page.close("b");
					page.close("div");
					page.open("div", "chat");
				}
				else {
					String popupUrl = prefix + "/chat?popup=1";
					page.open("div","topmenu");
#ifndef ANDROID
					page.raw("<a href=\""+popupUrl+"\" target=\"_blank\" onclick=\"return popup('"+popupUrl+"','/');\">Popup</a>");
					page.text(" - ");
#endif
					page.span("", "status.status");
					page.close("div");
					page.open("div", "chat.box");
				}
				
				page.open("div", "chatmessages");
				for(int i=0; i<mMessages.size(); ++i)
				{
	  				messageToHtml(page, mMessages[i], mMessages[i].isRead());
					mMessages[i].markRead();
				}
				page.close("div"); // chatmessages
				
				page.open("div", "chatpanel");
				page.openForm(prefix + "/chat", "post", "chatForm");
				page.textarea("message");
				//page.button("send","Send");
				//page.br();
				page.closeForm();
				page.javascript("$(document).ready(function() { document.chatForm.message.focus(); });");
				page.close("div"); // chatpanel
				
				page.close("div");
				
				page.javascript("var count = "+String::number(mMessagesCount)+";\n\
					var title = document.title;\n\
					var hasFocus = true;\n\
					var nbNewMessages = 0;\n\
					$(window).blur(function() {\n\
						hasFocus = false;\n\
						$('span.message').attr('class', 'oldmessage');\n\
					});\n\
					$(window).focus(function() {\n\
						hasFocus = true;\n\
						nbNewMessages = 0;\n\
						document.title = title;\n\
					});\n\
					function update()\n\
					{\n\
						var request = $.ajax({\n\
							url: '"+prefix+"/chat/'+count,\n\
							dataType: 'html',\n\
							timeout: 300000\n\
						});\n\
						request.done(function(html) {\n\
							if($.trim(html) != '')\n\
							{\n\
								$('#chatmessages').append(html);\n\
								var text = $('#chatmessages span.text:first');\n\
								if(text) text.html(text.html().linkify());\n\
								$('#chatmessages').scrollTop($('#chatmessages')[0].scrollHeight);\n\
								if(!hasFocus)\n\
								{\n\
									nbNewMessages+= 1;\n\
									document.title = title+' ('+nbNewMessages+')';\n\
								}\n\
								count+= 1;\n\
								playMessageSound();\n\
							}\n\
							setTimeout('update()', 100);\n\
						});\n\
						request.fail(function(jqXHR, textStatus) {\n\
							setTimeout('update()', 10000);\n\
						});\n\
					}\n\
					function post()\n\
					{\n\
						var message = document.chatForm.message.value;\n\
						if(!message) return false;\n\
						document.chatForm.message.value = '';\n\
						var request = $.post('"+prefix+"/chat',\n\
							{ 'message': message, 'ajax': 1 });\n\
						request.fail(function(jqXHR, textStatus) {\n\
							alert('The message could not be sent. Is this user online ?');\n\
						});\n\
					}\n\
					document.chatForm.onsubmit = function()\n\
					{\n\
						post();\n\
						return false;\n\
					}\n\
					$('textarea.message').keypress(function(e) {\n\
    						if (e.keyCode == 13 && !e.shiftKey) {\n\
       							e.preventDefault();\n\
        						post();\n\
    						}\n\
					});\n\
					setTimeout('update()', 1000);\n\
					$(document).ready( function() {\n\
						$('#chatmessages').scrollTop($('#chatmessages')[0].scrollHeight);\n\
					});");
				
				unsigned refreshPeriod = 5000;
				page.javascript("setInfoCallback(\""+prefix+"/?json\", "+String::number(refreshPeriod)+", function(info) {\n\
					transition($('#status'),\n\
						'<span class=\"'+info.status+'\">'+info.status.capitalize()+'</span>\\n');\n\
					if(info.newmessages) playMessageSound();\n\
				});");
				
				page.footer();
				return;
			}
		}
	}
	catch(const NetException &e)
	{
		throw;
	}
	catch(const Exception &e)
	{
		LogWarn("AddressBook::Contact::http", e.what());
		throw 500;
	}
	
	throw 404;
}

void AddressBook::Contact::messageToHtml(Html &html, const Message &message, bool old) const
{
	Desynchronize(this);

	if(old) html.open("span",".oldmessage");
	else html.open("span",".message");
	html.open("span",".date");
	html.text(message.time().toDisplayDate());
	html.close("span");
	html.text(" ");
	if(message.receiver() == Identifier::Null) html.open("span",".out");
	else html.open("span",".in");
	html.open("span",".user");
	if(message.receiver() == Identifier::Null) html.text(mAddressBook->userName());
	else html.text(mAddressBook->getContact(message.receiver())->name());
	html.close("span");
	html.text(": ");
	html.open("span",".text");
	html.text(message.content());
	html.close("span");
	html.close("span");
	html.close("span");
	html.br(); 
}

void AddressBook::Contact::copy(const AddressBook::Contact *contact)
{
	Assert(contact);
	Synchronize(this);
	
	{
		Synchronize(contact);
		
		mUniqueName = contact->mUniqueName;
		mName = contact->mName;
		mTracker = contact->mTracker;
		mSecret = contact->mSecret;
		mPeering = contact->mPeering;
		mRemotePeering = contact->mRemotePeering;
		mTime = contact->mTime;
		mDeleted = contact->mDeleted;
	}
	
	addAddresses(contact->addresses());
}

void AddressBook::Contact::serialize(Serializer &s) const
{
	Synchronize(this);
	
	StringMap map;
	map["uname"] << mUniqueName;
	map["name"] << mName;
	map["tracker"] << mTracker;
	map["secret"] << mSecret;
	map["peering"] << mPeering;
	map["remote"] << mRemotePeering;
	map["time"] << mTime;
	map["deleted"] << mDeleted;
	
	s.outputMapBegin(2);
	s.outputMapElement(String("info"),map);
	s.outputMapElement(String("addrs"),mAddrs);
	s.outputMapEnd();
}

bool AddressBook::Contact::deserialize(Serializer &s)
{
	Synchronize(this);
	
	mUniqueName.clear();
  	mName.clear();
	mTracker.clear();
	mSecret.clear();
	mPeering.clear();
	mRemotePeering.clear();
	
	StringMap map;
	
	String key;
	AssertIO(s.inputMapBegin());
	AssertIO(s.inputMapElement(key, map) && key == "info");
	AssertIO(s.inputMapElement(key, mAddrs) && key == "addrs");

	map["uname"] >> mUniqueName;
	map["name"] >> mName;
	map["tracker"] >> mTracker;
	map["secret"] >> mSecret;
	map["peering"] >> mPeering;
	map["remote"] >> mRemotePeering;

	if(map.contains("time")) map["time"] >> mTime;
	else mTime = Time::Now();
	
	if(map.contains("deleted")) map["deleted"] >> mDeleted;
	else mDeleted = false;
	
	// TODO: checks
	
	mMessages.clear();
	mMessagesCount = 0;
	mFound = false;
	return true;
}

bool AddressBook::Contact::isInlineSerializable(void) const
{
	return false; 
}

}
