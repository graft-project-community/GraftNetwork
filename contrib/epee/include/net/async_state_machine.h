#ifndef ASYNC_STATE_MACHINE_H
#define ASYNC_STATE_MACHINE_H

#include <memory>
#include <set>
#include <chrono>
#include <thread>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

namespace epee { namespace net {


struct async_callback_state_machine : public std::enable_shared_from_this<async_callback_state_machine>
{
    const int64_t infty = -1;

    struct i_task : public std::enable_shared_from_this<i_task>
    {
        virtual void exec() = 0;
        virtual ~i_task() {}

        std::weak_ptr<boost::asio::deadline_timer> timer;
    };

    std::shared_ptr<async_callback_state_machine>
    create(boost::asio::io_service& io_service,
           int64_t timeout,
           std::shared_ptr<i_task> finalizer = std::shared_ptr<i_task>())
    {
        std::shared_ptr<async_callback_state_machine> ret(new async_callback_state_machine(io_service,timeout));
        if (ret && ret->deadline_timer) {
            auto callback= boost::bind(&async_callback_state_machine::deadline_handler, _1, ret);
            ret->deadline_timer->async_wait(/**ret.get()*/callback);
            if (finalizer) ret->final_callback = finalizer;
        }

        return ret;
    }

    virtual ~async_callback_state_machine()
    {
        if (deadline_timer ) deadline_timer->cancel();
        if (final_callback) io_service.post(boost::bind(&i_task::exec, final_callback));
    }

protected:
    async_callback_state_machine(boost::asio::io_service& io_service, int64_t timeout)
        : io_service(io_service)
        , timeout_msec(timeout)
        , deadline_timer( timeout_msec > 0
                          ? new boost::asio::deadline_timer(io_service,
                                                            boost::posix_time::milliseconds(timeout))
                          : nullptr)
    {}

    static void deadline_handler(const boost::system::error_code& ec,
                                 std::shared_ptr<async_callback_state_machine>& machine)
    {
        if ( ec == boost::asio::error::operation_aborted )
            return;
        machine.reset();
    }


    boost::asio::io_service& io_service;
    int64_t timeout_msec;
    std::shared_ptr<i_task> final_callback;

    std::shared_ptr<boost::asio::deadline_timer> deadline_timer;
    std::set<std::shared_ptr<i_task> > scheduled_tasks;
    std::set<std::shared_ptr<boost::asio::deadline_timer>> active_timers;

private:
    struct weak_binder final
    {
        weak_binder(std::shared_ptr<i_task>& task
                    , std::shared_ptr<async_callback_state_machine> machine
                    = std::shared_ptr<async_callback_state_machine>() )
            : data(task)
            , machine(machine)
        {}

        void operator ()() {
            if ( std::shared_ptr<i_task> ptr = data.lock() ) {
                ptr->exec();
                if (std::shared_ptr<async_callback_state_machine> tmp =  machine.lock())
                    tmp->remove(ptr);
            }
        }
        std::weak_ptr<i_task> data;
        std::weak_ptr<async_callback_state_machine> machine;
    };

    struct timer_binder final
    {
        timer_binder(std::shared_ptr<i_task>& task,
                     std::shared_ptr<async_callback_state_machine> machine,
                     int64_t timeout_msec)
            : data(task)
            , machine(machine)
            , timer(new boost::asio::deadline_timer(machine->io_service,
                                                    boost::posix_time::milliseconds(timeout_msec)))
        {
            std::shared_ptr<i_task> tmp = this->data.lock();
            if (tmp)
                tmp->timer = timer;
        }

        void operator ()() {
            if ( std::shared_ptr<i_task> ptr = data.lock() ) {
                ptr->exec();
                if (std::shared_ptr<async_callback_state_machine> tmp =  machine.lock())
                    tmp->remove(ptr);
            }
        }

        static void timeout_handler(const boost::system::error_code& ec,
                                    std::shared_ptr<timer_binder>& binder)
        {
            if ( ec == boost::asio::error::operation_aborted )
                return;
            (*binder)();
        }

        std::weak_ptr<i_task> data;
        std::weak_ptr<async_callback_state_machine> machine;
        std::shared_ptr<boost::asio::deadline_timer> timer;
    };

    friend struct weak_binder;
    friend struct timer_binder;

    void remove(std::shared_ptr<i_task>& ptr) {
        // TODO: add mutex
        _mutex.lock();
        scheduled_tasks.erase(ptr);
        _mutex.unlock();
    }

    boost::recursive_mutex _mutex;

    struct stop_task : public i_task
    {
        stop_task();
        /*virtual*/ void exec() {
            std::shared_ptr<async_callback_state_machine> m = machine.lock();
            if (m) {
                machine.reset();
            }
        }
        std::weak_ptr<async_callback_state_machine> machine;
    };


public:
    void schedule_task(std::shared_ptr<i_task> task)
    {
        std::shared_ptr<weak_binder> wrapper(new weak_binder(task, shared_from_this()));
        _mutex.lock();
        scheduled_tasks.insert(task);
        _mutex.unlock();
        io_service.post(boost::bind(&weak_binder::operator(), wrapper));
    }

    void schedule_task(std::shared_ptr<i_task> task, int timeout)
    {
        std::shared_ptr<timer_binder> wrapper(new timer_binder(task, shared_from_this(), timeout));
        _mutex.lock();
        scheduled_tasks.insert(task);
        active_timers.insert(wrapper->timer);
        _mutex.unlock();
        wrapper->timer->async_wait(boost::bind(&timer_binder::timeout_handler, _1, wrapper));
    }

    void unschedule_task(std::weak_ptr<i_task> task)
    {
        std::shared_ptr<i_task> t = task.lock();
        if (!t)
            return;
        _mutex.lock();
        scheduled_tasks.erase(t);
        _mutex.unlock();
        std::shared_ptr<boost::asio::deadline_timer> timer = t->timer.lock();
        if (timer) {
            timer->cancel();
            _mutex.lock();
            active_timers.erase(timer);
            _mutex.unlock();
        }
    }

    void stop()
    {
        std::vector<std::shared_ptr<boost::asio::deadline_timer>> timers;
        _mutex.lock();
        scheduled_tasks.clear();
        for (auto entry : active_timers)
            timers.push_back(entry);
        active_timers.clear();
        _mutex.unlock();

        for (auto timer : timers)
            timer->cancel();

        std::shared_ptr<i_task> task(new stop_task);
        static_cast<stop_task*>(task.get())->machine = shared_from_this();

        std::shared_ptr<weak_binder> wrapper(new weak_binder(task, shared_from_this()));
        io_service.post(boost::bind(&weak_binder::operator(), wrapper));
    }
};


}} // namespace epee { namespace net {

#endif //  ASYNC_STATE_MACHINE_H
