#ifndef SERIAL_HPP
#define SERIAL_HPP

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <csignal>
#include <functional>

#include <atomic>

class Serial
{
public:
    explicit Serial(std::string_view serialPort);
    Serial(const Serial&) = delete;
    Serial &operator=(const Serial&) = delete;
    Serial(Serial&&) = delete;
    Serial &operator=(Serial&&) = delete;
    ~Serial();

    void setReadEvent(std::function<void(std::string&)>&& readEventCb);

    void sendMessage(const std::string &message);
    void sendChar(const char &message);

private:
    static constexpr uint32_t k_bufferSize = 256;
    static constexpr size_t k_activeTimems = 400;
    static constexpr size_t k_sleepTimems = 100;
    static constexpr size_t k_activeTimeus = k_activeTimems * 1000;
    static constexpr size_t k_sleepTimeus = k_sleepTimems * 1000;
    static constexpr int k_CR = 13;
    static constexpr int k_LF = 10;


    int fd;
    std::vector<std::string> m_messagesWriteQueue;

    std::mutex serialMutex;
    std::condition_variable sendCondition;
    std::mutex messagesWriteMutex;
    bool isNewMessageToSend=false;

    std::atomic<bool> serialRunning;
    std::unique_ptr<std::thread> receiver;
    std::unique_ptr<std::thread> sender;
    std::function<void(std::string&)> readEvent;

    std::array<char, k_bufferSize> buffer;

    void sendThread();
    void readThread();
    void newMessageNotify(char* buffer, const uint32_t& sizeOfMessage);
};

#endif// SERIAL_HPP
