#include <iostream>
#include <memory>
#include <protoactor/protoactor.hpp>
#include <string>

using namespace protoactor;

class Hello : public Message
{
public:
    Hello(const std::string &who) : who(who) {}

    std::string who;
};

class HelloActor : public IActor
{
public:
    virtual void receive(const IContext &context) override
    {
        auto message = context.message();
        if (auto h = dynamic_cast<Hello *>(message.get())) {
            std::cout << "Hello " << h->who << std::endl;
        }
    }
};

int main()
{
    auto props = Actor::from_producer([]() { return std::make_unique<HelloActor>(); });
    auto pid = Actor::spawn(*props);
    pid->tell<Hello>("ProtoActor");
    std::cin.get();
    return 0;
}
