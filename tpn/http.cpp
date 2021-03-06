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

#include "tpn/http.h"
#include "tpn/exception.h"
#include "tpn/html.h"
#include "tpn/config.h"
#include "tpn/mime.h"
#include "tpn/directory.h"
#include "tpn/bytestring.h"

namespace tpn
{

Http::Request::Request(void)
{
	clear();
}

Http::Request::~Request(void)
{
	clear();
}

Http::Request::Request(const String &url, const String &method)
{
	clear();

	int p = url.find("://");
	if(p == String::NotFound) this->url = url;
	else {
		String protocol(url.substr(0,p));
		if(protocol != "http") throw Exception(String("Unknown protocol in URL: ")+protocol);

		String host(url.substr(p+3));
		this->url = String('/') + host.cut('/');
		headers["Host"] = host;
	}

	if(!method.empty()) this->method = method;

	this->headers["User-Agent"] = String(APPNAME) + '/' + APPVERSION;
}

void Http::Request::send(Socket &sock)
{
	this->sock = &sock;

	if(method != "CONNECT" && version == "1.1" && !headers.contains("Connection"))
                headers["Connection"] = "close";

	//if(!headers.contains("Accept-Encoding"))
	//	headers["Accept-Encoding"] = "identity";

	String completeUrl(url);
	if(!get.empty())
	{
		if(completeUrl.find('?') == String::NotFound)
			completeUrl+= '?';

		for(	StringMap::iterator it = get.begin();
				it != get.end();
				++it)
		{
			completeUrl<<'&'<<it->first.urlEncode()<<'='<<it->second.urlEncode();
		}
	}

	String postData;
	for(	StringMap::iterator it = post.begin();
			it != post.end();
			++it)
	{
		if(!postData.empty()) postData<<'&';
		postData<<it->first.urlEncode()<<'='<<it->second.urlEncode();
	}

	if(!postData.empty())
	{
		headers["Content-Length"] = "";
		headers["Content-Length"] << postData.size();
		headers["Content-Type"] = "application/x-www-form-urlencoded";
	}
	
	String buf;
	buf<<method<<" "<<completeUrl<<" HTTP/"<<version<<"\r\n";

	for(	StringMap::iterator it = headers.begin();
			it != headers.end();
			++it)
	{
		List<String> lines;
		it->second.remove('\r');
		it->second.explode(lines,'\n');
		for(	List<String>::iterator l = lines.begin();
				l != lines.end();
				++l)
		{
			buf<<it->first<<": "<<*l<<"\r\n";
		}
	}

	if(!cookies.empty())
	{
		buf<<"Cookie: ";
		for(	StringMap::iterator it = cookies.begin();
				it != cookies.end();
				++it)
		{
			if(it != cookies.begin()) buf<<"; ";
			buf<<it->first<<'='<<it->second;
		}
		buf<<"\r\n";
	}
	
	buf<<"\r\n";
	sock<<buf;

	if(!postData.empty())
		sock<<postData;
}

void Http::Request::recv(Socket &sock, bool parsePost)
{
	this->sock = &sock;

	clear();

	// Read first line
	String line;
	if(!sock.readLine(line)) throw NetException("Connection closed");
	method.clear();
	url.clear();
	line.readString(method);
	line.readString(url);

	String protocol;
	line.readString(protocol);
	version = protocol.cut('/');

	if(url.empty() || version.empty() || protocol != "HTTP")
		throw 400;

	if(method != "GET" && method != "POST" && method != "HEAD")
		throw 405;

	// Read headers
	while(true)
	{
		String line;
		AssertIO(sock.readLine(line));
		if(line.empty()) break;

		String value = line.cut(':');
		line.trim();
		value.trim();
		headers.insert(line,value);
	}

	// Read cookies
	String cookie;
	if(headers.get("Cookie", cookie))
	{
		while(!cookie.empty())
		{
			String next = cookie.cut(';');
			String value = cookie.cut('=');
			cookie.trim();
			value.trim();
			cookies.insert(cookie,value);
			cookie = next;
		}
	}

	fullUrl = url;
	
	// Read URL variables
	String getData = url.cut('?');
	if(!getData.empty())
	{
		List<String> exploded;
		getData.explode(exploded,'&');
		for(	List<String>::iterator it = exploded.begin();
				it != exploded.end();
				++it)
		{
			String value = it->cut('=').urlDecode();
			get.insert(it->urlDecode(), value);
		}
	}

	url = url.urlDecode();
	
	String expect;
	if((headers.get("Expect",expect) && expect.toLower() == "100-continue")
		|| (method == "POST" && version == "1.1"))
	{
		sock.write("HTTP/1.1 100 Continue\r\n\r\n");
	}
	
	// Read post variables
	if(method == "POST" && parsePost)
	{
		if(!headers.contains("Content-Length"))
			throw Exception("Missing Content-Length header in POST request");

		size_t contentLength = 0;
		headers["Content-Length"].extract(contentLength);
		
		String contentType;
		if(headers.get("Content-Type", contentType))
		{
			String parameters = contentType.cut(';');
			contentType.trim();
			
			if(contentType == "application/x-www-form-urlencoded")
			{
				String data;
				if(sock.read(data, contentLength) != contentLength)
					throw NetException("Connection unexpectedly closed");
			  
				List<String> exploded;
				data.explode(exploded,'&');
				for(	List<String>::iterator it = exploded.begin();
						it != exploded.end();
						++it)
				{
					String value = it->cut('=').urlDecode();
					post.insert(it->urlDecode(), value);
				}
			}
			else if(contentType == "multipart/form-data")
			{
				String boundary;
				while(true)
				{
					String key;
			  		if(!parameters.readUntil(key,';')) break;
					String value = key.cut('=');
					key.trim();
					value.trim();
					value.trimQuotes();
					if(key == "boundary") boundary = String("--") + value;
				}
				
				Assert(!boundary.empty());
				
				String line;
				while(line.empty()) AssertIO(sock.readLine(line));
				Assert(line == boundary);
				
				bool finished = false;
				while(!finished)
				{
					StringMap mimeHeaders;
					while(true)
					{
						String line;
						AssertIO(sock.readLine(line));
						if(line.empty()) break;
						
						String value = line.cut(':');
						line.trim();
						value.trim();
						mimeHeaders.insert(line,value);
					}
					
					String contentType;
					if(mimeHeaders.get("Content-Type", contentType))
					{
						String parameters = contentType.cut(';');
						contentType.trim();
					}
					
					String contentDisposition;
					if(!mimeHeaders.get("Content-Disposition", contentDisposition))
						throw Exception("Missing Content-Disposition header in multipart POST request");
					
					String parameters = contentDisposition.cut(';');
					contentDisposition.trim();
					
					String name, fileName;
					while(true)
					{
						String key;
			  			if(!parameters.readUntil(key,';')) break;
						String value = key.cut('=');
						key.trim();
						value.trim();
						value.trimQuotes();
						if(key == "name") name = value;
						else if(key == "filename") fileName = value;
					}
					
					Stream *stream = NULL;
					if(fileName.empty()) stream = &post[name];
					else {
						post[name] = fileName;
						TempFile *tempFile = new TempFile();
						files[name] = tempFile;
						stream = tempFile;
						LogDebug("Http::Request", String("File upload: ") + fileName);
					}
	
					String contentLength;
					if(mimeHeaders.get("Content-Length", contentLength))
					{
						size_t size = 0;
						contentLength >> size;
						sock.read(*stream,size);
						
						String line;
						AssertIO(sock.readLine(line));
						AssertIO(sock.readLine(line));
						if(line == boundary + "--") finished = true;
						else AssertIO(line == boundary);
					}
					else {
					  	/*String line;
						while(sock.readLine(line))
						{
							if(line == boundary) break;
							if(stream) stream->write(line);
							line.clear();
						}*/
						
						// TODO: This should be replaced with Boyer-Moore algorithm for performance
						String bound = String("\r\n") + boundary;
						char *buffer = new char[bound.size()];
						try {
							size_t size = 0;
							size_t c = 0;
							size_t i = 0;
							while(true)
							{
								if(c == size)
								{
									c = 0;
									size = sock.readData(buffer,bound.size()-i);
									AssertIO(size);
								}
								
								if(buffer[c] == bound[i])
								{
									++i; ++c;
									if(i == bound.size()) 
									{
										// If we are here there is no data left in buffer
										String line;
										AssertIO(sock.readLine(line));
										if(line == "--") finished = true;
										break;
									}
								}
								else {
								  	if(i) stream->writeData(bound.data(), i);
									
									// WARNING: this works only if the pattern is not redundant
									i = c;
									while(c != size && buffer[c] != bound[0]) ++c;
									if(c != i) stream->writeData(buffer + i, c - i);
									i = 0;
								}
							}
						}	  
						catch(...)
						{
							delete[] buffer;
							throw;
						}
						delete[] buffer;
					}
				}
			}
			else {
				LogWarn("Http::Request", String("Unknown encoding: ") + contentType);
				sock.ignore(contentLength);
			}
		}
		else {
			LogWarn("Http::Request", "Missing Content-Type header in POST request");
			sock.ignore(contentLength);
		}
	}
}

void Http::Request::clear(void)
{
	method = "GET";
	version = "1.0";	// 1.1 requires chunked transfer encoding to be implemented
	url.clear();
	headers.clear();
	cookies.clear();
	get.clear();
	post.clear();
	fullUrl.clear();
	
	for(Map<String, TempFile*>::iterator it = files.begin(); it != files.end(); ++it)
	 	delete it->second;
	
	files.clear();
}

bool Http::Request::extractRange(int64_t &rangeBegin, int64_t &rangeEnd, int64_t contentLength) const
{
	if(contentLength < 0) contentLength = std::numeric_limits<int64_t>::max();
	rangeBegin = 0;
	rangeEnd = contentLength-1;
	
	String range;
	if(headers.get("Range",range))
	{
		range.cut(';');
		String tmp = range.cut('=');
		if(range != "bytes") throw 416;
		else {
			List<String> intervals;
			tmp.explode(intervals, ',');
			
			rangeBegin = contentLength-1;
			rangeEnd = 0;
			while(!intervals.empty())
			{
				String sbegin = intervals.front();
				String send = sbegin.cut('-');
				intervals.pop_front();
				
				int64_t begin, end;
				if(!send.empty())   send >> end;
				else end = contentLength-1;
				if(!sbegin.empty()) sbegin >> begin;
				else {
					begin = contentLength-end;
					end = contentLength-1;
				}
			
				if(begin >= contentLength || end >= contentLength || begin > end)
					throw 416;
				
				rangeBegin = std::min(begin, rangeBegin);
				rangeEnd   = std::max(end, rangeEnd);
			}
			
			return true;
		 }
	}
	
	return false;
}

Http::Response::Response(void)
{
	clear();
}

Http::Response::Response(const Request &request, int code)
{
	clear();

	this->code = code;
	this->version = request.version;
	this->sock = request.sock;
	if(code != 204)
		this->headers["Content-Type"] = "text/html; charset=UTF-8";
}

void Http::Response::send(void)
{
	if(!sock) throw Exception("No socket for HTTP response");
	send(*sock);
}

void Http::Response::send(Socket &sock)
{
	this->sock = &sock;

	if(version == "1.1" && code >= 200 && !headers.contains("Connexion"))
		headers["Connection"] = "close";

	if(!headers.contains("Date"))
		headers["Date"] = Time::Now().toHttpDate();

	if(!headers.contains("Last-Modified"))
		headers["Last-Modified"] = headers["Date"];
	
	if(message.empty())
	{
		switch(code)
		{
		case 100: message = "Continue";				break;
		case 200: message = "OK";				break;
		case 204: message = "No content";			break;
		case 206: message = "Partial Content";			break;
		case 301: message = "Moved Permanently"; 		break;
		case 302: message = "Found";				break;
		case 303: message = "See Other";			break;
		case 304: message = "Not Modified"; 			break;
		case 305: message = "Use Proxy"; 			break;
		case 307: message = "Temporary Redirect";		break;
		case 400: message = "Bad Request"; 			break;
		case 401: message = "Unauthorized";			break;
		case 403: message = "Forbidden"; 			break;
		case 404: message = "Not Found";			break;
		case 405: message = "Method Not Allowed";		break;
		case 406: message = "Not Acceptable";			break;
		case 408: message = "Request Timeout";			break;
		case 409: message = "Conflict";				break;
		case 410: message = "Gone";				break;
		case 413: message = "Request Entity Too Large"; 	break;
		case 414: message = "Request-URI Too Long";		break;
		case 416: message = "Requested Range Not Satisfiable";	break;
		case 418: message = "I'm a teapot";			break;
		case 500: message = "Internal Server Error";		break;
		case 501: message = "Not Implemented";			break;
		case 502: message = "Bad Gateway";			break;
		case 503: message = "Service Unavailable";		break;
		case 504: message = "Gateway Timeout";			break;
		case 505: message = "HTTP Version Not Supported";	break;

		default:
			if(code < 300) message = "OK";
			else message = "Error";
			break;
		}
	}

	String buf;
	buf<<"HTTP/"<<version<<" "<<code<<" "<<message<<"\r\n";

	for(	StringMap::iterator it = headers.begin();
			it != headers.end();
			++it)
	{
		List<String> lines;
		it->second.remove('\r');
		it->second.explode(lines,'\n');
		for(	List<String>::iterator l = lines.begin();
				l != lines.end();
				++l)
		{
			buf<<it->first<<": "<<*l<<"\r\n";
		}
	}

	for(	StringMap::iterator it = cookies.begin();
			it != cookies.end();
			++it)
	{
		buf<<"Set-Cookie: "<<it->first<<'='<<it->second<<"\r\n";
	}
	
	buf<<"\r\n";
	sock<<buf;
}

void Http::Response::recv(Socket &sock)
{
	this->sock = &sock;

	clear();

	// Read first line
	String line;
	if(!sock.readLine(line)) throw NetException("Connection closed");

	String protocol;
	line.readString(protocol);
	version = protocol.cut('/');

	line.read(code);
	message = line;

	if(version.empty() || protocol != "HTTP")
		throw Exception("Invalid HTTP response");

	// Read headers
	while(true)
	{
		String line;
		AssertIO(sock.readLine(line));
		if(line.empty()) break;

		String value = line.cut(':');
		line.trim();
		value.trim();
		
		if(line == "Set-Cookie")
		{
			String cookie = value;
			cookie.cut(';');
			value = cookie.cut('=');
			cookies.insert(cookie, value);
		}
		else {
			headers.insert(line,value);
		}
	}
}

void Http::Response::clear(void)
{
	code = 200;
	version = "1.0";
	message.clear();
	version.clear();
	cookies.clear();
}

Http::Server::Server(int port) :
	mPool(4, 16, 128),
	mSock(port)
{

}

Http::Server::~Server(void)
{
	mSock.close();	// useless
}

void Http::Server::run(void)
{
	try {
		while(true)
		{
			Socket *sock = new Socket;
			mSock.accept(*sock);
			Handler *client = new Handler(this, sock);
			mPool.launch(client);
		}
	}
	catch(const NetException &e)
	{
		return;
	}
}

Http::Server::Handler::Handler(Server *server, Socket *sock) :
		mServer(server),
		mSock(sock)
{

}

Http::Server::Handler::~Handler(void)
{
	delete mSock;	// deletion closes the socket
}

void Http::Server::Handler::run(void)
{
	// Warning: no return in this function (autodelete at the end)
	
	Request request;
	try {
		try {
			mSock->setTimeout(milliseconds(Config::Get("http_timeout").toInt()));
			request.recv(*mSock);
			mServer->process(request);
		}
		catch(const Timeout &e)
		{
			throw 408;
		}
		catch(const NetException &e)
		{
			LogDebug("Http::Server::Handler", e.what()); 
		}
		catch(const Exception &e)
		{
			LogWarn("Http::Server::Handler", e.what());
			throw 500;
		}
	}
	catch(int code)
	{
		try {
			Response response(request, code);
			response.headers["Content-Type"] = "text/html; charset=UTF-8";
			response.send();

			if(request.method != "HEAD")
			{
				Html page(response.sock);
				page.header(response.message, true);
				page.open("div", "error");
				page.openLink("/");
				page.image("/error.png", "Error");
				page.closeLink();
				page.br();
				page.open("h1",".huge");
				page.text(String::number(response.code));
				page.br();
				page.text(response.message);
				page.close("h1");
				page.close("div");
				page.footer();
			}
		}
		catch(...)
		{

		}
	}
	
	delete this;	// autodelete
}

int Http::Get(const String &url, Stream *output, int maxRedirections, bool noproxy)
{
	Request request(url,"GET");

	String host;
	if(!request.headers.get("Host", host))
		throw Exception("Invalid URL");

	Address addr;
	bool hasProxy = !noproxy && Config::GetProxyForUrl(url, addr);
	if(hasProxy) request.url = url;		// Full URL for proxy
	else addr.fromString(host);
	
	Socket sock;
	sock.setTimeout(milliseconds(Config::Get("http_timeout").toInt()));
	try {
		sock.connect(addr, true);	// Connect without proxy
		request.send(sock);
	}
	catch(const NetException &e)
	{
		if(hasProxy) LogWarn("Http::Get", String("HTTP proxy error: ") + e.what());
		throw;
	}

	Response response;
        response.recv(sock);

	if(maxRedirections && response.code/100 == 3 && response.headers.contains("Location"))
	{
		sock.discard();
		sock.close();

		// TODO: relative location (even if not RFC-compliant)
		return Get(response.headers["Location"], output, maxRedirections-1, noproxy);
	}

	if(output) sock.read(*output);
	else sock.discard();

	return response.code;
}

int Http::Post(const String &url, const StringMap &post, Stream *output, int maxRedirections, bool noproxy)
{
	Request request(url,"POST");
	request.post = post;

	String host;
	if(!request.headers.get("Host", host))
		throw Exception("Invalid URL");
	
	Address addr;
        bool hasProxy = !noproxy && Config::GetProxyForUrl(url, addr);
        if(hasProxy) request.url = url;		// Full URL for proxy
        else addr.fromString(host);

	Socket sock;
        sock.setTimeout(milliseconds(Config::Get("http_timeout").toInt()));
	try {
                sock.connect(addr, true);       // Connect without proxy
                request.send(sock);
        }
        catch(const NetException &e)
        {
                if(hasProxy) LogWarn("Http::Get", String("HTTP proxy error: ") + e.what());
                throw;
        }

	Response response;
        response.recv(sock);

	if(maxRedirections && response.code/100 == 3 && response.headers.contains("Location"))
	{
		sock.discard();
		sock.close();

		// TODO: support relative URLs, even if not RFC-compliant
		String location = response.headers["Location"];
		
		if(response.code == 303) return Get(location, output, maxRedirections-1, noproxy);
		else return Post(location, post, output, maxRedirections-1, noproxy);
	}

	if(output) sock.read(*output);
	else sock.discard();

	return response.code;
}

void Http::RespondWithFile(const Request &request, const String &fileName)
{
	int code = 200;
	File file;
	
	if(!File::Exist(fileName)) code = 404;
	else {
		if(request.method != "GET" && request.method != "HEAD") code = 405;
		else {
			String ifModifiedSince;
			if(request.headers.get("If-Modified-Since", ifModifiedSince))
			{
				try {
					Time time(ifModifiedSince);
					if(time >= File::Time(fileName))
					{
						Response response(request, 304);
						response.send();
						return;
					}
				}
				catch(const Exception &e)
				{
					LogWarn("Http::RespondWithFile", e.what()); 
				}
			}
		  
			try {
				file.open(fileName, File::Read);
			}
			catch(...)
			{
				code = 403;
			}
		}
	}
	
	int64_t rangeBegin;
	int64_t rangeEnd;
	bool hasRange = request.extractRange(rangeBegin, rangeEnd, file.size());
	if(hasRange) code = 206;
	
	if(code != 200 && code != 206)
	{
		Response response(request, code);
		response.headers["Content-Type"] = "text/html; charset=UTF-8";
		response.send();
		
		if(request.method != "HEAD")
		{
			Html page(response.sock);
			page.header(response.message, true);
			page.open("div", "error");
			page.openLink("/");
			page.image("/error.png", "Error");
			page.closeLink();
			page.br();
			page.br();
			page.open("h1",".huge");
			page.text(String::number(response.code));
			page.br();
			page.text(response.message);
			page.close("h1");
			page.close("div");
			page.footer();
		}
		
		return;
	}
	
	Response response(request, code);
	
	String name = fileName.afterLast(Directory::Separator);
	if(name != request.url.afterLast('/')) 
		response.headers["Content-Disposition"] = "inline; filename=\"" + name + "\"";
	
	if(hasRange)
	{
		 response.headers["Content-Range"] << rangeBegin << '-' << rangeEnd << '/' << file.size();
		 response.headers["Content-Length"]  << rangeEnd - rangeBegin + 1;
	}
	else {
		response.headers["Content-Length"] << file.size();
	}
	
	response.headers["Accept-Ranges"] = "bytes";
	response.headers["Content-Type"] = Mime::GetType(fileName);
	response.headers["Last-Modified"] = File::Time(fileName).toHttpDate();

	response.send();

	if(request.method != "HEAD")
	{
		if(hasRange)
		{
			file.seekRead(rangeBegin);
			file.read(*response.sock, rangeEnd - rangeBegin + 1);
		}
		else {
			file.read(*response.sock);
		}
		file.close();
	}
}

String Http::AppendGet(const String &url, const String &name, const String &value)
{
	char separator = '?';
	if(url.contains(separator)) separator = '&';
	return url + separator + name + "=" + value;
}

}
