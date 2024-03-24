#include "Serial.hpp"

#include <fcntl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <array>

int Serial::fd = -1;
std::vector<std::string> Serial::m_messagesQueue;
std::mutex Serial::serialMutex;
std::mutex Serial::messageMutex;
std::atomic<bool> Serial::serialRunning;

Serial::Serial(std::string serialPort) : receiver(nullptr), sender(nullptr)
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
    Serial::serialRunning = true;

    receiver = std::make_unique<std::thread>(readThread);
    sender = std::make_unique<std::thread>(sendThread);
    std::cout << "initialized" << std::endl;
}

Serial::~Serial()
{
    serialRunning = false;
    receiver->join();
    sender->join();
    close(fd);
    std::cout << "Serial was closed" << std::endl;
}

void Serial::readThread()
{
    std::array<char, k_bufferSize> buffer;
    std::fill(buffer.begin(), buffer.end(), 0);

    fd_set read_fds;
    int bytesRead = 0;
    int totalBytesRead = 0;

    while(serialRunning)
    {
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);

        // wait for data
        int result = select(fd + 1, &read_fds, NULL, NULL, &timeout);
        if(result == -1)
        {
            std::cerr << "error with select()." << std::endl;
            break;
        }

        else if(result > 0)
        {
            // check if select is for out device
            if(FD_ISSET(fd, &read_fds))
            {
                std::lock_guard<std::mutex> lock(serialMutex);
                bytesRead = read(fd, buffer.data() + totalBytesRead, sizeof(buffer) - totalBytesRead - 1);

                if(bytesRead > 0)
                {
                    totalBytesRead += bytesRead;
                    if(static_cast<int>(buffer[totalBytesRead - 1]) == 10 && static_cast<int>(buffer[totalBytesRead - 2]) == 13)
                    {
                        std::cout << "new message: " << std::string(buffer.data(), totalBytesRead) << std::endl;
                        totalBytesRead = 0;
                        std::fill(buffer.begin(), buffer.end(), 0);
                    }
                }
            }
        }
    }
    std::cout << "receiver closed" << std::endl;
}

void Serial::sendThread()
{
    while(serialRunning)
    {
        {
            std::lock_guard<std::mutex> lockMessage(messageMutex);
            if(m_messagesQueue.size() > 0)
            {
                auto newMessage = m_messagesQueue[0];
                int bytesWritten = 0;
                {
                    std::lock_guard<std::mutex> lockSerial(serialMutex);
                    bytesWritten = write(fd, newMessage.c_str(), newMessage.size());
                }
                if(bytesWritten < 0)
                {
                    std::cerr << "Error to send data!" << std::endl;
                }
                else
                {
                    std::cout << "Message was send: " << newMessage << std::endl;
                }

                m_messagesQueue.erase(m_messagesQueue.begin());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    std::cout << "sender closed" << std::endl;
}


void Serial::sendMessage(std::string message)
{
    std::lock_guard<std::mutex> lock(messageMutex);
    message.append("\r");
    m_messagesQueue.push_back(message);
}