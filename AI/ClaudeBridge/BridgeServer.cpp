/*
 * BridgeServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BridgeServer.h"

#include "../../lib/json/JsonNode.h"

namespace asio = boost::asio;
using asio::ip::tcp;

BridgeServer::BridgeServer(uint16_t port, Handler handler)
	: handler(std::move(handler))
	, acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), port))
{
	thread = std::thread(&BridgeServer::run, this);
}

BridgeServer::~BridgeServer()
{
	stopping = true;
	boost::system::error_code ec;
	acceptor.close(ec);
	{
		std::lock_guard lock(socketMutex);
		if(activeSocket)
			activeSocket->close(ec);
	}
	thread.join();
}

bool BridgeServer::hasClient()
{
	std::lock_guard lock(socketMutex);
	return activeSocket != nullptr;
}

void BridgeServer::run()
{
	while(!stopping)
	{
		tcp::socket socket(io);
		boost::system::error_code ec;
		acceptor.accept(socket, ec);
		if(ec)
		{
			if(stopping)
				return;
			// a pending connection that was closed before accept must not kill the accept loop
			logAi->warn("ClaudeBridge: accept failed: %s", ec.message());
			continue;
		}

		{
			std::lock_guard lock(socketMutex);
			activeSocket = &socket;
		}
		logAi->info("ClaudeBridge: client connected on port %d", acceptor.local_endpoint().port());

		asio::streambuf buffer;
		while(!stopping)
		{
			asio::read_until(socket, buffer, '\n', ec);
			if(ec)
				break;

			std::istream stream(&buffer);
			std::string line;
			std::getline(stream, line);
			asio::write(socket, asio::buffer(process(line) + "\n"), ec);
			if(ec)
				break;
		}

		std::lock_guard lock(socketMutex);
		activeSocket = nullptr;
	}
}

std::string BridgeServer::process(const std::string & line) const
{
	JsonNode reply;
	try
	{
		JsonNode request(line.data(), line.size(), "ClaudeBridge request");
		if(!request.isStruct() || !request["cmd"].isString())
			throw std::runtime_error("request must be an object with a string \"cmd\"");

		reply["id"] = request["id"];
		reply["result"] = handler(request["cmd"].String(), request["args"]);
		reply["ok"].Bool() = true;
	}
	catch(const std::exception & e)
	{
		reply["ok"].Bool() = false;
		reply["error"].String() = e.what();
	}

	// JsonWriter escapes line breaks and tabs inside strings, so the remaining ones are formatting
	std::string text = reply.toCompactString();
	std::erase_if(text, [](char c) { return c == '\n' || c == '\r' || c == '\t'; });
	return text;
}
