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

#include "bytestring.h"
#include "exception.h"

namespace arc
{

ByteString::ByteString(void)
{

}

ByteString::ByteString(const char *data, size_t size) :
		std::deque<char>(data,data+size)
{

}

ByteString::~ByteString(void)
{

}

void ByteString::clear(void)
{
	clear();
}

void ByteString::append(char value, int n)
{
	for(int i=0; i<n; ++i)
		push_back(value);
}

void ByteString::append(const ByteString &bs)
{
	insert(end(), bs.begin(), bs.end());
}

void ByteString::append(const char *array, size_t size)
{
	insert(end(), array, array+size);
}

void ByteString::fill(char value, int n)
{
	assign(n, value);
}

void ByteString::serialize(Stream &s) const
{
	for(int i=0; i<size(); ++i)
	{
		std::ostringstream oss;
		oss.width(2);
		oss.fill('0');
		oss<<std::hex<<unsigned(uint8_t(at(i)));
		s<<oss.str();
	}
}

void ByteString::deserialize(Stream &s)
{
	clear();

	String str;
	s.read(str);
	int count = str.size()/2;
	if(count*2 != str.size())
		throw InvalidData("Missing character at the end of hexadecimal representation");

	for(int i=0; i<count; ++i)
	{
		std::string byte;
		byte+= str[i*2];
		byte+= str[i*2+1];
		std::istringstream iss(byte);

		unsigned value;
		iss>>std::hex;
		if(!iss>>value)
			throw InvalidData("Invalid hexadecimal representation");

		push_back(uint8_t(value));
	}
}

void ByteString::serializeBinary(ByteStream &s) const
{
	s.writeBinary(*this);
}

void ByteString::deserializeBinary(ByteStream &s)
{
	assertIO(s.readBinary(*this));
}

int ByteString::readData(char *buffer, size_t size)
{
	size = std::min(size, this->size());
	std::copy(begin(), begin()+size, buffer);
	erase(begin(), begin()+size);
	return size;
}

void ByteString::writeData(const char *data, size_t size)
{
	insert(end(), data, data+size);
}

}

