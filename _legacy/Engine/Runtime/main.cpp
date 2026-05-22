#include "Application/Application.hpp"

int main()
{
    return static_cast<int>(CD::Application::getInstance()->run());
}