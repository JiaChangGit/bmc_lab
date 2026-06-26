#include "app/Application.hpp"

#include <cstdlib>
#include <pthread.h>
#include <spdlog/spdlog.h>

#include <csignal>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{

/*
 * 執行期參數 (runtime options)。
 * 目前只支援設定檔路徑與 HTTP port，其他服務組裝交給 Application。
 */
struct RuntimeOptions
{
    std::string configPath {"config/ai_server_profile.json"};
    unsigned short port {8080};
};

/*
 * 解析命令列參數 (command-line arguments)。
 *
 * 支援:
 *   --config <path>      指定平台設定 JSON。
 *   --port <1-65535>    指定 HTTP API 監聽 port。
 *   --help              顯示使用方式後結束。
 *
 * 錯誤處理:
 *   缺少參數、port 非數字、port 超出 TCP port 範圍或未知參數時丟出例外。
 */
RuntimeOptions parseOptions(int argc, char* argv[])
{
    RuntimeOptions options;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--config")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--config requires a path argument");
            }

            options.configPath = argv[++index];
        }
        else if (argument == "--port")
        {
            if (index + 1 >= argc)
            {
                throw std::runtime_error("--port requires a numeric argument");
            }

            const std::string portValue = argv[++index];
            std::size_t parsedLength = 0;
            const unsigned long parsedPort = std::stoul(portValue, &parsedLength, 10);
            if (parsedLength != portValue.size() || parsedPort == 0 ||
                parsedPort > static_cast<unsigned long>(std::numeric_limits<unsigned short>::max()))
            {
                throw std::runtime_error("Invalid --port value: " + portValue);
            }

            options.port = static_cast<unsigned short>(parsedPort);
        }
        else if (argument == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [--config <path>] [--port <1-65535>]\n";
            std::exit(0);
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + argument);
        }
    }

    return options;
}

/*
 * 將 SIGINT / SIGTERM 先擋住，讓主執行緒可以用 sigwait() 同步等待結束訊號。
 *
 * 關鍵字:
 *   Signal Handling（訊號處理）是 Linux 程式接收 Ctrl+C 或 systemd stop 的機制。
 *   這裡不在 signal handler 內做清理，避免在非同步 handler 內呼叫不安全的函式。
 */
void prepareSignalMask()
{
    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    sigaddset(&signalSet, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signalSet, nullptr);
}

/*
 * 等待關閉訊號。
 *
 * 收到 SIGINT 或 SIGTERM 後回到 main()，再依 Application::stop() 的順序停止服務。
 */
void waitForShutdownSignal()
{
    sigset_t signalSet;
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    sigaddset(&signalSet, SIGTERM);

    int signalNumber = 0;
    sigwait(&signalSet, &signalNumber);
    spdlog::info("Received shutdown signal {}", signalNumber);
}

} // 匿名命名空間

int main(int argc, char* argv[])
{
    try
    {
        // spdlog pattern 固定時間、等級與訊息格式，方便 Demo 與排錯時對照 log。
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%^%l%$] %v");
        spdlog::set_level(spdlog::level::info);

        // 啟動順序：解析參數 -> 準備訊號 -> 建立 Application -> 啟動服務 -> 等待停止訊號。
        const RuntimeOptions options = parseOptions(argc, argv);
        prepareSignalMask();
        openbmc::app::Application application(options.configPath, options.port);
        application.start();
        waitForShutdownSignal();
        application.stop();
        return 0;
    }
    catch (const std::exception& exception)
    {
        // 啟動階段常見錯誤包含設定檔無法讀取、JSON 欄位缺失、port 被占用或 D-Bus 初始化失敗。
        spdlog::error("Application failed: {}", exception.what());
        return 1;
    }
}
