#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace openbmc::services
{
class ManagementService;
}

namespace openbmc::redfish
{

class RedfishApiServer
{
  public:
    /*
     * 提供受 Redfish 結構啟發的 HTTP API (schema-inspired API)。
     * 所有查詢與動作都透過服務層，避免 HTTP handler 直接持有平台狀態。
     */
    explicit RedfishApiServer(
        services::ManagementService& managementService, const std::string& bindAddress = "0.0.0.0",
        unsigned short port = 8080);
    ~RedfishApiServer();

    /*
     * 用途:
     *   啟動 HTTP 監聽執行緒。
     *
     * 錯誤處理:
     *   bind 或 listen 失敗時會由 Boost.Asio 丟出例外。
     *
     * 注意事項:
     *   API 是 Redfish schema-inspired，不代表符合 Redfish 標準的全部要求。
     */
    void start();

    /*
     * 用途:
     *   停止 HTTP acceptor 並等待接收執行緒結束。
     *
     * 注意事項:
     *   stop() 會 cancel / close acceptor，讓 non-blocking accept loop 能離開。
     */
    void stop();

  private:
    services::ManagementService& managementService_;
    std::string bindAddress_;
    unsigned short port_;
    std::atomic<bool> running_ {false};
    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::thread acceptThread_;

    void acceptLoop();
    void handleSession(boost::asio::ip::tcp::socket socket);
};

} // 命名空間 openbmc::redfish
