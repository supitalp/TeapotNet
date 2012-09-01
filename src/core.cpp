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

#include "core.h"
#include "html.h"
#include "sha512.h"

namespace arc
{

Core *Core::Instance = new Core(8000);	// TODO

Core::Core(int port) :
		mSock(port),
		mLastRequest(0)
{

}

Core::~Core(void)
{
	mSock.close();
}

void Core::add(Socket *sock)
{
	assert(sock != NULL);
	Handler *handler = new Handler(this,sock);
	handler->start(true); // handler will destroy itself
}

void Core::run(void)
{
	Interface::Instance->add("peers", this);

	try {
		while(true)
		{
			Socket *sock = new Socket;
			mSock.accept(*sock);
			add(sock);
		}
	}
	catch(const NetException &e)
	{
		return;
	}
}

unsigned Core::addRequest(Request *request)
{
	++mLastRequest;
	request->mId = mLastRequest;
	mRequests.insert(mLastRequest, request);

	if(request->mReceiver == Identifier::Null)
	{
		for(Map<Identifier,Handler*>::iterator it = mHandlers.begin();
				it != mHandlers.end();
				++it)
		{
			Handler *handler = it->second;
			handler->lock();
			handler->addRequest(request);
			handler->unlock();
		}
	}
	else {
		// TODO: This should be modified to allow routing
		Handler *handler;
		if(!mHandlers.get(request->mReceiver, handler)) return 0;	// TODO
		handler->lock();
		handler->addRequest(request);
		handler->unlock();
	}

	return mLastRequest;
}

void Core::removeRequest(unsigned id)
{
	for(Map<Identifier,Handler*>::iterator it = mHandlers.begin();
				it != mHandlers.end();
				++it)
	{
		Handler *handler = it->second;
		handler->lock();
		handler->removeRequest(id);
		handler->unlock();
	}
}

void Core::http(Http::Request &request)
{
	List<String> list;
	request.url.explode(list,'/');

	// URL should begin with /
	if(list.empty()) throw 404;
	if(!list.front().empty()) throw 404;
	list.pop_front();
	if(list.empty()) throw 404;

	// Remove last /
	if(list.back().empty()) list.pop_back();
	if(list.empty()) throw 404;

	if(list.front() == "peers")
	{
		list.pop_front();

		if(list.empty())
		{
			Http::Response response(request, 200);
			response.send();

			Html page(response.sock);
			page.header("Peers");
			page.open("h1");
			page.text("Peers");
			page.close("h1");

			if(mHandlers.empty()) page.text("No peer...");
			else for(Map<Identifier,Handler*>::iterator it = mHandlers.begin();
							it != mHandlers.end();
							++it)
			{
				String str(it->first.toString());
				page.link(str,String("/peers/")+str);
				page.br();
			}

			page.footer();
		}
		else {
			//Identifier ident;
			//list.front() >> ident;

			// TODO
		}
	}
}

void Core::add(const Identifier &peer, Core::Handler *handler)
{
	Assert(handler != NULL);
	mHandlers.insert(peer, handler);
}

void Core::remove(const Identifier &peer)
{
	if(mHandlers.contains(peer))
	{
		delete mHandlers.get(peer);
		mHandlers.erase(peer);
	}
}

Core::Handler::Handler(Core *core, Socket *sock) :
	mCore(core),
	mSock(sock),
	mSender(sock)
{

}

Core::Handler::~Handler(void)
{
	delete mSock;
	delete mHandler;
}

void Core::Handler::addRequest(Request *request)
{
	mSender.lock();
	request->addPending();
	mSender.mRequestsQueue.push(request);
	mSender.unlock();
	mSender.notify();

	mRequests.insert(request->id(),request);
}

void Core::Handler::removeRequest(unsigned id)
{
	mRequests.erase(id);
}

void Core::Handler::run(void)
{
	ByteString nonce_a, salt_a;
	for(int i=0; i<16; ++i)
	{
	  	char c = char(rand()%256);	// TODO
		nonce_a.push_back(c);
		
		c = char(rand()%256);		// TODO
		salt_a.push_back(c);  
	}
  
	*mSock<<"H "<<mCore->mName<<" "<<APPNAME<<" "<<APPVERSION<<" "<<nonce_a<<Stream::NewLine;
  
	String line, command;
	if(!mSock->readLine(line)) return;

	line.read(command);
	command = command.toUpper();
	if(command != "H") throw Exception("Unexpected command");
	
	String name, appname, version;
	line.read(name);
	line.read(appname);
	line.read(version);
	
	ByteString nonce_b;
	line.read(nonce_b);
	
	// TODO: checks
	
	ByteString secret;	// TODO: set this according to name
	
	ByteString agregate_a, hash_a;
	agregate_a.writeBinary(secret);
	agregate_a.writeBinary(salt_a);
	agregate_a.writeBinary(nonce_b);
	agregate_a.writeBinary(mCore->mName);
	agregate_a.writeBinary(name);
	Sha512::Hash(agregate_a, hash_a);	// TODO: multiple times
	  
	*mSock<<"A "<<" "<<salt_a<<" "<<hash_a<<Stream::NewLine;

	line.clear();
	if(!mSock->readLine(line)) throw Exception("Connection unexpectedly closed");
	
	line.read(command);
	command = command.toUpper();
	if(command != "H") throw Exception("Unexpected command");
	
	ByteString salt_b, test_b;
	line.read(salt_b);
	line.read(test_b);
	
	ByteString agregate_b, hash_b;
	agregate_b.writeBinary(secret);
	agregate_b.writeBinary(salt_b);
	agregate_b.writeBinary(nonce_a);
	agregate_b.writeBinary(name);
	agregate_b.writeBinary(mCore->mName);
	Sha512::Hash(agregate_b, hash_b);	// TODO: multiple times
	
	if(test_b != hash_b) throw Exception("Authentication failed");
	
	// TODO: set mPeer here
	
	mCore->add(mPeer,this);

	mSender.start();

	while(mSock->readLine(line))
	{
		lock();

		String command;
		line.read(command);
		command = command.toUpper();

		// Read parameters
		StringMap parameters;
		while(true)
		{
			String name;
			AssertIO(mSock->readLine(name));
			if(name.empty()) break;

			String value = name.cut(':');
			name.trim();
			value.trim();
			parameters.insert(name,value);
		}

		if(command == "R")
		{
			unsigned id;
			String status;
			unsigned channel;

			line.read(id);
			line.read(status);
			line.read(channel);

			Request *request;
			if(mRequests.get(id,request))
			{
				Request::Response *response;

				if(channel)
				{
					ByteStream *sink = request->mContentSink; // TODO
					response = new Request::Response(status, parameters, sink);
					mResponses.insert(channel,response);
				}
				else response = new Request::Response(status, parameters);

				response->mPeer = mPeer;
				request->addResponse(response);

				// TODO: Only one response expected for now
				mRequests.erase(id);

				request->removePending();	// TODO
			}
		}
		else if(command == "D")
		{
			unsigned channel, size;
			line.read(channel);
			line.read(size);

			Request::Response *response;
			if(mResponses.get(channel,response))
			{
				if(size) mSock->readBinary(*response->content(),size);
				else {
					response->content()->close();
					mRequests.erase(channel);
				}
			}
			else mSock->ignore(size);	// TODO: error
		}
		else if(command == "I" || command == "G")
		{
			String &target = line;

			Request *request = new Request;
			request->setTarget(target, (command == "G"));
			request->setParameters(parameters);
			request->execute();

			mSender.lock();
			mSender.mRequestsToRespond.push_back(request);
			request->mResponseSender = &mSender;
			mSender.unlock();
			mSender.notify();
		}

		unlock();
	}

	mSock->close();

	mCore->remove(mPeer);
}

const size_t Core::Handler::Sender::ChunkSize = 4096;	// TODO

Core::Handler::Sender::Sender(Socket *sock) :
		mSock(sock),
		mLastChannel(0)
{

}

Core::Handler::Sender::~Sender(void)
{
	for(int i=0; i<mRequestsToRespond.size(); ++i)
	{
		Request *request = mRequestsToRespond[i];
		if(request->mResponseSender == this)
			request->mResponseSender = NULL;
	}
}

void Core::Handler::Sender::run(void)
{
	char buffer[ChunkSize];

	while(true)
	{
		lock();
		if(mTransferts.empty()
				&& mRequestsQueue.empty()
				&& mRequestsToRespond.empty())
			wait();

		if(!mRequestsQueue.empty())
		{
			Request *request = mRequestsQueue.front();
			if(request->mIsData) *mSock<<"G";
			else *mSock<<"I";
			*mSock<<" "<<request->target()<<Stream::NewLine;

			StringMap &parameters = request->mParameters;
			for(	StringMap::iterator it = parameters.begin();
						it != parameters.end();
						++it)
			{
					*mSock<<it->first<<": "<<it->second<<Stream::NewLine;
			}
			*mSock<<Stream::NewLine;

			mRequestsQueue.pop();
		}

		for(int i=0; i<mRequestsToRespond.size(); ++i)
		{
			Request *request = mRequestsToRespond[i];
			for(int j=0; j<request->responsesCount(); ++j)
			{
				Request::Response *response = request->response(i);
				if(!response->mIsSent)
				{
					String status = "OK";
					unsigned channel = 0;

					if(response->content())
					{
						++mLastChannel;
						channel = mLastChannel;
						mTransferts.insert(channel,response->content());
					}

					*mSock<<"R "<<request->id()<<" "<<status<<" "<<channel<<Stream::NewLine;

					StringMap &parameters = response->mParameters;
					for(	StringMap::iterator it = parameters.begin();
							it != parameters.end();
							++it)
					{
						*mSock<<it->first<<": "<<it->second<<Stream::NewLine;
					}
					*mSock<<Stream::NewLine;
				}
			}
		}

		Map<unsigned, ByteStream*>::iterator it = mTransferts.begin();
		while(it != mTransferts.end())
		{
			size_t size = it->second->readData(buffer,ChunkSize);
			*mSock<<"D "<<it->first<<" "<<size<<Stream::NewLine;
			mSock->writeData(buffer, size);

			if(size == 0)
			{
				delete it->second;
				mTransferts.erase(it++);
			}
			else ++it;
		}
		unlock();
	}
}

}
