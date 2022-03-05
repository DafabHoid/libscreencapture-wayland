/*******************************************************************************
   Copyright © 2022 by DafabHoid <github@dafaboid.de>

   SPDX-License-Identifier: GPL-3.0-or-later
*******************************************************************************/
#include <sdbus-c++/sdbus-c++.h>
#include "xdg-desktop-portal.hpp"
#include <cstdio>
#include <cstdarg>
#include <random>
#include <array>
#include <utility>
#include <vector>
#include <map>
#include <future>
#include <memory>
#include <exception>
#include <variant>

namespace portal
{

using OptionsMap = std::map<std::string, sdbus::Variant>;
using RequestParameter = std::variant<sdbus::ObjectPath, std::string>;
using RequestParameterList = std::vector<RequestParameter>;
using PortalResponse = OptionsMap;

static const char PORTAL_BUS[] = "org.freedesktop.portal.Desktop";
static const char PORTAL_PATH[] = "/org/freedesktop/portal/desktop";
static const char SCREENCAST_INTERFACE[] = "org.freedesktop.portal.ScreenCast";
static const char PORTAL_REQUEST_INTERFACE[] = "org.freedesktop.portal.Request";
static const char KEY_TOKEN[] = "handle_token";
static const char KEY_SESSION_TOKEN[] = "session_handle_token";
static const char KEY_SESSION_HANDLE[] = "session_handle";
static const char KEY_SOURCE_TYPES[] = "types";
static const char KEY_CURSOR_MODE[] = "cursor_mode";
static constexpr unsigned char CURSOR_MODE_HIDDEN = 1u;
static constexpr unsigned char CURSOR_MODE_EMBED = 2u;
static constexpr unsigned char CURSOR_MODE_META = 4u;

class DBusException : public std::exception
{
	char message[128];
public:
	explicit DBusException(const char* messageFmtStr, ...) noexcept __attribute__((format(printf, 2, 3)));
	const char* what() const noexcept override { return message; }
};


enum PortalResponseStatus
{
	SUCCESS = 0,
	USER_CANCELLED = 1,
	ABORTED = 2,
};

std::optional<PortalResponse> portalRequest(sdbus::IProxy& portal, const std::string& methodName,
											OptionsMap options, const RequestParameterList& fixedParams)
{
	std::string myName = portal.getConnection().getUniqueName().substr(1);
	std::replace(myName.begin(), myName.end(), '.', '_');
	sdbus::ObjectPath expectedReplyPath = "/org/freedesktop/portal/desktop/request/" + myName + "/gfxtablet";

	// install response signal receiver on reply path
	std::unique_ptr<sdbus::IProxy> signalProxy = sdbus::createProxy(portal.getConnection(), PORTAL_BUS, expectedReplyPath);

	std::promise<std::pair<uint32_t, OptionsMap>> promisedResponse;
	// this handler is unregistered when signalProxy is destroyed, so capturing stack variables is safe
	auto responseSignalHandler = [&] (const sdbus::Error* err, uint32_t resultCode, OptionsMap results)
	{
#ifndef NDEBUG
		// signalProxy is captured by reference
		// while the pointer itself may change (when expectedPath != replyPath), the unique_ptr reference stays valid
		const auto& signal = *signalProxy->getCurrentlyProcessedMessage();
		printf("Recv signal: sender = %s, path = %s\n", signal.getSender().c_str(), signal.getPath().c_str());
#endif
		if (err != nullptr)
		{
			// parsing of arguments failed
			// → pass error to thread waiting on the promise object
			promisedResponse.set_exception(std::make_exception_ptr(*err));
		}
		else
		{
			promisedResponse.set_value({resultCode, std::move(results)});
		}
	};

	signalProxy->uponSignal("Response").onInterface(PORTAL_REQUEST_INTERFACE).call(responseSignalHandler);
	signalProxy->finishRegistration();

	auto method = portal.createMethodCall(SCREENCAST_INTERFACE, methodName);
	for (const RequestParameter& p : fixedParams)
	{
		if (std::holds_alternative<sdbus::ObjectPath>(p))
		{
			method << std::get<sdbus::ObjectPath>(p);
		}
		else if (std::holds_alternative<std::string>(p))
		{
			method << std::get<std::string>(p);
		}
	}
	options[KEY_TOKEN] = "gfxtablet";
	method << options;

	// call method, which returns a path to a reply object that is signalled when the actual result is ready
	auto reply = portal.callMethod(method);
	sdbus::ObjectPath replyPath;
	reply >> replyPath;

	if (expectedReplyPath != replyPath)
	{
		std::fprintf(stderr, "Response path is not as expected, xdg-desktop-portal too old? Got: %s, expected: %s\n",
		             replyPath.c_str(), expectedReplyPath.c_str());
		signalProxy = sdbus::createProxy(portal.getConnection(), PORTAL_BUS, replyPath);
		signalProxy->uponSignal("Response").onInterface(PORTAL_REQUEST_INTERFACE).call(responseSignalHandler);
		signalProxy->finishRegistration();
	}

	// wait for actual result, placed in the promise by the signal handler
	auto [resultCode, results] = promisedResponse.get_future().get();

	if (resultCode == PortalResponseStatus::USER_CANCELLED)
	{
		return std::nullopt;
	}
	if (resultCode >= PortalResponseStatus::ABORTED)
	{
		throw DBusException("Portal request has been aborted");
	}

	return std::move(results);
}

sdbus::UnixFd openPipeWireRemoteFd(sdbus::IProxy& portal, const sdbus::ObjectPath& sessionHandle)
{
	sdbus::MethodCall method = portal.createMethodCall(SCREENCAST_INTERFACE, "OpenPipeWireRemote");
	method << sessionHandle;
	// pass empty options
	OptionsMap options;
	method << options;
	sdbus::MethodReply reply = portal.callMethod(method);
	sdbus::UnixFd fd;
	reply >> fd;
	return fd;
}

std::pair<int,uint32_t> getPipeWireShareInfo(sdbus::IConnection& connection, CursorMode cursorMode)
{
	std::unique_ptr<sdbus::IProxy> portal = sdbus::createProxy(connection, PORTAL_BUS, PORTAL_PATH);

	uint32_t interfaceVersion = portal->getProperty("version").onInterface(SCREENCAST_INTERFACE);
	uint32_t screenCastSources = portal->getProperty("AvailableSourceTypes").onInterface(SCREENCAST_INTERFACE);
	uint32_t cursorModes = 0;
	if (interfaceVersion >= 2)
	{
		cursorModes = portal->getProperty("AvailableCursorModes").onInterface(SCREENCAST_INTERFACE);
	}
	std::printf("ScreenCast interface, version %u. cursorModes = %#x screenCastSources = %#x\n",
	             interfaceVersion, cursorModes, screenCastSources);

	// generate session name with random characters
	auto charGenerator = std::uniform_int_distribution('a', 'z');
	auto seed = std::default_random_engine{std::random_device()()};
	std::array<char, 20> sessionName;
	std::generate(sessionName.begin(), sessionName.end()-1, [&] () { return charGenerator(seed); });
	sessionName.back() = 0; // null terminate

	// create a Session object first
	auto response = portalRequest(*portal, "CreateSession", {{KEY_SESSION_TOKEN, sessionName.begin()}}, {});
	if (!response)
	{
		throw DBusException("No response received for CreateSession");
	}
	auto sessionHandleIt = response.value().find(KEY_SESSION_HANDLE);
	if (sessionHandleIt == response.value().end() || !sessionHandleIt->second.containsValueOfType<std::string>())
	{
		throw DBusException("Portal::CreateSession did not return a session handle!");
	}
	auto sessionHandle = static_cast<sdbus::ObjectPath>(sessionHandleIt->second.get<std::string>());
	std::printf("Session handle acquired: %s\n", sessionHandle.c_str());


	// select the source type and cursor mode
	OptionsMap options;
	options[KEY_SOURCE_TYPES] = screenCastSources;
	if (interfaceVersion >= 2)
	{
		unsigned int cmRequested = 0;
		if (cursorMode & ::CURSOR_MODE_HIDDEN)
			cmRequested |= portal::CURSOR_MODE_HIDDEN;
		if (cursorMode & ::CURSOR_MODE_EMBED)
			cmRequested |= portal::CURSOR_MODE_EMBED;
		if (cursorMode & ::CURSOR_MODE_META)
			cmRequested |= portal::CURSOR_MODE_META;
		unsigned int cm = cmRequested & cursorModes;
		if (cm == 0u)
			cm = portal::CURSOR_MODE_HIDDEN;
		options[KEY_CURSOR_MODE] = cm;
	}
	response = portalRequest(*portal, "SelectSources", std::move(options), {sessionHandle});
	if (!response)
	{
		throw DBusException("No response received for SelectSources");
	}

	response = portalRequest(*portal, "Start", {}, {sessionHandle, /* parent_window */ std::string()});
	if (!response)
	{
		std::fprintf(stderr, "User cancelled screen sharing\n");
		return {-1, {}};
	}
	auto streamIt = response.value().find("streams");
	if (streamIt == response.value().end())
	{
		throw DBusException("Portal::Start did not return a streams array!");
	}
	std::vector<sdbus::Struct<uint32_t, OptionsMap>> streams = streamIt->second;
	if (streams.empty())
	{
		throw DBusException("Portal::Start did not return any stream!");
	}

	// use the first stream, but it should be only one by default anyway
	auto& stream = streams.at(0);
	uint32_t pipeWireNode = stream.get<0>();
	sdbus::UnixFd fd = openPipeWireRemoteFd(*portal, sessionHandle);
	return {fd.release(), {pipeWireNode}};
}

std::optional<SharedScreen> requestPipeWireShare(CursorMode cursorMode)
{
	std::unique_ptr<sdbus::IConnection> connection = sdbus::createSessionBusConnection();
	connection->enterEventLoopAsync();

	try
	{
		auto [fd, pipeWireNode] = getPipeWireShareInfo(*connection, cursorMode);
		if (fd < 0)
			return std::nullopt;
		return SharedScreen {std::move(connection), fd, pipeWireNode};
	}
	catch (const sdbus::Error& e)
	{
		throw DBusException("%s", e.what());
	}
}

DBusException::DBusException(const char* messageFmtStr, ...) noexcept
{
	std::va_list v_args;
	va_start(v_args, messageFmtStr);
	std::vsnprintf(message, sizeof(message), messageFmtStr, v_args);
	va_end(v_args);

	dumpStackTrace();
}

} // namespace portal

// C interface

SharedScreen_t* requestPipeWireShareFromPortal(CursorMode cursorMode)
{
	try
	{
		std::optional<SharedScreen> shareInfo = portal::requestPipeWireShare(cursorMode);
		if (!shareInfo)
			return nullptr;
		auto cStruct = new SharedScreen_t;
		auto& cppStruct = shareInfo.value();
		cStruct->pipeWireFd = cppStruct.pipeWireFd;
		cStruct->pipeWireNode = cppStruct.pipeWireNode;
		cStruct->connection = cppStruct.dbusConnection.release();
		return cStruct;
	}
	catch (const std::exception& e)
	{
		fprintf(stderr, "%s\n", e.what());
		return nullptr;
	}
}

void dropSharedScreen(SharedScreen_t* shareInfo)
{
	sdbus::IConnection* conn = static_cast<sdbus::IConnection *>(shareInfo->connection);
	delete conn;
	delete shareInfo;
}