#ifndef PROTOACTOR_PROTOACTOR_HPP
#define PROTOACTOR_PROTOACTOR_HPP

#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <protoactor/mailbox.hpp>
#include <protoactor/types.hpp>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace protoactor
{

using namespace mailbox;

class IContext;
class PID;
class Props;

enum class ContextState
{
    None,
    Alive,
    Restarting,
    Stopping,
};

class StartedMessage : public SystemMessage
{
public:
    StartedMessage()
        : SystemMessage{true}
    {
    }

    static Message::UPtr instance()
    {
        static StartedMessage _instance;
        return Message::UPtr{&_instance};
    }
};

class IActor
{
public:
    virtual ~IActor() = default;
    virtual void receive(const IContext &context) = 0;
};

using Producer = std::function<std::unique_ptr<IActor> ()>;

class ISenderContext
{
public:
    virtual ~ISenderContext() = default;
    virtual Message::SPtr message() const = 0;
};

class IContext : public ISenderContext
{
};

class LocalContext : public IMessageInvoker, public IContext
{
public:
    LocalContext(const Producer &producer, PID *parent)
        : parent_{parent}
        , producer_{producer}
    {
        incarnate_actor();
    }

    virtual void escalate_failure(const std::exception &, const Message::SPtr &) override
    {
    }

    virtual void invoke_system_message(const Message::SPtr &message) override
    {
        if (dynamic_cast<StartedMessage *>(message.get())) {
            return invoke_user_message(message);
        }
    }

    virtual void invoke_user_message(const Message::SPtr &message) override
    {
        process_message(message);
    }

    virtual Message::SPtr message() const override
    {
        return message_;
    }

private:
    static void default_receive(IContext &context)
    {
        auto &lc = static_cast<LocalContext &>(context);
        return lc.actor_->receive(context);
    }

    void incarnate_actor()
    {
        state_ = ContextState::Alive;
        actor_ = producer_();
    }

    void process_message(const Message::SPtr &message)
    {
        message_ = message;
        default_receive(*this);
        message_.reset();
    }

    std::unique_ptr<IActor> actor_;
    Message::SPtr message_;
    PID *parent_;
    Producer producer_;
    ContextState state_{ContextState::None};
};

class StopMessage : public SystemMessage
{
public:
    StopMessage()
        : SystemMessage{true}
    {
    }

    static Message::UPtr instance()
    {
        static StopMessage _instance;
        return Message::UPtr{&_instance};
    }
};

class Process
{
public:
    virtual ~Process() = default;

    virtual void stop(PID *pid)
    {
        send_system_message(pid, StopMessage::instance());
    }

    virtual void send_system_message(PID *pid, Message::UPtr message) = 0;
    virtual void send_user_message(PID *pid, Message::UPtr message) = 0;
};

class DeadLetterProcess : public Process
{
public:
    static DeadLetterProcess &instance()
    {
        static DeadLetterProcess _instance;
        return _instance;
    }

    virtual void send_system_message(PID *, Message::UPtr) override
    {
    }

    virtual void send_user_message(PID *, Message::UPtr) override
    {
    }
};

class LocalProcess : public Process
{
public:
    LocalProcess(const std::shared_ptr<IMailbox> &mailbox)
        : mailbox_{mailbox}
    {
    }

    bool is_dead() const { return is_dead_; }

    virtual void send_system_message(PID *, Message::UPtr message) override
    {
        mailbox_->post_system_message(std::move(message));
    }

    virtual void send_user_message(PID *, Message::UPtr message) override
    {
        mailbox_->post_user_message(std::move(message));
    }

    virtual void stop(PID *pid) override
    {
        Process::stop(pid);
        is_dead_.store(true);
    }

private:
    std::shared_ptr<IMailbox> mailbox_;
    std::atomic_bool is_dead_{false};
};

class PID
{
public:
    PID(const std::string &address, const std::string &id)
        : address_(address)
        , id_(id)
    {
    }

    const std::string &address() const { return address_; }
    const std::string &id() const { return id_; }

    template <typename TMessage, typename... TArgs>
    void tell(TArgs &&...args)
    {
        tell(Message::UPtr{new TMessage(std::forward<TArgs>(args)...)});
    }

private:
    Process *ref();
    void tell(Message::UPtr message);

    std::string address_;
    std::string id_;
    Process *process_{nullptr};
};

class ProcessNameExistException : public std::invalid_argument
{
public:
    ProcessNameExistException(const std::string &name)
        : std::invalid_argument{"a Process with the name '" + name + "' already exists"}
    {
    }
};

class ProcessRegistry
{
public:
    Process &get(const PID &pid) const;

    static ProcessRegistry &instance()
    {
        static ProcessRegistry _instance;
        return _instance;
    }

    std::string next_id()
    {
        int id = ++sequence_id_;
        return '$' + std::to_string(id);
    }

    std::unique_ptr<PID> try_add(const std::string &id, std::unique_ptr<Process> process);

private:
    using LocalActorRefs = std::unordered_map<std::string, std::unique_ptr<Process>>;

    static const char *no_host() { return "nonhost"; }

    std::string address_{no_host()};
    LocalActorRefs local_actor_refs_;
    mutable std::mutex mutex_;
    std::atomic_int sequence_id_{0};
};

using MailboxProducer = std::function<std::unique_ptr<IMailbox> ()>;
using Spawner = std::function<std::unique_ptr<PID> (const std::string &id, const Props &props, PID *parent)>;

class Props
{
public:
    static std::unique_ptr<PID> default_spawner(const std::string &name, const Props &props, PID *parent)
    {
        std::shared_ptr<IMailbox> mailbox = props.mailbox_producer_();
        auto pid = ProcessRegistry::instance().try_add(name, std::make_unique<LocalProcess>(mailbox));
        auto ctx = std::make_shared<LocalContext>(props.producer(), parent);
        auto &dispatcher = props.dispatcher();
        mailbox->register_handlers(ctx, dispatcher);
        mailbox->post_system_message(StartedMessage::instance());
        mailbox->start();
        return pid;
    }

    IDispatcher &dispatcher() const { return *dispatcher_; }
    const Producer &producer() const { return producer_; }

    std::unique_ptr<PID> spawn(const std::string &name, PID *parent) const
    {
        return spawner_(name, *this, parent);
    }

    Props &with_producer(Producer &&producer)
    {
        producer_ = std::move(producer);
        return *this;
    }

private:
    static std::unique_ptr<IMailbox> produce_default_mailbox()
    {
        return UnboundedMailbox::create();
    }

    IDispatcher *dispatcher_{&Dispatchers::default_dispatcher()};
    MailboxProducer mailbox_producer_{&Props::produce_default_mailbox};
    Producer producer_;
    Spawner spawner_{&Props::default_spawner};
};

class Actor
{
public:
    static std::unique_ptr<Props> from_producer(Producer &&producer)
    {
        auto props = std::make_unique<Props>();
        props->with_producer(std::forward<Producer>(producer));
        return props;
    }

    static std::unique_ptr<PID> spawn(const Props &props)
    {
        auto name = ProcessRegistry::instance().next_id();
        return spawn_named(props, name);
    }

    static std::unique_ptr<PID> spawn_named(const Props &props, const std::string &name)
    {
        return props.spawn(name, nullptr);
    }
};

Process *PID::ref()
{
    if (process_) {
        auto lp = dynamic_cast<LocalProcess *>(process_);
        if (lp && lp->is_dead()) {
            process_ = nullptr;
        }
        return process_;
    }
    auto reff = &ProcessRegistry::instance().get(*this);
    if (!dynamic_cast<DeadLetterProcess *>(reff)) {
        process_ = reff;
    }
    return process_;
}

void PID::tell(Message::UPtr message)
{
    auto p = ref();
    auto &reff = p ? *p : ProcessRegistry::instance().get(*this);
    reff.send_user_message(this, std::move(message));
}

Process &ProcessRegistry::get(const PID &pid) const
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto iter = local_actor_refs_.find(pid.id());
    if (local_actor_refs_.end() == iter) {
        return DeadLetterProcess::instance();
    }
    return *iter->second.get();
}

std::unique_ptr<PID> ProcessRegistry::try_add(const std::string &id, std::unique_ptr<Process> process)
{
    auto pid = std::make_unique<PID>(address_, id);
    auto emplace_result = [&]() {
        std::unique_lock<std::mutex> lock(mutex_);
        return local_actor_refs_.emplace(id, std::move(process));
    }();
    if (!emplace_result.second) {
        throw ProcessNameExistException(id);
    }
    return pid;
}

} // namespace protoactor

#endif // PROTOACTOR_PROTOACTOR_HPP
