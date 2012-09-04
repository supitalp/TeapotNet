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

#ifndef ARC_ADDRESSBOOK_H
#define ARC_ADDRESSBOOK_H

#include "include.h"
#include "thread.h"
#include "http.h"
#include "address.h"
#include "socket.h"
#include "identifier.h"
#include "array.h"
#include "map.h"

namespace arc
{

class AddressBook : public Thread, protected Synchronizable
{
public:
	AddressBook(void);
	~AddressBook(void);
	
	const Identifier &addContact(String &name, String &secret);
	void removeContact(Identifier &peering);
	void computePeering(const String &name, const ByteString &secret, ByteStream &out);
	void computeRemotePeering(const String &name, const ByteString &secret, ByteStream &out);
	
	void http(Http::Request &request);
	
	struct Contact
	{
		String name;
		ByteString secret;
		Identifier peering;
		Identifier remotePeering;
		Array<Address> addrs;
	};
	
private:
	void run(void);
	bool publish(const Identifier &remotePeering);
	bool query(const Identifier &peering, Array<Address> &addrs);
	
	String mLocalName;
	Map<Identifier, Contact> mContacts;	// Sorted by peering
};

}

#endif