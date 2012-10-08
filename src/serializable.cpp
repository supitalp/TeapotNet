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

#include "serializable.h"
#include "string.h"
#include "exception.h"
#include "yamlserializer.h"

namespace tpot
{

Serializable::Serializable(void)
{

}

Serializable::~Serializable(void)
{

}

void Serializable::serialize(Serializer &s) const
{
	// DUMMY
	throw Unsupported("Object cannot be deserialized");
}

bool Serializable::deserialize(Serializer &s)
{
	// DUMMY
	throw Unsupported("Object cannot be deserialized");
}

void Serializable::serialize(Stream &s) const
{
	YamlSerializer serializer(&s);	// TODO
	serialize(serializer);
}

bool Serializable::deserialize(Stream &s)
{
	YamlSerializer serializer(&s);	// TODO
	return deserialize(serializer);
}

bool Serializable::isInlineSerializable(void) const
{
	return true;	// Most objects allow inline serialization
}

String Serializable::toString(void) const
{
	String str;
	serialize(str);
	return str;
}

void Serializable::fromString(String str)
{
	AssertIO(deserialize(str));  
}

Serializable::operator String(void) const
{
	return toString();
}

std::ostream &operator<<(std::ostream &os, const Serializable &s)
{
	os<<std::string(s.toString());
	return os;
}

}
