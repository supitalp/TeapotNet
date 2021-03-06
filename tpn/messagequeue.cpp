/*************************************************************************
 *   Copyright (C) 2011-2013 by Paul-Louis Ageneau                       *
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

#include "tpn/messagequeue.h"
#include "tpn/user.h"
#include "tpn/html.h"
#include "tpn/sha512.h"
#include "tpn/yamlserializer.h"
#include "tpn/jsonserializer.h"
#include "tpn/notification.h"
#include "tpn/splicer.h"

namespace tpn
{

MessageQueue::MessageQueue(User *user) :
	mUser(user),
	mHasNew(false)
{
	if(mUser) mDatabase = new Database(mUser->profilePath() + "messages.db");
	else mDatabase = new Database("messages.db");
	
	// TODO: backward compatibility, sould be removed (27/10/2013)
	try {
		mDatabase->execute("CREATE INDEX IF NOT EXISTS contact ON messages (contact)");
	}
	catch(...)
	{
		mDatabase->execute("DROP TABLE IF EXISTS messages");
	}
	//
	
	// WARNING: Additionnal fields in messages should be declared in erase()
	mDatabase->execute("CREATE TABLE IF NOT EXISTS messages\
	(id INTEGER PRIMARY KEY AUTOINCREMENT,\
	stamp TEXT UNIQUE NOT NULL,\
	parent TEXT,\
	headers TEXT,\
	content TEXT,\
	author TEXT,\
	signature TEXT,\
	contact TEXT NOT NULL,\
	time INTEGER(8) NOT NULL,\
	public INTEGER(1) NOT NULL,\
	incoming INTEGER(1) NOT NULL,\
	relayed INTEGER(1) NOT NULL,\
	isread INTEGER(1) NOT NULL)");

	mDatabase->execute("CREATE INDEX IF NOT EXISTS stamp ON messages (stamp)");
	mDatabase->execute("CREATE INDEX IF NOT EXISTS contact ON messages (contact)");
	mDatabase->execute("CREATE INDEX IF NOT EXISTS time ON messages (time)");
	
	mDatabase->execute("CREATE TABLE IF NOT EXISTS received\
	(stamp TEXT NOT NULL,\
	contact TEXT NOT NULL)");
	
	try {
		mDatabase->execute("CREATE UNIQUE INDEX IF NOT EXISTS contact_stamp ON received (contact,stamp)");
	}
	catch(...)
	{
		// TODO: backward compatibility, should be removed (08/12/2013)
		// Delete duplicates from table and recreate unique index
		mDatabase->execute("DELETE FROM received WHERE rowid NOT IN (SELECT MIN(rowid) FROM received GROUP BY contact, stamp)");
		mDatabase->execute("CREATE UNIQUE INDEX IF NOT EXISTS contact_stamp ON received (contact,stamp)");
	}
	
	Interface::Instance->add("/"+mUser->name()+"/messages", this);
}

MessageQueue::~MessageQueue(void)
{
	Interface::Instance->remove("/"+mUser->name()+"/messages", this);

	delete mDatabase;
}

User *MessageQueue::user(void) const
{
	return mUser;
}

bool MessageQueue::hasNew(void) const
{
	Synchronize(this);
	
	bool old = mHasNew;
	mHasNew = false;
	return old;
}

bool MessageQueue::add(Message &message)
{
	Synchronize(this);
	
	if(message.stamp().empty()) 
	{
		LogWarn("MessageQueue::add", "Message with empty stamp, dropping");
		return false;
	}
	
	if(!message.isIncoming()) message.writeSignature(user());
	
	bool exist = false;
	Message oldMessage;
	Database::Statement statement = mDatabase->prepare("SELECT * FROM messages WHERE stamp=?1");
	statement.bind(1, message.stamp());
	if(statement.step())
	{
		exist = true;
		statement.input(oldMessage);
	}
	statement.finalize();
	
	LogDebug("MessageQueue::add", "Adding message '"+message.stamp()+"'"+(exist ? " (already in queue)" : ""));

	if(exist && (message.isRelayed() || !oldMessage.isRelayed()))
	{
		if(message.isIncoming())
		{
			// Allow resetting time to prevent synchronization problems
			Database::Statement statement = mDatabase->prepare("UPDATE messages SET time=?1 WHERE stamp=?2 and contact=?3");
			statement.bind(1, message.time());
			statement.bind(2, message.stamp());
			statement.bind(3, message.contact());
			statement.execute();
		}
		
		return false;
	}
	
	mDatabase->insert("messages", message);
	
	if(!exist)
	{
		if(message.isIncoming() && !message.isPublic()) mHasNew = true;
		notifyAll();
		SyncYield(this);
		
		String attachment = message.header("attachment");
		if(!attachment.empty())
		try {
			ByteString target;
			target.fromString(attachment);
			Splicer::Prefetch(target);
		}
		catch(const Exception &e)
		{
			LogWarn("MessageQueue::add", String("Attachment prefetching failed: ") + e.what());
		}
	}
	
	return true;
}

bool MessageQueue::get(const String &stamp, Message &result) const
{
	Synchronize(this);
 
	Database::Statement statement = mDatabase->prepare("SELECT * FROM messages WHERE stamp=?1 LIMIT 1");
        statement.bind(1, stamp);
        if(statement.step())
	{
		result.deserialize(statement);
        	statement.finalize(); 
		return true;
	}

	statement.finalize();
        return false;
}

void MessageQueue::markReceived(const String &stamp, const String &uname)
{
	Synchronize(this);
	
	Database::Statement statement = mDatabase->prepare("INSERT OR IGNORE INTO received (stamp, contact) VALUES (?1,?2)");
	statement.bind(1, stamp);
	statement.bind(2, uname);
	statement.execute();
}

void MessageQueue::markRead(const String &stamp)
{
	Synchronize(this);
  
	Database::Statement statement = mDatabase->prepare("UPDATE messages SET isread=1 WHERE stamp=?1");
	statement.bind(1, stamp);
	statement.execute();
}

void MessageQueue::ack(const Array<Message> &messages)
{
	Map<String, StringArray> stamps;
	for(int i=0; i<messages.size(); ++i)
		if(!messages[i].isRead() && messages[i].isIncoming())
		{
			stamps[messages[i].contact()].append(messages[i].stamp());

			Database::Statement statement = mDatabase->prepare("UPDATE messages SET isread=1 WHERE stamp=?1");
        		statement.bind(1, messages[i].stamp());
        		statement.execute();
		}

	for(Map<String, StringArray>::iterator it = stamps.begin();
		it != stamps.end();
		++it)
	{
		// TODO: ACKs are sent but ignored at reception for public messages

		String tmp;
		YamlSerializer serializer(&tmp);
		serializer.output(it->second);

		Notification notification(tmp);
		notification.setParameter("type", "ack");
		
		AddressBook::Contact *contact = mUser->addressBook()->getContactByUniqueName(it->first);
		if(contact)
			contact->send(notification);
		
		AddressBook::Contact *self = mUser->addressBook()->getSelf();
		if(self && self != contact) 
			self->send(notification);
	}
}

void MessageQueue::erase(const String &uname)
{
	Synchronize(this);

	// Additionnal fields in messages should be added here
	Database::Statement statement = mDatabase->prepare("INSERT OR REPLACE INTO messages \
(id, stamp, parent, headers, content, author, signature, contact, time, public, incoming, relayed, isread) \
SELECT m.id, m.stamp, m.parent, m.headers, m.content, m.author, '', p.contact, m.time, m.public, m.incoming, 1, m.isread \
FROM messages AS m LEFT JOIN messages AS p ON p.stamp=NULLIF(m.parent,'') WHERE m.contact=?1 AND p.contact IS NOT NULL");
	statement.bind(1, uname);
	statement.execute();

        statement = mDatabase->prepare("DELETE FROM messages WHERE contact=?1");
        statement.bind(1, uname);
        statement.execute();
	
	statement = mDatabase->prepare("DELETE FROM received WHERE contact=?1");
        statement.bind(1, uname);
        statement.execute();
}

void MessageQueue::http(const String &prefix, Http::Request &request)
{
	String url = request.url;
	url.ignore();	// removes first '/'
	String uname = url;
	url = uname.cut('/');
	if(!url.empty()) throw 404;
	url = "/";
	if(!uname.empty()) url+= uname + "/";
	
	AddressBook::Contact *contact = NULL;
	if(!uname.empty()) 
	{
		contact = mUser->addressBook()->getContactByUniqueName(uname);
		if(!contact) throw 404;
	}
	
	AddressBook::Contact *self = mUser->addressBook()->getSelf();
	
	if(request.method == "POST")
        {
		if(!user()->checkToken(request.post["token"], "message")) 
			throw 403;
		
                if(!request.post.contains("message") || request.post["message"].empty())
			throw 400;

		bool isPublic = request.post["public"].toBool();
		
		try {
			Message message(request.post["message"]);
			message.setIncoming(false);
			message.setPublic(isPublic);
			if(!isPublic) message.setContact(uname);
			
			if(request.post.contains("parent"))
				message.setParent(request.post["parent"]);
		
			if(request.post.contains("attachment"))
				message.setHeader("attachment", request.post.get("attachment"));
	
			add(message);	// signs the message
			
			if(contact)
			{
				contact->send(message);
				if(self && self != contact)
					self->send(message);
			}
			else {
				user()->addressBook()->send(message);
			}
		}
		catch(const Exception &e)
		{
			LogWarn("AddressBook::Contact::http", String("Cannot post message: ") + e.what());
			throw 409;
		}

		Http::Response response(request, 200);
		response.send();
		return;
        }
        
	if(request.get.contains("json"))
	{
		int64_t next = 0;
		if(request.get.contains("next")) request.get.get("next").extract(next);

		Http::Response response(request, 200);
		response.headers["Content-Type"] = "application/json";
		response.send();

		Selection selection;
		if(request.get["public"].toBool()) selection = selectPublic(uname);
		else  selection = selectPrivate(uname);
		if(request.get["incoming"].toBool()) selection.includeOutgoing(false);
		if(request.get.contains("parent")) selection.setParentStamp(request.get["parent"]);
		
		int count = 100; // TODO: 100 messages selected is max
		if(request.get.contains("count")) request.get.get("count").extract(count);
		
		SerializableArray<Message> array;
		while(!selection.getLast(next, count, array))
			if(!wait(60.)) return;

		ack(array);
		
		JsonSerializer serializer(response.sock);
		serializer.output(array);
		return;
	}
	
	if(!contact) throw 400;
	String name = contact->name();
	String status = contact->status();
	
	bool isPopup = request.get.contains("popup");

	Http::Response response(request, 200);
	response.send();

	Html page(response.sock);
	
	String title= "Chat with "+name;
	page.header(title, isPopup);

	page.open("div","topmenu");	
	if(isPopup) page.span(title, ".button");
	page.span(status.capitalized(), "status.button");
	page.raw("<a class=\"button\" href=\"#\" onclick=\"createFileSelector('/"+mUser->name()+"/myself/files/?json', '#fileSelector', 'input.attachment', 'input.attachmentname','"+mUser->generateToken("directory")+"'); return false;\">Send file</a>");

// TODO: should be hidden in CSS
#ifndef ANDROID
	if(!isPopup)
	{
		String popupUrl = prefix + url + "?popup=1";
		page.raw("<a class=\"button\" href=\""+popupUrl+"\" target=\"_blank\" onclick=\"return popup('"+popupUrl+"','/');\">Popup</a>");
	}
#endif
	page.close("div");

	page.div("", "fileSelector");	

	if(isPopup) page.open("div", "chat");
	else page.open("div", "chat.box");

	page.open("div", "chatmessages");
	page.close("div");

	page.open("div", "chatpanel");
	page.div("","attachedfile");
	page.openForm("#", "post", "chatform");
	page.textarea("chatinput");
	page.input("hidden", "attachment");
	page.input("hidden", "attachmentname");
	page.closeForm();
	page.close("div");

	page.close("div");
 
	page.javascript("function post() {\n\
			var message = $(document.chatform.chatinput).val();\n\
			var attachment = $(document.chatform.attachment).val();\n\
			if(!message) return false;\n\
			var fields = {};\n\
			fields['message'] = message;\n\
			fields['token'] = '"+user()->generateToken("message")+"';\n\
			if(attachment) fields['attachment'] = attachment;\n\
			$.post('"+prefix+url+"', fields)\n\
			.fail(function(jqXHR, textStatus) {\n\
				alert('The message could not be sent.');\n\
			});\n\
			$(document.chatform.chatinput).val('');\n\
			$(document.chatform.attachment).val('');\n\
			$(document.chatform.attachmentname).val('');\n\
			$('#attachedfile').hide();\n\
		}\n\
		$(document.chatform).submit(function() {\n\
			post();\n\
			return false;\n\
		});\n\
		$(document.chatform.attachment).change(function() {\n\
			$('#attachedfile').html('');\n\
			$('#attachedfile').hide();\n\
			var filename = $(document.chatform.attachmentname).val();\n\
			if(filename != '') {\n\
				$('#attachedfile').append('<img class=\"icon\" src=\"/file.png\">');\n\
				$('#attachedfile').append('<span class=\"filename\">'+filename+'</span>');\n\
				$('#attachedfile').show();\n\
			}\n\
			$(document.chatform.chatinput).focus();\n\
			if($(document.chatform.chatinput).val() == '') {\n\
				$(document.chatform.chatinput).val(filename);\n\
				$(document.chatform.chatinput).select();\n\
			}\n\
		});\n\
		$(document.chatform.chatinput).keypress(function(e) {\n\
			if (e.keyCode == 13 && !e.shiftKey) {\n\
				e.preventDefault();\n\
				post();\n\
			}\n\
		});\n\
		$('#attachedfile').hide();\n\
		setMessagesReceiver('"+Http::AppendGet(request.fullUrl, "json")+"','#chatmessages');");

	unsigned refreshPeriod = 5000;
	page.javascript("setCallback(\""+contact->urlPrefix()+"/?json\", "+String::number(refreshPeriod)+", function(info) {\n\
		transition($('#status'), info.status.capitalize());\n\
		$('#status').removeClass().addClass('button').addClass(info.status);\n\
		if(info.newmessages) playMessageSound();\n\
	});");

	page.footer();
	return;
}

MessageQueue::Selection MessageQueue::select(const String &uname) const
{
	return Selection(this, uname, true, true, true);
}

MessageQueue::Selection MessageQueue::selectPrivate(const String &uname) const
{
	return Selection(this, uname, true, false, true);
}

MessageQueue::Selection MessageQueue::selectPublic(const String &uname, bool includeOutgoing) const
{
	return Selection(this, uname, false, true, includeOutgoing);
}

MessageQueue::Selection MessageQueue::selectChilds(const String &parentStamp) const
{
	return Selection(this, parentStamp);
}

MessageQueue::Selection::Selection(void) :
	mMessageQueue(NULL),
	mBaseTime(time_t(0)),
	mIncludePrivate(true),
	mIncludePublic(true),
	mIncludeOutgoing(true)
{
	
}

MessageQueue::Selection::Selection(const MessageQueue *messageQueue, const String &uname, 
				   bool includePrivate, bool includePublic, bool includeOutgoing) :
	mMessageQueue(messageQueue),
	mContact(uname),
	mBaseTime(time_t(0)),
	mIncludePrivate(includePrivate),
	mIncludePublic(includePublic),
	mIncludeOutgoing(includeOutgoing)
{
	
}

MessageQueue::Selection::Selection(const MessageQueue *messageQueue, const String &parent) :
	mMessageQueue(messageQueue),
	mBaseTime(time_t(0)),
	mIncludePrivate(true),
	mIncludePublic(true),
	mIncludeOutgoing(true),
	mParentStamp(parent)
{
	
}

MessageQueue::Selection::~Selection(void)
{
	
}

void MessageQueue::Selection::setParentStamp(const String &stamp)
{
	mParentStamp = stamp;
}

bool MessageQueue::Selection::setBaseStamp(const String &stamp)
{
	if(!stamp.empty())
	{
		Message base;
		if(mMessageQueue->get(stamp, base))
		{
			mBaseStamp = base.stamp();
			mBaseTime = base.time();
			return true;
		}
	}
	
	mBaseStamp.clear();
	return false;
}

String MessageQueue::Selection::baseStamp(void) const
{
	return mBaseStamp;
}

void MessageQueue::Selection::includePrivate(bool enabled)
{
	mIncludePrivate = enabled;
}

void MessageQueue::Selection::includePublic(bool enabled)
{
	mIncludePublic = enabled;
}

void MessageQueue::Selection::includeOutgoing(bool enabled)
{
	mIncludeOutgoing = enabled;
}

int MessageQueue::Selection::count(void) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	
	int count = 0;
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("COUNT(*) AS count")+" WHERE "+filter());
	filterBind(statement);
	if(!statement.step()) return 0;
	statement.input(count);
	statement.finalize();
	return count;
}

int MessageQueue::Selection::unreadCount(void) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	
	int count = 0;
        Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("COUNT(*) AS count")+" WHERE "+filter()+" AND message.isread=0 AND message.incoming=1");
	filterBind(statement);
	if(!statement.step()) return 0;
        statement.input(count);
        statement.finalize();
        return count;
}

bool MessageQueue::Selection::getOffset(int offset, Message &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("*")+" WHERE "+filter()+" ORDER BY message.time,message.stamp LIMIT @offset,1");
	filterBind(statement);
	statement.bind(statement.parameterIndex("offset"), offset);
	if(!statement.step())
	{
		statement.finalize();
		return false;
	}
	
        statement.input(result);
	statement.finalize();
        return true;
}

bool MessageQueue::Selection::getRange(int offset, int count, Array<Message> &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("*")+" WHERE "+filter()+" ORDER BY message.time,message.stamp LIMIT @offset,@count");
	filterBind(statement);
	statement.bind(statement.parameterIndex("offset"), offset);
	statement.bind(statement.parameterIndex("count"), count);
        statement.fetch(result);
	statement.finalize();
	
        return (!result.empty());
}

bool MessageQueue::Selection::getLast(int count, Array<Message> &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();

	// Find the id of the last message without counting only messages without a parent
	int64_t lastId = 0;
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("id")+" WHERE "+filter()+" AND NULLIF(message.parent,'') IS NULL ORDER BY message.id DESC LIMIT @count");
	filterBind(statement);
	statement.bind(statement.parameterIndex("count"), count);
	while(statement.step())
		statement.input(lastId);
	statement.finalize();
	
	statement = mMessageQueue->mDatabase->prepare("SELECT "+target("message.*, message.id AS number")+" WHERE "+filter()+" AND message.id>=@lastid OR parent.id>=@lastid ORDER BY message.time,message.id");
	filterBind(statement);
	statement.bind(statement.parameterIndex("lastid"), lastId);
        statement.fetch(result);
	statement.finalize();
	
	if(!result.empty())
	{
		if(mIncludePrivate) mMessageQueue->mHasNew = false;
		return true;
	}
        return false;
}

bool MessageQueue::Selection::getLast(const Time &time, int max, Array<Message> &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("message.*, message.id AS number")+" WHERE "+filter()+" AND message.time>=@time ORDER BY message.id DESC LIMIT @max");
	filterBind(statement);
	statement.bind(statement.parameterIndex("time"), time);
	statement.bind(statement.parameterIndex("max"), max);
        statement.fetch(result);
	statement.finalize();
	
	if(!result.empty())
	{
		result.reverse();
		if(mIncludePrivate) mMessageQueue->mHasNew = false;
		return true;
	}
        return false;
}

bool MessageQueue::Selection::getLast(int64_t nextNumber, int count, Array<Message> &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();
	
	if(nextNumber <= 0) 
		return getLast(count, result);
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("message.*, message.id AS number")+" WHERE "+filter()+" AND message.id>=@id ORDER BY message.id ASC LIMIT @count");
	filterBind(statement);
	statement.bind(statement.parameterIndex("id"), nextNumber);
	statement.bind(statement.parameterIndex("count"), count);
        statement.fetch(result);
	statement.finalize();
	
	if(!result.empty())
	{
		// No reverse here
		if(mIncludePrivate) mMessageQueue->mHasNew = false;
		return true;
	}
        return false;
}

bool MessageQueue::Selection::getUnread(Array<Message> &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("*")+" WHERE "+filter()+" AND message.isread=0");
        filterBind(statement);
	statement.fetch(result);
	statement.finalize();
        return (!result.empty());
}

bool MessageQueue::Selection::getUnreadStamps(StringArray &result) const
{
	Assert(mMessageQueue);
	Synchronize(mMessageQueue);
	result.clear();
	
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("stamp")+" WHERE "+filter()+" AND message.isread=0");
        filterBind(statement);
	statement.fetchColumn(0, result);
	statement.finalize();
        return (!result.empty());
}

void MessageQueue::Selection::markRead(const String &stamp)
{
        Assert(mMessageQueue);
        Synchronize(mMessageQueue);

	// Check the message is in selection
	Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("id")+" WHERE "+filter()+" AND message.stamp=@stamp");
        filterBind(statement);
	statement.bind(statement.parameterIndex("stamp"), stamp);
	bool found = statement.step();
	statement.finalize();
	
	if(found)
	{
		statement = mMessageQueue->mDatabase->prepare("UPDATE messages SET isread=1 WHERE stamp=@stamp");
		statement.bind(statement.parameterIndex("stamp"), stamp);
		statement.execute();
	}
}

int MessageQueue::Selection::checksum(int offset, int count, ByteStream &result) const
{
	StringList stamps;

	{
		Assert(mMessageQueue);
		Synchronize(mMessageQueue);
		result.clear();
	
		Database::Statement statement = mMessageQueue->mDatabase->prepare("SELECT "+target("stamp")+" WHERE "+filter()+" ORDER BY message.time,message.stamp LIMIT @offset,@count");
        	filterBind(statement);
        	statement.bind(statement.parameterIndex("offset"), offset);
		statement.bind(statement.parameterIndex("count"), count);

		while(statement.step())
        	{
			 String stamp;
                	 statement.value(0, stamp);
                	 stamps.push_back(stamp);
		}

		statement.finalize();
	}

	Sha512 sha512;
	sha512.init();
	for(StringList::iterator it = stamps.begin(); it != stamps.end(); ++it)
	{
		if(it != stamps.begin()) sha512.process(",", 1);
		sha512.process(it->data(), it->size());
	}

	sha512.finalize(result);
	return stamps.size();
}

String MessageQueue::Selection::target(const String &columns) const
{
	StringList columnsList;
	columns.trimmed().explode(columnsList, ',');
	for(StringList::iterator it = columnsList.begin(); it != columnsList.end(); ++it)
		if(!it->contains('.') && !it->contains(' ') && !it->contains('(') && !it->contains(')')) 
			*it = "message." + *it;

	String target;
	target.implode(columnsList, ',');	

	target+= " FROM " + table();

        return target;
}

String MessageQueue::Selection::table(void) const
{
	return "messages AS message LEFT JOIN messages AS parent ON parent.stamp=NULLIF(message.parent,'')";
}

String MessageQueue::Selection::filter(void) const
{
        String condition;
        if(mContact.empty()) condition = "1=1";
	else {
		condition = "(EXISTS(SELECT 1 FROM received WHERE received.contact=@contact AND received.stamp=message.stamp)\
			OR (message.contact='' OR message.contact=@contact)\
			OR (NOT message.relayed AND NULLIF(message.parent,'') IS NOT NULL AND (parent.contact='' OR parent.contact=@contact)))";
	}

        if(!mBaseStamp.empty()) condition+= " AND (message.time>@basetime OR (message.time=@basetime AND message.stamp>=@basestamp))";

        if(!mParentStamp.empty()) condition+= " AND message.parent=@parentstamp";

        if( mIncludePrivate && !mIncludePublic) condition+= " AND message.public=0";
        if(!mIncludePrivate &&  mIncludePublic) condition+= " AND message.public=1";
        if(!mIncludeOutgoing) condition+= " AND message.incoming=1";

        return condition;
}

void MessageQueue::Selection::filterBind(Database::Statement &statement) const
{
	statement.bind(statement.parameterIndex("contact"), mContact);
	statement.bind(statement.parameterIndex("parentstamp"), mParentStamp);
	statement.bind(statement.parameterIndex("basestamp"), mBaseStamp);
	statement.bind(statement.parameterIndex("basetime"), mBaseTime);
}

}

