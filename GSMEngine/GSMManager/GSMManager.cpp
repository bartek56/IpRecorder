#include "GSMManager.hpp"

#include <iostream>

GSMManager::GSMManager(const std::string &port) : tasks(port)
{
}

bool GSMManager::initilize()
{
    if(!setDefaultConfig())
    {
        std::cout << "Failed to set default configuration" << std::endl;
        return false;
    }
    return true;
}

bool GSMManager::sendSms(const std::string &number, const std::string &message)
{
    return tasks.addSmsTask(number, message);
}

bool GSMManager::sendSmsSync(const std::string &number, const std::string &message)
{
    return tasks.sendSms(number, message);
}

bool GSMManager::isNewSms()
{
    return tasks.isNewSms();
}

Sms GSMManager::getSms()
{
    return tasks.getLastSms();
}

bool GSMManager::setDefaultConfig()
{
    auto setConfig = [&](const std::string &command)
    {
        auto result = tasks.setConfig(command);
        if(!result)
        {
            std::cout << "failed to set config: " << command << std::endl;
            return false;
        }
        return true;
    };

    bool result = true;
    result &= setConfig("AT");
    result &= setConfig("AT+CMGF=1");
    result &= setConfig("AT+CNMI=1,2,0,0");
    result &= setConfig("AT+CLIP=1");

    return result;
}
