/*
 * BridgeServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <boost/asio.hpp>

class JsonNode;

/// Serves one local TCP client at a time. Request: {"id", "cmd", "args"} per line, reply: {"id", "ok", "result" | "error"} per line.
class BridgeServer
{
public:
	using Handler = std::function<JsonNode(const std::string & cmd, const JsonNode & args)>;

private:
	Handler handler;
	boost::asio::io_context io;
	boost::asio::ip::tcp::acceptor acceptor;
	std::mutex socketMutex;
	boost::asio::ip::tcp::socket * activeSocket = nullptr;
	std::atomic<bool> stopping = false;
	std::thread thread;

	void run();
	std::string process(const std::string & line) const;

public:
	BridgeServer(uint16_t port, Handler handler);
	~BridgeServer();

	bool hasClient();
};
