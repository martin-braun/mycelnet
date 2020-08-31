#include <lokimq/lokimq.h>
#include <nlohmann/json.hpp>
#include <cxxopts.hpp>
#include <future>
#include <vector>
#include <array>
#include <net/net.hpp>

#ifdef _WIN32
// add the unholy windows headers for iphlpapi
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <strsafe.h>
#else
#include <sys/wait.h>
#endif

/// do a lokimq request on an lmq instance blocking style
/// returns a json object parsed from the result
std::optional<nlohmann::json>
LMQ_Request(
    lokimq::LokiMQ& lmq,
    const lokimq::ConnectionID& id,
    std::string_view method,
    std::optional<nlohmann::json> args = std::nullopt)
{
  std::promise<std::optional<std::string>> result_promise;

  auto handleRequest = [&result_promise](bool success, std::vector<std::string> result) {
    if ((not success) or result.empty())
    {
      result_promise.set_value(std::nullopt);
      return;
    }
    result_promise.set_value(result[0]);
  };
  if (args.has_value())
  {
    lmq.request(id, method, handleRequest, args->dump());
  }
  else
  {
    lmq.request(id, method, handleRequest);
  }
  auto ftr = result_promise.get_future();
  const auto str = ftr.get();
  if (str.has_value())
    return nlohmann::json::parse(*str);
  return std::nullopt;
}

int
main(int argc, char* argv[])
{
  cxxopts::Options opts("lokinet-vpn", "LokiNET vpn control utility");

  opts.add_options()("v,verbose", "Verbose", cxxopts::value<bool>())(
      "h,help", "help", cxxopts::value<bool>())("up", "put vpn up", cxxopts::value<bool>())(
      "down", "put vpn down", cxxopts::value<bool>())(
      "exit", "specify exit node address", cxxopts::value<std::string>())(
      "rpc", "rpc url for lokinet", cxxopts::value<std::string>())(
      "endpoint", "endpoint to use", cxxopts::value<std::string>())(
      "token", "exit auth token to use", cxxopts::value<std::string>());

  lokimq::address rpcURL("tcp://127.0.0.1:1190");
  std::string exitAddress;
  std::string endpoint = "default";
  std::optional<std::string> token;
  lokimq::LogLevel logLevel = lokimq::LogLevel::warn;
  bool goUp = false;
  bool goDown = false;
  try
  {
    const auto result = opts.parse(argc, argv);

    if (result.count("help") > 0)
    {
      std::cout << opts.help() << std::endl;
      return 0;
    }

    if (result.count("verbose") > 0)
    {
      logLevel = lokimq::LogLevel::debug;
    }
    if (result.count("rpc") > 0)
    {
      rpcURL = lokimq::address(result["rpc"].as<std::string>());
    }
    if (result.count("exit") > 0)
    {
      exitAddress = result["exit"].as<std::string>();
    }
    goUp = result.count("up") > 0;
    goDown = result.count("down") > 0;

    if (result.count("endpoint") > 0)
    {
      endpoint = result["endpoint"].as<std::string>();
    }
    if (result.count("token") > 0)
    {
      token = result["token"].as<std::string>();
    }
  }
  catch (const cxxopts::option_not_exists_exception& ex)
  {
    std::cerr << ex.what();
    std::cout << opts.help() << std::endl;
    return 1;
  }
  catch (std::exception& ex)
  {
    std::cout << ex.what() << std::endl;
    return 1;
  }
  if ((not goUp) and (not goDown))
  {
    std::cout << opts.help() << std::endl;
    return 1;
  }
  if (goUp and exitAddress.empty())
  {
    std::cout << "no exit address provided" << std::endl;
    return 1;
  }

  lokimq::LokiMQ lmq{[](lokimq::LogLevel lvl, const char* file, int line, std::string msg) {
                       std::cout << lvl << " [" << file << ":" << line << "] " << msg << std::endl;
                     },
                     logLevel};

  lmq.start();

  std::promise<bool> connectPromise;

  const auto connID = lmq.connect_remote(
      rpcURL,
      [&connectPromise](auto) { connectPromise.set_value(true); },
      [&connectPromise](auto, std::string_view msg) {
        std::cout << "failed to connect to lokinet RPC: " << msg << std::endl;
        connectPromise.set_value(false);
      });

  auto ftr = connectPromise.get_future();
  if (not ftr.get())
  {
    return 1;
  }

  std::vector<std::string> firstHops;
  std::string ifname;

  const auto maybe_status = LMQ_Request(lmq, connID, "llarp.status");
  if (not maybe_status.has_value())
  {
    std::cout << "call to llarp.status failed" << std::endl;
    return 1;
  }

  try
  {
    // extract first hops
    const auto& links = maybe_status->at("result")["links"]["outbound"];
    for (const auto& link : links)
    {
      const auto& sessions = link["sessions"]["established"];
      for (const auto& session : sessions)
      {
        std::string addr = session["remoteAddr"];
        const auto pos = addr.find(":");
        firstHops.push_back(addr.substr(0, pos));
      }
    }
    // get interface name
#ifdef _WIN32
    // strip off the "::ffff."
    ifname = maybe_status->at("result")["services"][endpoint]["ifaddr"];
    const auto pos = ifname.find("/");
    if (pos != std::string::npos)
    {
      ifname = ifname.substr(0, pos);
    }
#else
    ifname = maybe_status->at("result")["services"][endpoint]["ifname"];
#endif
  }
  catch (std::exception& ex)
  {
    std::cout << "failed to parse result: " << ex.what() << std::endl;
    return 1;
  }
  if (goUp)
  {
    std::optional<nlohmann::json> maybe_result;
    if (token.has_value())
    {
      maybe_result = LMQ_Request(
          lmq,
          connID,
          "llarp.exit",
          nlohmann::json{{"exit", exitAddress}, {"range", "0.0.0.0/0"}, {"token", *token}});
    }
    else
    {
      maybe_result = LMQ_Request(
          lmq, connID, "llarp.exit", nlohmann::json{{"exit", exitAddress}, {"range", "0.0.0.0/0"}});
    }

    if (not maybe_result.has_value())
    {
      std::cout << "could not add exit" << std::endl;
      return 1;
    }

    if (maybe_result->contains("error") and maybe_result->at("error").is_string())
    {
      std::cout << maybe_result->at("error").get<std::string>() << std::endl;
      return 1;
    }
  }
  if (goDown)
  {
    LMQ_Request(lmq, connID, "llarp.exit", nlohmann::json{{"range", "0.0.0.0/0"}, {"unmap", true}});
  }

  return 0;
}