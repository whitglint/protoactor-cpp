#ifndef PROTOACTOR_MAILBOX_HPP
#define PROTOACTOR_MAILBOX_HPP

#include <boost/lockfree/queue.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <protoactor/types.hpp>
#include <vector>

namespace protoactor
{
namespace mailbox
{

class IMessageInvoker;

class IDispatcher
{
public:
    virtual ~IDispatcher() = default;
    virtual void schedule(const std::function<void ()> &runner) = 0;
    virtual int throughput() const = 0;
};

class SynchronousDispatcher : public IDispatcher
{
public:
    virtual void schedule(const std::function<void ()> &runner) override
    {
        runner();
    }

    virtual int throughput() const override
    {
        return 300;
    }
};

class Dispatchers
{
public:
    static IDispatcher &default_dispatcher()
    {
        static SynchronousDispatcher _instance;
        return _instance;
    }
};

class IMailbox
{
public:
    virtual ~IMailbox() = default;
    virtual void post_system_message(Message::UPtr message) = 0;
    virtual void post_user_message(Message::UPtr message) = 0;
    virtual void register_handlers(const std::shared_ptr<IMessageInvoker> &invoker, IDispatcher &dispatcher) = 0;
    virtual void start() = 0;
};

class IMailboxQueue
{
public:
    virtual ~IMailboxQueue() = default;
    virtual bool has_messages() const = 0;
    virtual Message::UPtr pop() = 0;
    virtual void push(Message::UPtr message) = 0;
};

class IMailboxStatistics
{
public:
    virtual ~IMailboxStatistics() = default;
    virtual void mailbox_empty() = 0;
    virtual void message_posted(const Message &message) = 0;
    virtual void message_received(const Message &message) = 0;
    virtual void mailbox_started() = 0;
};

class IMessageInvoker
{
public:
    virtual ~IMessageInvoker() = default;
    virtual void escalate_failure(const std::exception &reason, const Message::SPtr &message) = 0;
    virtual void invoke_system_message(const Message::SPtr &message) = 0;
    virtual void invoke_user_message(const Message::SPtr &message) = 0;
};

enum class MailboxStatus
{
    Idle = false,
    Busy = true,
};

class SystemMessage : public Message
{
public:
    SystemMessage(bool do_not_delete = false)
        : Message{do_not_delete}
    {
    }
};

class ResumeMailboxMessage : public SystemMessage
{
};

class SuspendMailboxMessage : public SystemMessage
{
};

class DefaultMailbox : public IMailbox
{
public:
    template <typename... TMailboxStatistics>
    DefaultMailbox(std::unique_ptr<IMailboxQueue> system_messages, std::unique_ptr<IMailboxQueue> user_mailbox, TMailboxStatistics &&...stats)
        : stats_{std::forward<TMailboxStatistics>(stats)...}
        , system_messages_{std::move(system_messages)}
        , user_mailbox_{std::move(user_mailbox)}
    {
    }

    virtual void post_system_message(Message::UPtr message) override
    {
        for (auto &stat : stats_) {
            stat->message_posted(*message);
        }
        system_messages_->push(std::move(message));
        schedule();
    }

    virtual void post_user_message(Message::UPtr message) override
    {
        for (auto &stat : stats_) {
            stat->message_posted(*message);
        }
        user_mailbox_->push(std::move(message));
        schedule();
    }

    virtual void register_handlers(const std::shared_ptr<IMessageInvoker> &invoker, IDispatcher &dispatcher) override
    {
        invoker_ = invoker;
        dispatcher_ = &dispatcher;
    }

    virtual void start() override
    {
        for (auto &stat : stats_) {
            stat->mailbox_started();
        }
    }

protected:
    void schedule()
    {
        MailboxStatus expected{MailboxStatus::Idle};
        if (status_.compare_exchange_strong(expected, MailboxStatus::Busy)) {
            dispatcher_->schedule([this]() {
                run();
            });
        }
    }

private:
    using Stats = std::vector<std::unique_ptr<IMailboxStatistics>>;

    bool process_messages()
    {
        Message::SPtr message;
        try
        {
            for (auto i = 0; i < dispatcher_->throughput(); ++i) {
                message = system_messages_->pop();
                if (message) {
                    if (dynamic_cast<SuspendMailboxMessage *>(message.get())) {
                        suspended_ = true;
                    } else if (dynamic_cast<ResumeMailboxMessage *>(message.get())) {
                        suspended_ = false;
                    }
                    invoker_->invoke_system_message(message);
                    for (auto &stat : stats_) {
                        stat->message_received(*message);
                    }
                    continue;
                }
                if (suspended_) {
                    break;
                }
                message = user_mailbox_->pop();
                if (message) {
                    invoker_->invoke_user_message(message);
                    for (auto &stat : stats_) {
                        stat->message_received(*message);
                    }
                } else {
                    break;
                }
            }
        } catch (const std::exception &e) {
            invoker_->escalate_failure(e, message);
        }
        return true;
    }

    void run()
    {
        auto done = process_messages();
        if (!done) {
            return;
        }
        status_.store(MailboxStatus::Idle);
        if (system_messages_->has_messages() || (!suspended_ && user_mailbox_->has_messages())) {
            schedule();
        } else {
            for (auto &stat : stats_) {
                stat->mailbox_empty();
            }
        }
    }

    IDispatcher *dispatcher_{nullptr};
    std::shared_ptr<IMessageInvoker> invoker_;
    Stats stats_;
    std::atomic<MailboxStatus> status_{MailboxStatus::Idle};
    bool suspended_{false};
    std::unique_ptr<IMailboxQueue> system_messages_;
    std::unique_ptr<IMailboxQueue> user_mailbox_;
};

class UnboundedMailboxQueue : public IMailboxQueue
{
public:
    virtual ~UnboundedMailboxQueue()
    {
        while (!messages_.empty()) {
            pop();
        }
    }

    virtual bool has_messages() const override { return !messages_.empty(); }

    virtual Message::UPtr pop() override
    {
        Message::UPtr message;
        messages_.pop(message);
        return message;
    }

    virtual void push(Message::UPtr message) override
    {
        messages_.push(message.release());
    }

private:
    using Messages = boost::lockfree::queue<Message *>;

    Messages messages_{0};
};

class UnboundedMailbox
{
public:
    template <typename... TMailboxStatistics>
    static std::unique_ptr<IMailbox> create(TMailboxStatistics &&...stats)
    {
        return std::make_unique<DefaultMailbox>(std::make_unique<UnboundedMailboxQueue>(), std::make_unique<UnboundedMailboxQueue>(), std::forward<TMailboxStatistics>(stats)...);
    }
};

} // namespace mailbox
} // namespace protoactor

#endif // PROTOACTOR_MAILBOX_HPP
