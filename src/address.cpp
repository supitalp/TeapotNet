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

#include "address.h"
#include "exception.h"
#include "string.h"
#include "bytestring.h"

namespace tpot
{

Address::Address(void)
{
	setNull();
}

Address::Address(const String &host, const String &service)
{
	set(host,service);
}

Address::Address(const String &host, int port)
{
	set(host,port);
}


Address::Address(String Address)
{
	deserialize(Address);
}

Address::Address(const sockaddr *addr, socklen_t addrlen)
{
	set(addr,addrlen);
}

Address::~Address(void)
{

}

void Address::set(const String &host, const String &service)
{
	addrinfo aiHints;
	std::memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_UNSPEC;
	aiHints.ai_socktype = SOCK_STREAM;
	aiHints.ai_protocol = IPPROTO_TCP;
	aiHints.ai_flags = 0;

	addrinfo *aiList = NULL;
	if(getaddrinfo(host.c_str(), service.c_str(), &aiHints, &aiList) != 0)
		throw NetException("Address resolution failed for "+host+":"+service);

	mAddrLen = aiList->ai_addrlen;
	std::memcpy(&mAddr,aiList->ai_addr,aiList->ai_addrlen);

	freeaddrinfo(aiList);
}

void Address::set(const String &host, int port)
{
	String service;
	service << port;
	set(host, port);
}

void Address::set(const sockaddr *addr, socklen_t addrlen)
{
	if(!addr || !addrlen)
	{
		mAddrLen = 0;
		return;
	}

	mAddrLen = addrlen;
	std::memcpy(&mAddr,addr,addrlen);
}

void Address::setNull(void)
{
	mAddrLen = 0;
}

bool Address::isNull(void) const
{
	return (mAddrLen == 0);
}

String Address::host(void) const
{
	if(isNull()) throw InvalidData("Requested host for null address");
	char host[HOST_NAME_MAX];
	if(getnameinfo(addr(), addrLen(), host, HOST_NAME_MAX, NULL, 0, NI_NUMERICHOST))
		throw InvalidData("Invalid stored network address");
	return String(host);
}

String Address::service(void) const
{
	if(isNull()) throw InvalidData("Requested service for null address");

	char service[SERVICE_NAME_MAX];
	if(getnameinfo(addr(), addrLen(), NULL, 0, service, SERVICE_NAME_MAX, NI_NUMERICSERV))
		throw InvalidData("Invalid stored network address");
	return String(service);
}

int Address::port(void) const
{
	if(isNull()) throw InvalidData("Requested port for null address");

	String str(service());
	int port;
	str >> port;
	return port;
}

void Address::serialize(Stream &s) const
{
	char host[HOST_NAME_MAX];
	char service[SERVICE_NAME_MAX];
	if(getnameinfo(addr(), addrLen(), host, HOST_NAME_MAX, service, SERVICE_NAME_MAX, NI_NUMERICHOST|NI_NUMERICSERV))
		throw InvalidData("Invalid stored network Address");
	s<<host<<':'<<service;
}

void Address::deserialize(Stream &s)
{
	String str;
	AssertIO(s.readField(str));
	int separator = str.find_last_of(':');
	if(separator == String::NotFound) throw InvalidData("Invalid network Address: " + str);
	String host(str,0,separator);
	String service(str,separator+1);

	addrinfo aiHints;
	std::memset(&aiHints, 0, sizeof(aiHints));
	aiHints.ai_family = AF_UNSPEC;
	aiHints.ai_socktype = SOCK_STREAM;
	aiHints.ai_protocol = IPPROTO_TCP;
	aiHints.ai_flags = 0;

	addrinfo *aiList = NULL;
	if(getaddrinfo(host.c_str(), service.c_str(), &aiHints, &aiList) != 0)
		throw InvalidData("Invalid network Address: " + str);

	mAddrLen = aiList->ai_addrlen;
	std::memcpy(&mAddr,aiList->ai_addr,aiList->ai_addrlen);

	freeaddrinfo(aiList);
}

void Address::serializeBinary(ByteStream &s) const
{
	switch(addrFamily())
	{
	case AF_INET:	// IP v4
	{
		s.writeBinary(uint8_t(4));
		const sockaddr_in *sa = reinterpret_cast<const sockaddr_in*>(&mAddr);
		const char *b = reinterpret_cast<const char *>(&sa->sin_addr.s_addr);

		for(int i=0; i<4; ++i) s.writeBinary(uint8_t(b[i]));
		s.writeBinary(uint16_t(ntohs(sa->sin_port)));
		break;
	}

	case AF_INET6:	// IP v6
	{
		s.writeBinary(uint8_t(16));
		const sockaddr_in6 *sa6 = reinterpret_cast<const sockaddr_in6*>(&mAddr);

		for(int i=0; i<16; ++i) s.writeBinary(uint8_t(sa6->sin6_addr.s6_addr[i]));
		s.writeBinary(uint16_t(ntohs(sa6->sin6_port)));
		break;
	}

	default:
		throw InvalidData("Stored network Address cannot be serialized to binary");
	}
}

void Address::deserializeBinary(ByteStream &s)
{
	uint8_t size = 0;
	AssertIO(s.readBinary(size));

	if(size == 0)
	{
		mAddrLen = 0;
		return;
	}

	switch(size)
	{
	case 4:		// IP v4
	{
		mAddrLen = sizeof(sockaddr_in);
		sockaddr_in *sa = reinterpret_cast<sockaddr_in*>(&mAddr);
		char *b = reinterpret_cast<char *>(&sa->sin_addr.s_addr);

		sa->sin_family = AF_INET;
		uint8_t u;
		for(int i=0; i<4; ++i)
		{
				AssertIO(s.readBinary(u)); b[i] = u;
		}
		uint16_t port;
		AssertIO(s.readBinary(port));
		sa->sin_port = htons(port);
		break;
	}

	case 16:	// IP v6
	{
		mAddrLen = sizeof(sockaddr_in6);
		sockaddr_in6 *sa6 = reinterpret_cast<sockaddr_in6*>(&mAddr);

		sa6->sin6_family = AF_INET6;
		uint8_t u;
		for(int i=0; i<16; ++i)
		{
			AssertIO(s.readBinary(u));
			sa6->sin6_addr.s6_addr[i] = u;
		}
		uint16_t port;
		AssertIO(s.readBinary(port));
		sa6->sin6_port = htons(port);
		break;
	}

	default:
		throw InvalidData("Invalid network Address");
	}
}

const sockaddr *Address::addr(void) const
{
	return reinterpret_cast<const sockaddr*>(&mAddr);
}

int Address::addrFamily(void) const
{
	return reinterpret_cast<const sockaddr_in*>(&mAddr)->sin_family;
}

socklen_t Address::addrLen(void) const
{
	return mAddrLen;
}

bool operator < (const Address &a1, const Address &a2)
{
	if(a1.addrLen() != a2.addrLen()) return a1.addrLen() < a2.addrLen();
	return std::memcmp(a1.addr(),a2.addr(),a1.addrLen()) < 0;
}

bool operator > (const Address &a1, const Address &a2)
{
	if(a1.addrLen() != a2.addrLen()) return a1.addrLen() > a2.addrLen();
	else return std::memcmp(a1.addr(),a2.addr(),a1.addrLen()) > 0;
}

bool operator == (const Address &a1, const Address &a2)
{
	if(a1.addrLen() != a2.addrLen()) return false;
	else return std::memcmp(a1.addr(),a2.addr(),a1.addrLen()) == 0;
}

bool operator != (const Address &a1, const Address &a2)
{
	return !(a1 == a2);
}

}
