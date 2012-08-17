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
#include "httpd.h"
#include "sha512.h"

using namespace arc;

int main(int argc, char** argv)
{
	// TEST
	String test = "The quick brown fox jumps over the lazy dog";
	ByteString result;
	Sha512::Hash(test.data(),test.size(), result);

	std::cout<<"Data: "<<test<<std::endl;
	std::cout<<"Hash: "<<result.toString()<<std::endl;

	// TEST
	Httpd *httpd = new Httpd(8080);
	httpd->start();
	httpd->join();
	
	return 0;
}

