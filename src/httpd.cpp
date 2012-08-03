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

#include "httpd.h"

namespace arc
{

Httpd::Httpd(int port) :
		mSock(port)
{

}

Httpd::~Httpd(void)
{
	mSock.close();
	// TODO: delete clients
}

void Httpd::run(void)
{
	try {
		while(true)
		{
			Socket sock = mSock.accept();
			Handler *client = new Handler(this, sock);
			client->start();
		}
	}
	catch(const NetException &e)
	{
		return;
	}
}

Httpd::Handler::Handler(Httpd *httpd, const Socket &sock) :
		mHttpd(httpd),
		mSock(sock)
{

}

Httpd::Handler::~Handler(void)
{
	mSock.close();
}

void Httpd::Handler::run(void)
{
	String action;
	if(!mSock.readLine(action)) return;
	String url = action.cut(' ');
	String protocol = url.cut(' ');
	action.trim();
	url.trim();
	protocol.trim();

	if(action != "GET" && action != "POST")
		error(405);

	if(url.empty() || protocol.empty())
		error(400);

	StringMap headers;
	while(true)
	{
		String line;
		assertIO(mSock.readLine(line));
		if(line.empty()) break;

		String value = line.cut(':');
		line.trim();
		value.trim();
		headers.insert(line,value);
	}

	String file(url);
	String getData = file.cut('?');
	StringMap get;
	if(!getData.empty())
	{
		std::list<String> exploded;
		getData.explode(exploded,'&');
		for(	std::list<String>::iterator it = exploded.begin();
				it != exploded.end();
				++it)
		{
			String value = it->cut('=');
			get.insert(*it, value);
		}
	}

	StringMap post;
	if(action == "POST")
	{
		// TODO: read post data
	}

	if(!headers.contains("Connection"))
	if(protocol == "HTTP/1.1") headers['Connection'] = 'Keep-Alive';
	else headers['Connection'] = 'Close';

	process(file, url, headers, get, post);
}

void Httpd::Handler::error(int code)
{
	StringMap headers;
	headers["Content-Type"] = "text/html; charset=UTF-8";
	headers["Connexion"] = "Close";	// TODO

	respond(code, headers);
}

void Httpd::Handler::respond(int code, StringMap &headers)
{
	String message;
	switch(code)
	{
	case 100: message = "Continue";				break;
	case 200: message = "OK";					break;
	case 204: message = "No content";			break;
	case 206: message = "Partial Content";		break;
	case 301: message = "Moved Permanently"; 	break;
	case 302: message = "Found";			 	break;
	case 303: message = "See Other";			break;
	case 304: message = "Not Modified"; 		break;
	case 305: message = "Use Proxy"; 			break;
	case 307: message = "Temporary Redirect"; 	break;
	case 400: message = "Bad Request"; 			break;
	case 401: message = "Unauthorized";			break;
	case 403: message = "Forbidden"; 			break;
	case 404: message = "Not Found";			break;
	case 405: message = "Method Not Allowed";	break;
	case 406: message = "Not Acceptable";		break;
	case 408: message = "Request Timeout";		break;
	case 410: message = "Gone";					break;
	case 413: message = "Request Entity Too Large"; 		break;
	case 414: message = "Request-URI Too Long";				break;
	case 416: message = "Requested Range Not Satisfiable";	break;
	case 500: message = "Internal Server Error";			break;
	case 501: message = "Not Implemented";					break;
	case 502: message = "Bad Gateway";						break;
	case 503: message = "Service Unavailable";				break;
	case 504: message = "Gateway Timeout";					break;
	case 505: message = "HTTP Version Not Supported";		break;

	default:
		if(code < 300) message = "OK";
		else message = "Error";
		break;
	}

	mSock<<"HTTP/1.1 "<<code<<" "<<message<<"\r\n";
	for(	StringMap::iterator it = headers.begin();
			it != headers.end();
			++it)
	{
		mSock<<it->first<<": "<<it->second<<"\r\n";
	}

	mSock<<"\r\n";
}

void Httpd::Handler::process(	const String &file,
								const String &url,
								StringMap &headers,
								StringMap &get,
								StringMap &post)
{
	error(404);
}

}
