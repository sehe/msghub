#include "msghub.h"

#include "hubclient.h"
#include "hubconnection.h"
#include "ihub.h"

using boost::asio::ip::tcp;

namespace msghublib {

    class msghub::impl : public detail::ihub,
                         public std::enable_shared_from_this<impl> {
      public:
        using any_io_executor = boost::asio::any_io_executor;
        using hubclient = detail::hubclient;
        using hubconnection = detail::hubconnection;

      private:
        any_io_executor executor_;
        boost::asio::executor_work_guard<any_io_executor> work_ =
            make_work_guard(executor_);
        tcp::acceptor acceptor_;
        std::shared_ptr<hubconnection> remote_hub_; // using std::atomic_* accessors

        // the subscriptions are under mutex
        std::mutex mutable mutex_;
        std::map<std::string, msghub::onmessage> local_subs_;
        std::multimap<std::string, std::weak_ptr<hubclient>> remote_subs_;

      public:
        impl(boost::asio::any_io_executor executor)
            : executor_(executor)
            , acceptor_(make_strand(executor))
        {}

        void stop() {
            {
                std::shared_ptr<hubconnection> rhub;
                if (auto p = std::atomic_exchange(&remote_hub_, rhub))
                    p->close(false);
            }

            if (!weak_from_this().expired()) {
                post(acceptor_.get_executor(), [this, self = shared_from_this()] {
                    if (acceptor_.is_open())
                        acceptor_.cancel();
                });
            } else {
                if (acceptor_.is_open())
                    acceptor_.cancel();
            }

            if (0) {
                std::lock_guard lk(mutex_);
                for (auto& [_, client] : remote_subs_)
                    if (auto alive = client.lock())
                        alive->stop();
            }

            work_.reset();
        }

        ~impl() { stop(); }

        bool connect(const std::string& hostip, uint16_t port) {
            auto p = std::make_shared<hubconnection>(executor_, *this);

            if (p->init(hostip, port)) {
                atomic_store(&remote_hub_, p);
                return true;
            }

            return false;
        }

        bool create(uint16_t port) {
            try {
                acceptor_.open(tcp::v4());
                acceptor_.set_option(tcp::acceptor::reuse_address(true));
                acceptor_.bind({ {}, port });
                acceptor_.listen();

                accept_next();

                auto p = std::make_shared<hubconnection>(executor_, *this);

                if (p->init("localhost", port)) {
                    atomic_store(&remote_hub_, p);
                    return true;
                }
            } catch (boost::system::system_error const&) {
            }
            return false;
        }

        bool publish(std::string_view topic, span<char const> message) {
            if (auto p = atomic_load(&remote_hub_)) {
                return p->write({ hubmessage::action::publish, topic, message });
            }
            return false;
        }

        bool unsubscribe(const std::string& topic) {
            std::unique_lock lk(mutex_);
            if (auto it = local_subs_.find(topic); it != local_subs_.end()) {
                /*it =*/local_subs_.erase(it);
                lk.unlock();

                if (auto p = atomic_load(&remote_hub_)) {
                    return p->write({ hubmessage::action::unsubscribe, topic },
                                    true);
                }
            }
            return false;
        }

        bool subscribe(const std::string& topic, msghub::onmessage handler) {
            std::unique_lock lk(mutex_);
            if (auto [it, ins] = local_subs_.emplace(topic, handler); ins) {
                lk.unlock();
                if (auto p = atomic_load(&remote_hub_)) {
                    return p->write({ hubmessage::action::subscribe, topic }, true);
                    // TODO: wait feedback from server here?
                }
            } else {
                // just update the handler
                it->second = handler; // overwrite
                return true;
            }

            return false;
        }

      private:
        msghub::onmessage const& lookup_handler(std::string_view topic) const {
            static const msghub::onmessage no_handler = [](auto...) {};

            std::lock_guard lk(mutex_);
            auto it = local_subs_.find(std::string(topic));
            return (it == local_subs_.end()) ? no_handler : it->second;
        }

        void distribute(std::shared_ptr<hubclient> const& subscriber,
                        hubmessage const& msg) {
            std::string topic(msg.topic());
            std::unique_lock lk(mutex_);
            auto range = remote_subs_.equal_range(topic);

            switch (msg.get_action()) {
            case hubmessage::action::publish:
                for (auto it = range.first; it != range.second;) {
                    if (auto alive = it->second.lock()) {
                        alive->write(msg);
                        ++it;
                    } else
                        it = remote_subs_.erase(it);
                }
                break;

            case hubmessage::action::subscribe:
#if __cpp_lib_erase_if // allows us to write that in one go:
                std::erase_if(remote_subs_,
                              [](auto& p) { return p.second.expired(); });
#endif
                remote_subs_.emplace(topic, subscriber);
                break;

            case hubmessage::action::unsubscribe:
                for (auto it = range.first; it != range.second;) {
                    if (auto alive = it->second.lock();
                        !alive || alive == subscriber)
                        it = remote_subs_.erase(it);
                    else
                        ++it;
                }
                break;

            default:
                break;
            }
        }

        void deliver(hubmessage const& msg) {
            lookup_handler(msg.topic())(msg.topic(), msg.body());
        }

        void accept_next() {
            auto subscriber =
                std::make_shared<hubclient>(acceptor_.get_executor(), *this);

            // Schedule next accept
            acceptor_.async_accept(
                subscriber->socket(),
                [=, this, self = shared_from_this()](boost::system::error_code ec) {
                    handle_accept(subscriber, ec);
                });
        }

        void handle_accept(std::shared_ptr<hubclient> const& client,
                           const boost::system::error_code& error) {
            if (!error) {
                client->start();
                accept_next();
            } else {
                //// TODO: Handle IO error - on thread exit
                // int e = error.value();
            }
        }
    };

} // namespace msghublib

namespace msghublib {

    /*explicit*/ msghub::msghub(boost::asio::any_io_executor executor)
        : pimpl(std::make_shared<impl>(executor)) {}

    msghub::~msghub() = default;

    void msghub::stop()                                                    { return pimpl->stop();                    } 
    bool msghub::connect(const std::string& hostip, uint16_t port)         { return pimpl->connect(hostip, port);     } 
    bool msghub::create(uint16_t port)                                     { return pimpl->create(port);              } 
    bool msghub::unsubscribe(const std::string& topic)                     { return pimpl->unsubscribe(topic);        } 
    bool msghub::subscribe(const std::string& topic, onmessage handler)    { return pimpl->subscribe(topic, handler); } 
    bool msghub::publish(std::string_view topic, span<char const> message) { return pimpl->publish(topic, message);   } 

} // namespace msghublib
