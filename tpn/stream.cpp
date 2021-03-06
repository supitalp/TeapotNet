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

#include "tpn/stream.h"
#include "tpn/serializable.h"
#include "tpn/exception.h"
#include "tpn/bytestream.h"

namespace tpn
{

const String Stream::IgnoredCharacters = "\r\0";
const String Stream::BlankCharacters = " \t\r\n";
const String Stream::NewLine = "\r\n";
const char Stream::Space = ' ';

Stream::Stream(void) :
	mLast(0),
	mHexa(false),
	mEnd(false)
{

}

Stream::~Stream(void)
{

}

size_t Stream::readData(ByteStream &s, size_t max)
{
	char buffer[BufferSize];
	size_t left = max;
	size_t size;
	while(left && (size = readData(buffer,std::min(BufferSize,left))))
	{
		left-= size;
		s.writeData(buffer,size);
	}
	return max-left;
}

size_t Stream::writeData(ByteStream &s, size_t max)
{
	char buffer[BufferSize];
	size_t left = max;
	size_t size;
	while(left && (size = s.readData(buffer,std::min(BufferSize,left))))
	{
		left-= size;
		writeData(buffer,size);
	}
	return max-left;
}

bool Stream::hexaMode(void)
{
	return mHexa;
}

bool Stream::hexaMode(bool enabled)
{
	bool old = mHexa;
	mHexa = enabled;
	return old;
}

bool Stream::get(char &chr)
{
	while(readData(&mLast,1))
	{
		mEnd = false;
		if(!IgnoredCharacters.contains(mLast))
		{
			chr = mLast;
			return true;
		}
	}
	
	mEnd = true;
	return false;
}

bool Stream::ignore(size_t size)
{
	char buffer[BufferSize];
	size_t len;
	while(size && (len = readData(buffer, std::min(size, BufferSize))))
	{
		if(!len) return false;
		size-= len;
		mLast = buffer[len-1];
	}

	return true;
}

void Stream::put(char chr)
{
	writeData(&chr,1);
}

void Stream::space(void)
{
	put(Space);
}

void Stream::newline(void)
{
	write(NewLine);
}

char Stream::last(void) const
{
	return mLast;
}

bool Stream::atEnd(void) const
{
	return mEnd;
}

bool Stream::ignoreUntil(char delimiter)
{
	if(!get(mLast)) return false;
	while(mLast != delimiter)
		if(!get(mLast)) return false;
	return true;
}

bool Stream::ignoreUntil(const String &delimiters)
{
	if(!get(mLast)) return false;
	while(!delimiters.contains(mLast))
		if(!get(mLast)) return false;
	return true;
}

bool Stream::ignoreWhile(const String &chars)
{
	if(!get(mLast)) return false;
	while(chars.contains(mLast))
		if(!get(mLast)) return false;
	return true;
}

bool Stream::readUntil(Stream &output, char delimiter)
{
	const int maxCount = 10240;	// 10 Ko for security reasons
	
	int left = maxCount;
	char chr;
	if(!get(chr)) return false;
	while(chr != delimiter)
	{
		output.write(chr);
		--left;
		if(!left || !get(chr)) break;
	}
	return true;
}

bool Stream::readUntil(Stream &output, const String &delimiters)
{
	const int maxCount = 10240;	// 10 Ko for security reasons
	
	int left = maxCount;
	char chr;
	if(!get(chr)) return false;
	while(!delimiters.contains(chr))
	{
		output.write(chr);
		--left;
		if(!left || !get(chr)) break;
	}
	return true;
}

int64_t Stream::read(Stream &s)
{
	char buffer[BufferSize];
	int64_t total = 0;
	size_t size;
	while((size = readData(buffer,BufferSize)))
	{
		total+= size;
		mLast = buffer[size-1];
		s.writeData(buffer,size);
	}
	mEnd = true;
	return total;
}

int64_t Stream::read(Stream &s, int64_t max)
{
	char buffer[BufferSize];
	int64_t left = max;
	size_t size;
	while(left && (size = readData(buffer,size_t(std::min(int64_t(BufferSize),left)))))
	{
		left-=size;
		mLast = buffer[size-1];
		s.writeData(buffer,size);
	}
	mEnd = (left != 0);
	return max-left;
}

bool Stream::read(Serializable &s)
{
	return s.deserialize(*this);
}

bool Stream::read(String &s)
{
	s.clear();
  
	char chr;
	if(!get(chr)) return false;

	while(BlankCharacters.contains(chr))
		if(!get(chr)) return false;

	while(!BlankCharacters.contains(chr))
	{
		s+= chr;
		if(!get(chr)) break;
	}

	return true;
}

bool Stream::read(bool &b)
{
	String s;
	if(!read(s)) return false;
	s.trim();
	String str = s.toLower();
	if(str == "true" || str == "yes" || str == "on" || str == "1") b = true;
	else if(str.empty() || str == "false" || str == "no" || str == "off"  || str == "0") b = false;
	else throw Exception("Invalid boolean value: \""+s+"\"");
	return true;
}

double Stream::readDouble(void)
{
	double value;
	if(!read(value)) throw Exception("No value to read");
	return value;
}

float Stream::readFloat(void)
{
	float value;
	if(!read(value)) throw Exception("No value to read");
	return value;
}

int Stream::readInt(void)
{
	int value;
	if(!read(value)) throw Exception("No value to read");
	return value;
}

bool Stream::readBool(void)
{
	bool value;
	if(!read(value)) throw Exception("No value to read");
	return value;
}

void Stream::write(Stream &s)
{
	s.read(*this);
}

void Stream::write(const Serializable &s)
{
	s.serialize(*this);
}

void Stream::write(const String &s)
{
  	writeData(s.data(), s.size());
}

void Stream::write(const char *s)
{
	if(s) write(String(s));
}

void Stream::write(const std::string &s)
{
	write(String(s));
}

void Stream::write(bool b)
{
	if(b) write("true");
	else write("false");
}

Stream &Stream::operator<<(Stream &s)
{
	write(s);
	return (*this);
}

bool Stream::assertChar(char chr)
{
	char tmp;
	if(!readChar(tmp)) return false;
	AssertIO(tmp == chr);
	return true;
}

bool Stream::readChar(char &chr)
{
	if(!ignoreWhile(BlankCharacters)) return false;
	chr = last();
	return true;
}

bool Stream::readLine(String &str)
{
	str.clear();
	return readUntil(str, '\n');
}

bool Stream::readString(String &str)
{
	return read(str);
}

bool Stream::readStdString(std::string &output)
{
	String str;
	if(!read(str)) return false;
	output+= str;
	return true;
}

void Stream::error(void)
{
	throw IOException();
}

}
