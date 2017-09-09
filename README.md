# protoactor-cpp

Ultra-fast, distributed, cross-platform actors.

[Proto.Actor](http://proto.actor/)

## Source code

This is the C++ repository for Proto Actor.

**(unstable/WIP)**

Other implementations:
* C#: [https://github.com/AsynkronIT/protoactor-dotnet](https://github.com/AsynkronIT/protoactor-dotnet)
* Go: [https://github.com/AsynkronIT/protoactor-go](https://github.com/AsynkronIT/protoactor-go)
* Python (unstable/WIP): [https://github.com/AsynkronIT/protoactor-python](https://github.com/AsynkronIT/protoactor-python)
* JavaScript (unstable/WIP): [https://github.com/AsynkronIT/protoactor-js](https://github.com/AsynkronIT/protoactor-js)

## Requirements

* C++14 compiler
* Boost 1.53.0
* CMake

## How to build

protoactor-cpp is header-only.

protoactor-cpp uses and requires the CMake in order to build examples.

## Design principles

**Minimalistic API** - The API should be small and easy to use. Avoid enterprisey containers and configurations.

**Build on existing technologies** - There are already a lot of great technologies for e.g. networking and clustering. Build on those instead of reinventing them. E.g. gRPC streams for networking, Consul for clustering.

**Pass data, not objects** - Serialization is an explicit concern - don't try to hide it. Protobuf all the way.

**Be fast** - Do not trade performance for magic API trickery.

## Getting started

The best place currently for learning how to use Proto.Actor is the [examples](https://github.com/whitglint/protoactor-cpp/tree/master/examples).

### Hello world

Define a message type:

```cpp
class Hello : public Message
{
public:
    Hello(const std::string &who) : who(who) {}

    std::string who;
};
```

Define an actor:

```cpp
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
```

Spawn it and send a message to it:

```cpp
auto props = Actor::from_producer([]() { return std::make_unique<HelloActor>(); });
auto pid = Actor::spawn(*props);
pid->tell<Hello>("ProtoActor");
```

You should see the output `Hello ProtoActor`.
