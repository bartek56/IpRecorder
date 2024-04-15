#include "Serial.hpp"

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <array>
#include <mutex>

constexpr size_t Serial::k_sleepTimems;
constexpr size_t Serial::k_activeTimems;

Serial::Serial(const std::string &serialPort) : fd(-1), m_messagesQueue(), serialMutex(), messageMutex(), serialRunning(true), receiver(nullptr), sender(nullptr)
{
    fd = open(serialPort.c_str(), O_RDWR);
    if(fd == -1)
    {
        std::cerr << "GSM serial is not connected on port: " << serialPort << std::endl;
        throw std::runtime_error("Initialization failed");
    }

    // serial port configuration
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B19200);
    cfsetospeed(&options, B19200);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;// disable parity
    options.c_cflag &= ~CSTOPB;// one bit of stop
    options.c_cflag &= ~CSIZE; // disable bits of size
    options.c_cflag |= CS8;    // 8 bits of data
    options.c_lflag = 0;       //~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag = 0;       // ~(IXON | IXOFF | IXANY);
    tcsetattr(fd, TCSANOW, &options);

    // non-blocking mode
    fcntl(fd, F_SETFL, O_NONBLOCK);
    Serial::serialRunning.store(true);

    receiver = std::make_unique<std::thread>([this]() { this->readThread(); });
    sender = std::make_unique<std::thread>([this]() { this->sendThread(); });
}

Serial::~Serial()
{
    serialRunning.store(false);
    sender->join();
    receiver->join();
    close(fd);
    std::cout << "Serial was closed" << std::endl;
}

void Serial::readThread()
{
    std::array<char, k_bufferSize> buffer;
    std::fill(buffer.begin(), buffer.end(), 0);

    fd_set read_fds;
    int bytesRead = 0;
    uint32_t totalBytesRead = 0;

    while(serialRunning.load())
    {
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = k_activeTimeus;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        // wait for data
        int result = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if(result == -1)
        {
            std::cerr << "error with select()." << std::endl;
            break;
        }
        else if(result == 0 && totalBytesRead > 0)
        {
            newMessageNotify(buffer, totalBytesRead, true);
        }

        else if(result > 0)
        {
            // check if select is for out device
            if(FD_ISSET(fd, &read_fds))
            {
                {
                    std::lock_guard<std::mutex> lock(serialMutex);
                    bytesRead = read(fd, buffer.data() + totalBytesRead, sizeof(buffer) - totalBytesRead - 1);
                }

                if(bytesRead > 0)
                {
                    totalBytesRead += bytesRead;
                    // skip begining of message, because sometimes it is CRLF. It's to short message to send
                    for(auto iter = buffer.begin() + 2; (iter + 1) != buffer.end(); iter++)
                    {
                        auto asciValue1 = int(*iter);
                        auto asciValue2 = int(*(iter + 1));

                        if(asciValue1 == 0 && asciValue2 == 0)
                        {
                            break;
                        }

                        if(asciValue1 == 13 && asciValue2 == 10)
                        {
                            newMessageNotify(buffer, totalBytesRead, false);
                            break;
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(k_sleepTimems));
    }
    std::cout << "receiver closed" << std::endl;
}

void Serial::sendThread()
{
    while(serialRunning.load())
    {
        {
            std::unique_lock<std::mutex> lk(messageMutex);
            cv.wait_for(lk, std::chrono::milliseconds(k_activeTimems), [this]() { return isNewMessage; });

            if(m_messagesQueue.size() > 0)
            {
                auto newMessage = m_messagesQueue[0];
                int bytesWritten = 0;
                {
                    std::lock_guard<std::mutex> lockSerial(serialMutex);
                    if(newMessage.size() == 2)
                    {
                        auto ptr = static_cast<char>(std::stoi(newMessage.c_str()));
                        bytesWritten = write(fd, &ptr, 1);
                    }
                    else
                    {
                        bytesWritten = write(fd, newMessage.c_str(), newMessage.size());
                    }
                }
                if(bytesWritten < 0)
                {
                    std::cerr << "Error to send data!" << std::endl;
                }
                /*
                else
                {
                    std::cout << "Message was send: " << newMessage << std::endl;
                }
                */

                m_messagesQueue.erase(m_messagesQueue.begin());
            }
            else
            {
                isNewMessage = false;
            }
        }
        //std::this_thread::sleep_for(std::chrono::milliseconds(k_sleepTimems));
    }
    std::cout << "sender closed" << std::endl;
}


void Serial::sendMessage(std::string message)
{
    std::lock_guard<std::mutex> lock(messageMutex);
    message.append("\r");
    m_messagesQueue.push_back(message);
    isNewMessage = true;
    cv.notify_one();
}

void Serial::sendChar(char message)
{
    std::lock_guard<std::mutex> lock(messageMutex);
    m_messagesQueue.push_back(std::to_string(message));
    isNewMessage = true;
    cv.notify_one();
}

void Serial::setReadEvent(std::function<void(std::string &)> cb)
{
    readEvent = cb;
}

void Serial::newMessageNotify(std::array<char, k_bufferSize> &buffer, uint32_t &sizeOfMessage, const bool &timeout)
{
    if(!timeout)
    {
        // std::cout << "without timeout" << std::endl;
        uint32_t bytes = 0;
        char *startOfMessage = buffer.data();
        for(auto iter = buffer.begin(); (iter + 1 != buffer.end()); iter++)
        {
            auto asciValue1 = int(*iter);
            auto asciValue2 = int(*(iter + 1));

            if(asciValue1 == 0 && asciValue2 == 0)
            {
                break;
            }
            bytes++;
            // std::cout << "byte " << bytes << " - " << "hex: " << asciValue1 << std::endl;
            if(asciValue1 == 13 && asciValue2 == 10)
            {
                iter++;
                bytes += 1;
                // skip sending message if begining is CRLF
                if(bytes == 2)
                {
                    startOfMessage = startOfMessage + bytes;
                    bytes = 0;
                    continue;
                }


                // std::cout << "byte " << bytes << " - " << "hex: " << asciValue2 << std::endl;
                // std::cout << "bytes of new message " << bytes << std::endl;
                auto newMessage = std::string(startOfMessage, bytes);
                startOfMessage = startOfMessage + bytes;

                if(readEvent)
                    readEvent(newMessage);
                bytes = 0;
            }
        }
        // send rest of data without CRLF
        if(bytes > 0)
        {
            auto newMessage = std::string(startOfMessage, bytes);

            if(readEvent)
                readEvent(newMessage);
        }
    }
    else
    {
        /// if message was came with timeout, it means message is without CRLF
        // std::cout << "with timeout" << std::endl;
        // uint32_t bytes = 0;
        // for(auto iter = buffer.begin(); iter + 1 != buffer.end(); iter++)
        // {
        //    auto asciValue1 = int(*iter);
        //    auto asciValue2 = int(*(iter + 1));
        //

        //     if(asciValue1 == 0 && asciValue2 == 0)
        //     {
        //         break;
        //     }
        //     std::cout << "byte " << bytes << " - "
        //               << "hex: " << asciValue1 << std::endl;
        //     bytes++;
        // }
        auto newMessage = std::string(buffer.data(), sizeOfMessage);
        if(readEvent)
            readEvent(newMessage);
    }
    sizeOfMessage = 0;
    std::fill(buffer.begin(), buffer.end(), 0);
}