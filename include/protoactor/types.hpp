#ifndef PROTOACTOR_TYPES_HPP
#define PROTOACTOR_TYPES_HPP

#include <memory>

namespace protoactor
{

class Message
{
public:
    class Deleter
    {
    public:
        void operator()(Message *message) const
        {
            if (message && !message->do_not_delete_) {
                delete message;
            }
        }
    };

    using SPtr = std::shared_ptr<Message>;
    using UPtr = std::unique_ptr<Message, Deleter>;

protected:
    Message(bool do_not_delete = false)
        : do_not_delete_{do_not_delete}
    {
    }

    virtual ~Message() = default;

private:
    bool do_not_delete() const { return do_not_delete_; }

    const bool do_not_delete_;
};

} // namespace protoactor

#endif // PROTOACTOR_TYPES_HPP
