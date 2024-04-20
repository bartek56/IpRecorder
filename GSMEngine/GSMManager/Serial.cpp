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

    uint32_t sizeOfMessage = 0;
    char *startOfMessage = nullptr;

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
        else if(result == 0 && sizeOfMessage > 0)
        {
            //std::cout << "timeout" << std::endl;
            newMessageNotify(startOfMessage, sizeOfMessage);
            totalBytesRead = 0;
            sizeOfMessage = 0;
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
                    startOfMessage = buffer.data();
                    uint32_t sizeOfPreviousMessage = totalBytesRead - bytesRead;
                    if(sizeOfPreviousMessage > 0)
                        sizeOfPreviousMessage -= 1;

                    for(int i = sizeOfPreviousMessage; i < (totalBytesRead - 1); i++)
                    {
                        auto asciValue1 = int(buffer[i]);
                        auto asciValue2 = int(buffer[i + 1]);

                        //std::cout << "byte " << i << " " << asciValue1 << std::endl;

                        if(asciValue1 == 0 && asciValue2 == 0)
                        {
                            break;
                        }
                        sizeOfMessage++;

                        if(asciValue1 == 13 && asciValue2 == 10)
                        {
                            if(i == 0)
                            {
                                //std::cout << "skip first CRLF" << std::endl;
                                i += 1;
                                sizeOfMessage += 1;
                                continue;
                            }
                            i++;
                            sizeOfMessage++;
                            //std::cout << "byte " << i << " " << asciValue2 << "  it is CRLF " << std::endl;
                            if(int(buffer[i + 1]) == 13 && int(buffer[i + 2]) == 10)
                            {
                                //std::cout << "skip CRLF duplicate" << std::endl;
                                //std::cout << "byte " << i + 1 << " " << int(buffer[i + 1]) << std::endl;
                                //std::cout << "byte " << i + 2 << " " << int(buffer[i + 2]) << std::endl;
                                // skip CRLF duplicate
                                i += 2;
                            }
                            newMessageNotify(startOfMessage, sizeOfMessage);
                            sizeOfMessage = 0;
                            startOfMessage = buffer.data() + i + 1;
                            //std::cout << "i: " << i << "   totalBytesRead: " << totalBytesRead << std::endl;
                            if(i + 1 >= totalBytesRead)
                            {
                                //std::cout << "no more data" << std::endl;
                                totalBytesRead = 0;
                                break;
                            }
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

void Serial::newMessageNotify(char *buffer, uint32_t &sizeOfMessage)
{
    auto newMessage = std::string(buffer, sizeOfMessage);
    //std::cout << "new message " << newMessage << std::endl;
    if(readEvent)
        readEvent(newMessage);
}
