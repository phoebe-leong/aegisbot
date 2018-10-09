//
// futures.h
// *********
//
// Copyright (c) 2018 Sharon W (sharon at aegis dot gg)
//
// Distributed under the MIT License. (See accompanying file LICENSE)
// 

#include <stdexcept>
#include <type_traits>
#include <future>
#include <memory>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include <asio.hpp>
#include <asio/strand.hpp>
#include <asio/bind_executor.hpp>
#include <aegis/core.hpp>

using namespace std::literals::chrono_literals;


namespace aegis
{



// 
// 
// template<typename T>
// struct function_traits;
// 
// template<typename Ret, typename... Args>
// struct function_traits<Ret(Args...)>
// {
//     using return_type = Ret;
//     using args_as_tuple = std::tuple<Args...>;
//     using signature = Ret(Args...);
// 
//     static constexpr std::size_t arity = sizeof...(Args);
// 
//     template <std::size_t N>
//     struct arg
//     {
//         static_assert(N < arity, "no such parameter index.");
//         using type = typename std::tuple_element<N, std::tuple<Args...>>::type;
//     };
// };
// 
// template<typename Ret, typename... Args>
// struct function_traits<Ret(*)(Args...)> : public function_traits<Ret(Args...)>
// {
// };
// 
// template <typename T, typename Ret, typename... Args>
// struct function_traits<Ret(T::*)(Args...)> : public function_traits<Ret(Args...)>
// {
// };
// 
// template <typename T, typename Ret, typename... Args>
// struct function_traits<Ret(T::*)(Args...) const> : public function_traits<Ret(Args...)>
// {
// };
// 
// template <typename T>
// struct function_traits : public function_traits<decltype(&T::operator())>
// {
// };
// 
// template<typename T>
// struct function_traits<T&> : public function_traits<std::remove_reference_t<T>>
// {
// };




















template <class T>
class promise;

template <class T>
class future;

template <typename T>
class shared_future;

template <typename T, typename... A>
future<T> make_ready_future(A&&... value);

template <typename T>
future<T> make_exception_future(std::exception_ptr value) noexcept;




template<typename T>
struct add_future
{
    using type = future<T>;
};

template<typename T>
struct add_future<future<T>>
{
    using type = future<T>;
};

template<typename T>
struct remove_future
{
    using type = T;
};

template<typename T>
struct remove_future<future<T>>
{
    using type = T;
};



template<typename T>
struct is_future : std::false_type {};

template<typename T>
struct is_future<future<T>> : std::true_type {};

template<typename F, typename... A>
struct result_of : std::result_of<F(A...)> {};

template<typename F>
struct result_of<F, void> : std::result_of<F()> {};

template<typename F, typename... A>
using result_of_t = typename result_of<F, A...>::type;


struct ready_future_marker {};
struct exception_future_marker {};

template<typename T>
using add_future_t = typename add_future<T>::type;

template<typename T>
using remove_future_t = typename remove_future<T>::type;

namespace detail
{
template<typename T, typename Func, typename State>
add_future_t<T> call_state(Func&& func, State&& state);

template<typename T, typename Func, typename Future>
add_future_t<T> call_future(Func&& func, Future&& fut) noexcept;
} // detail

template <typename T>
struct future_state
{
    using type = T;
    static constexpr bool copy_noexcept = std::is_nothrow_copy_constructible<T>::value;
    static_assert(std::is_nothrow_move_constructible<T>::value,
                  "Types must be no-throw move constructible");
    static_assert(std::is_nothrow_destructible<T>::value,
                  "Types must be no-throw destructible");
    static_assert(std::is_nothrow_copy_constructible<std::exception_ptr>::value,
                  "std::exception_ptr's copy constructor must not throw");
    static_assert(std::is_nothrow_move_constructible<std::exception_ptr>::value,
                  "std::exception_ptr's move constructor must not throw");
    enum class state
    {
        invalid,
        future,
        result,
        exception,
    } _state = state::future;
    union any
    {
        any() {}
        ~any() {}
        T value;
        std::exception_ptr ex;
    } _u;
    future_state() noexcept {}
    future_state(future_state&& x) noexcept
        : _state(x._state)
    {
        switch (_state)
        {
            case state::future:
                break;
            case state::result:
                new (&_u.value) T(std::move(x._u.value));
                x._u.value.~T();
                break;
            case state::exception:
                new (&_u.ex) std::exception_ptr(std::move(x._u.ex));
                x._u.ex.~exception_ptr();
                break;
            case state::invalid:
                break;
            default:
                abort();
        }
        x._state = state::invalid;
    }
    ~future_state() noexcept
    {
        switch (_state)
        {
            case state::invalid:
                break;
            case state::future:
                break;
            case state::result:
                _u.value.~T();
                break;
            case state::exception:
                _u.ex.~exception_ptr();
                break;
            default:
                abort();
        }
    }
    future_state& operator=(future_state&& x) noexcept
    {
        if (this != &x)
        {
            this->~future_state();
            new (this) future_state(std::move(x));
        }
        return *this;
    }
    bool available() const noexcept
    {
        return _state == state::result || _state == state::exception;
    }
    bool failed() const noexcept
    {
        return _state == state::exception;
    }
    void wait();
    void set(const T& value) noexcept
    {
        assert(_state == state::future);
        new (&_u.value) T(value);
        _state = state::result;
    }
    void set(T&& value) noexcept
    {
        assert(_state == state::future);
        new (&_u.value) T(std::move(value));
        _state = state::result;
    }
    template <typename... A>
    void set(A&&... a)
    {
        assert(_state == state::future);
        new (&_u.value) T(std::forward<A>(a)...);
        _state = state::result;
    }
    void set_exception(std::exception_ptr ex) noexcept
    {
        assert(_state == state::future);
        new (&_u.ex) std::exception_ptr(ex);
        _state = state::exception;
    }
    std::exception_ptr get_exception() && noexcept
    {
        assert(_state == state::exception);
        // Move ex out so future::~future() knows we've handled it
        _state = state::invalid;
        auto ex = std::move(_u.ex);
        _u.ex.~exception_ptr();
        return ex;
    }
    std::exception_ptr get_exception() const& noexcept
    {
        assert(_state == state::exception);
        return _u.ex;
    }
    auto get_value() && noexcept
    {
        assert(_state == state::result);
        return std::move(_u.value);
    }
    //template<typename U = T, typename = std::enable_if_t<std::is_copy_constructible<U>::value>>
    //std::enable_if_t<std::is_copy_constructible<U>::value, U> get_value() const& noexcept(copy_noexcept)
    template<typename U = T>
    std::enable_if_t<std::is_copy_constructible<U>::value, U> get_value() const& noexcept(copy_noexcept)
    {
        assert(_state == state::result);
        return _u.value;
    }
    T get() &&
    {
        assert(_state != state::future);
        //assert(available());
        if (_state == state::exception)
        {
            _state = state::invalid;
            auto ex = std::move(_u.ex);
            _u.ex.~exception_ptr();
            // Move ex out so future::~future() knows we've handled it
            std::rethrow_exception(std::move(ex));
        }
        return std::move(_u.value);
    }
    T get() const&
    {
        assert(_state != state::future);
        //assert(available());
        if (_state == state::exception)
        {
            std::rethrow_exception(_u.ex);
        }
        return _u.value;
    }
    void ignore() noexcept
    {
        assert(_state != state::future);
        this->~future_state();
        _state = state::invalid;
    }
    void forward_to(promise<T>& pr) noexcept
    {
        assert(_state != state::future);
        if (_state == state::exception)
        {
            pr.set_urgent_exception(std::move(_u.ex));
            _u.ex.~exception_ptr();
        }
        else
        {
            pr.set_urgent_value(std::move(_u.value));
            _u.value.~T();
        }
        _state = state::invalid;
    }
};

template <>
struct future_state<void>
{
    using type = void;
    //static_assert(sizeof(std::exception_ptr) == sizeof(void*), "exception_ptr not a pointer");
    static_assert(std::is_nothrow_copy_constructible<std::exception_ptr>::value,
                  "std::exception_ptr's copy constructor must not throw");
    static_assert(std::is_nothrow_move_constructible<std::exception_ptr>::value,
                  "std::exception_ptr's move constructor must not throw");
    static constexpr bool copy_noexcept = true;
    enum class state : uintptr_t
    {
        invalid = 0,
        future = 1,
        result = 2,
        exception_min = 3,  // or anything greater
    };
    union any
    {
        any() { st = state::future; }
        ~any() {}
        state st;
        std::exception_ptr ex;
    } _u;
    future_state() noexcept {}
    future_state(future_state&& x) noexcept
    {
        if (x._u.st < state::exception_min)
        {
            _u.st = x._u.st;
        }
        else
        {
            // Move ex out so future::~future() knows we've handled it
            // Moving it will reset us to invalid state
            new (&_u.ex) std::exception_ptr(std::move(x._u.ex));
            x._u.ex.~exception_ptr();
        }
        x._u.st = state::invalid;
    }
    ~future_state() noexcept
    {
        if (_u.st >= state::exception_min)
        {
            _u.ex.~exception_ptr();
        }
    }
    future_state& operator=(future_state&& x) noexcept
    {
        if (this != &x)
        {
            this->~future_state();
            new (this) future_state(std::move(x));
        }
        return *this;
    }
    bool available() const noexcept
    {
        return _u.st == state::result || _u.st >= state::exception_min;
    }
    bool failed() const noexcept
    {
        return _u.st >= state::exception_min;
    }
    void set()
    {
        assert(_u.st == state::future);
        _u.st = state::result;
    }
    void set_exception(std::exception_ptr ex) noexcept
    {
        assert(_u.st == state::future);
        new (&_u.ex) std::exception_ptr(ex);
        assert(_u.st >= state::exception_min);
    }
    void get() &&
    {
        assert(available());
        if (_u.st >= state::exception_min)
        {
            // Move ex out so future::~future() knows we've handled it
            // Moving it will reset us to invalid state
            std::rethrow_exception(std::move(_u.ex));
        }
    }
    void get() const&
    {
        assert(available());
        if (_u.st >= state::exception_min)
        {
            std::rethrow_exception(_u.ex);
        }
    }
    void ignore() noexcept
    {
        assert(available());
        this->~future_state();
        _u.st = state::invalid;
    }
    std::exception_ptr get_exception() && noexcept
    {
        assert(_u.st >= state::exception_min);
        // Move ex out so future::~future() knows we've handled it
        // Moving it will reset us to invalid state
        return std::move(_u.ex);
    }
    std::exception_ptr get_exception() const& noexcept
    {
        assert(_u.st >= state::exception_min);
        return _u.ex;
    }
    void get_value() const noexcept
    {
        assert(_u.st == state::result);
    }

    //template<typename Promise>
    void forward_to(promise<void>& pr) noexcept;
};

class task
{
public:
    virtual ~task() = default;
    virtual void run() noexcept = 0;
    virtual void run_and_dispose() noexcept = 0;
};

template <typename T>
class continuation_base : public task
{
protected:
    future_state<T> _state;
    using future_type = future<T>;
    using promise_type = promise<T>;
public:
    continuation_base() = default;
    explicit continuation_base(future_state<T>&& state) : _state(std::move(state)) {}

    void set_state(T&& state)
    {
        _state.set(std::move(state));
    }
    void set_state(future_state<T>&& state)
    {
        _state = std::move(state);
    }
    future_state<T>* state() noexcept
    {
        return &_state;
    }

    friend class promise<T>;
    friend class future<T>;
};

template <>
class continuation_base<void> : public task
{
protected:
    future_state<void> _state;
    using future_type = future<void>;
    using promise_type = promise<void>;
public:
    continuation_base() = default;
    explicit continuation_base(future_state<void>&& state) : _state(std::move(state)) {}

//     void set_state(T&& state)
//     {
//         _state.set(std::move(state));
//     }
    void set_state(future_state<void>&& state)
    {
        _state = std::move(state);
    }
    future_state<void>* state() noexcept
    {
        return &_state;
    }

    friend class promise<void>;
    friend class future<void>;
};

template <typename Func, typename T>
struct continuation final : continuation_base<T>
{
    continuation(Func&& func, future_state<T>&& state) : continuation_base<T>(std::move(state)), _func(std::move(func)) {}
    continuation(Func&& func) : _func(std::move(func)) {}
    virtual void run_and_dispose() noexcept override
    {
        _func(std::move(this->_state));
        delete this;
    }
        //TODO
    virtual void run() noexcept override
    {
        _func(std::move(this->_state));
    }
//     future_state<T>* state() noexcept
//     {
//         return &_state;
//     }
    Func _func;
    //future_state<T> _state;
};
using task_ptr = std::unique_ptr<task>;


template <typename T>
class promise
{
    enum class urgent { no, yes };
    future<T>* _future = nullptr;
    future_state<T> _local_state;
    future_state<T>* _state;
    std::unique_ptr<continuation_base<T>> _task;
    static constexpr bool copy_noexcept = future_state<T>::copy_noexcept;
public:
    /// \brief Constructs an empty \c promise.
    ///
    /// Creates promise with no associated future yet (see get_future()).
    promise() noexcept : _state(&_local_state) {}

    /// \brief Moves a \c promise object.
    promise(promise&& x) noexcept : _future(x._future), _state(x._state), _task(std::move(x._task))
    {
        if (_state == &x._local_state)
        {
            _state = &_local_state;
            _local_state = std::move(x._local_state);
        }
        x._future = nullptr;
        x._state = nullptr;
        migrated();
    }
    promise(const promise&) = delete;
    ~promise() noexcept
    {
        abandoned();
    }
    promise& operator=(promise&& x) noexcept
    {
        if (this != &x)
        {
            this->~promise();
            new (this) promise(std::move(x));
        }
        return *this;
    }
    void operator=(const promise&) = delete;

    /// \brief Gets the promise's associated future.
    ///
    /// The future and promise will be remember each other, even if either or
    /// both are moved.  When \c set_value() or \c set_exception() are called
    /// on the promise, the future will be become ready, and if a continuation
    /// was attached to the future, it will run.
    future<T> get_future() noexcept;

    /// \brief Sets the promises value (variadic)
    ///
    /// Forwards the arguments and makes them available to the associated
    /// future.  May be called either before or after \c get_future().
    template <typename... A>
    void set_value(A&&... a) noexcept
    {
        assert(_state);
        _state->set(std::forward<A>(a)...);
        make_ready<urgent::no>();
    }

    /// \brief Marks the promise as failed
    ///
    /// Forwards the exception argument to the future and makes it
    /// available.  May be called either before or after \c get_future().
    void set_exception(std::exception_ptr ex) noexcept
    {
        do_set_exception<urgent::no>(std::move(ex));
    }

    /// \brief Marks the promise as failed
    ///
    /// Forwards the exception argument to the future and makes it
    /// available.  May be called either before or after \c get_future().
    template<typename Exception>
    void set_exception(Exception&& e) noexcept
    {
        set_exception(make_exception_ptr(std::forward<Exception>(e)));
    }
private:
    //     template<urgent Urgent>
    //     void do_set_value(T result) noexcept
    //     {
    //         assert(_state);
    //         _state->set(std::move(result));
    //         make_ready<Urgent>();
    //     }

    //     void set_urgent_value(const T& result) noexcept(copy_noexcept)
    //     {
    //         set_value(std::forward<T>(result));
    //         //do_set_value<urgent::yes>(result);
    //     }

    template<typename... A>
    void set_urgent_value(A&&... a) noexcept
    {
        set_value(std::forward<A>(a)...);
        //do_set_value<urgent::yes>(std::move(result));
    }

    template<urgent Urgent>
    void do_set_exception(std::exception_ptr ex) noexcept
    {
        assert(_state);
        _state->set_exception(std::move(ex));
        make_ready<Urgent>();
    }

    void set_urgent_exception(std::exception_ptr ex) noexcept
    {
        do_set_exception<urgent::yes>(std::move(ex));
    }
private:
    template <typename Func>
    void schedule(Func&& func)
    {
        _task = std::make_unique<continuation<Func, T>>(std::move(func));
        _state = &static_cast<continuation<Func, T>*>(&*_task)->_state;

        //         auto tws = std::make_unique<continuation<Func, T>>(std::move(func));
        //         _state = &tws->_state;
        //         _task = std::move(tws);
    }
//     void schedule(std::unique_ptr<continuation_base<T>> callback)
//     {
//         _state = &callback->_state;
//         _task = std::move(callback);
//     }
    template<urgent Urgent>
    void make_ready() noexcept;
    void migrated() noexcept;
    void abandoned() noexcept;

    template <typename U>
    friend class future;

    friend struct future_state<T>;
};

/// \brief Specialization of \c promise<void>
///
/// This is an alias for \c promise<>, for generic programming purposes.
/// For example, You may have a \c promise<T> where \c T can legally be
/// \c void.
// template<>
// class promise<void> : public promise<> {};

template <typename T>
class future
{
    promise<T>* _promise;
    future_state<T> _local_state;  // valid if !_promise
    //static constexpr bool copy_noexcept = future_state<T>::copy_noexcept;
private:
    future(promise<T>* pr) noexcept : _promise(pr)
    {
        _promise->_future = this;
    }
    template <typename... A>
    future(ready_future_marker, A&&... a) : _promise(nullptr)
    {
        _local_state.set(std::forward<A>(a)...);
    }
    future(exception_future_marker, std::exception_ptr ex) noexcept : _promise(nullptr)
    {
        _local_state.set_exception(std::move(ex));
    }
    explicit future(future_state<T>&& state) noexcept
        : _promise(nullptr), _local_state(std::move(state))
    {
    }
    future_state<T>* state() noexcept
    {
        return (_promise && _promise->_state) ? _promise->_state : &_local_state;
        //return _promise ? _promise->_state : &_local_state;
    }
    const future_state<T>* state() const noexcept
    {
        return (_promise && _promise->_state) ? _promise->_state : &_local_state;
        //return _promise ? _promise->_state : &_local_state;
    }
    template <typename Func>
    void schedule(Func&& func)
    {
        //std::cout << "Schedule from thread: " << std::this_thread::get_id() << '\n';
        auto t_st = state();
        if (t_st->available())
        {
            //::seastar::schedule(std::make_unique<continuation<Func, T...>>(std::move(func), std::move(*state())));
            //auto tws = std::make_unique<continuation<Func, T>>(std::move(func));
            //asio::post(*aegis::internal::_io_context, [func = std::move(func), state = std::move(*t_st)]() mutable
            {
                //func(state);
                func(std::move(*t_st));
            }//);
        }
        else
        {
            assert(_promise);
            _promise->schedule(std::move(func));
            _promise->_future = nullptr;
            _promise = nullptr;
        }
    }

    future_state<T> get_available_state() noexcept
    {
        auto st = state();
        if (_promise)
        {
            _promise->_future = nullptr;
            _promise = nullptr;
        }
        return std::move(*st);
    }

    future<T> rethrow_with_nested()
    {
        if (!failed())
        {
            return make_exception_future<T>(std::current_exception());
        }
        else
        {
            //
            // Encapsulate the current exception into the
            // std::nested_exception because the current libstdc++
            // implementation has a bug requiring the value of a
            // std::throw_with_nested() parameter to be of a polymorphic
            // type.
            //
            std::nested_exception f_ex;
            try
            {
                get();
            }
            catch (...)
            {
                std::throw_with_nested(f_ex);
            }
            //__builtin_unreachable();
            throw std::runtime_error("unreachable");
        }
    }

    template<typename U>
    friend class shared_future;
public:
    /// \brief The data type carried by the future.
    using value_type = T;
    /// \brief The data type carried by the future.
    using promise_type = promise<T>;
    /// \brief Moves the future into a new object.
    future(future&& x) noexcept : _promise(x._promise)
    {
        if (!_promise)
        {
            _local_state = std::move(x._local_state);
        }
        x._promise = nullptr;
        if (_promise)
        {
            _promise->_future = this;
        }
    }
    future(const future&) = delete;
    future& operator=(future&& x) noexcept
    {
        if (this != &x)
        {
            this->~future();
            new (this) future(std::move(x));
        }
        return *this;
    }
    void operator=(const future&) = delete;
    ~future()
    {
        if (_promise)
        {
            _promise->_future = nullptr;
        }
        if (failed())
        {
            //report_failed_future(state()->get_exception());
        }
    }
    /// \brief gets the value returned by the computation
    ///
    /// Requires that the future be available.  If the value
    /// was computed successfully, it is returned (as an
    /// \c std::tuple).  Otherwise, an exception is thrown.
    ///
    /// If get() is called in a \ref seastar::thread context,
    /// then it need not be available; instead, the thread will
    /// be paused until the future becomes available.
    auto get()
    {
        if (!state()->available())
        {
            do_wait();
        }
        //         else if (thread_impl::get() && thread_impl::should_yield())
        //         {
        //             thread_impl::yield();
        //         }
        return get_available_state().get();
    }

    std::exception_ptr get_exception()
    {
        return get_available_state().get_exception();
    }

    /// Wait for the future to be available (in a seastar::thread)
    ///
    /// When called from a seastar::thread, this function blocks the
    /// thread until the future is available. Other threads and
    /// continuations continue to execute; only the thread is blocked.
    void wait() noexcept
    {
        std::cout << "Waiting from thread: " << std::this_thread::get_id() << '\n';
        if (!state()->available())
        {
            do_wait();
        }
    }
private:
    class thread_wake_task final : public task
    {
        //         thread_context* _thread;
        //         future* _waiting_for;
        //     public:
        //         thread_wake_task(thread_context* thread, future* waiting_for)
        //             : _thread(thread), _waiting_for(waiting_for)
        //         {
        //         }
        //         virtual void run_and_dispose() noexcept override
        //         {
        //             *_waiting_for->state() = std::move(this->_state);
        //             //thread_impl::switch_in(_thread);
        //             // no need to delete, since this is always allocated on
        //             // _thread's stack.
        //         }
    };
    void do_wait() noexcept
    {
        // fake wait
        // maybe execute something in the asio queue?

        while (!available())
        {
            std::this_thread::sleep_for(std::chrono::nanoseconds(1));
        }

        //         auto thread = thread_impl::get();
        //         assert(thread);
        //         thread_wake_task wake_task{ thread, this };
        //         _promise->schedule(std::unique_ptr<continuation_base<T>>(&wake_task));
        //         _promise->_future = nullptr;
        //         _promise = nullptr;
        //         thread_impl::switch_out(thread);
    }

public:
    /// \brief Checks whether the future is available.
    ///
    /// \return \c true if the future has a value, or has failed.
    bool available() const noexcept
    {
        auto t_st = state();
        //assert(state());
        assert(t_st);
        return t_st->available();
    }

    /// \brief Checks whether the future has failed.
    ///
    /// \return \c true if the future is available and has failed.
    bool failed() noexcept
    {
        return state()->failed();
    }

    /// \brief Schedule a block of code to run when the future is ready.
    ///
    /// Schedules a function (often a lambda) to run when the future becomes
    /// available.  The function is called with the result of this future's
    /// computation as parameters.  The return value of the function becomes
    /// the return value of then(), itself as a future; this allows then()
    /// calls to be chained.
    ///
    /// If the future failed, the function is not called, and the exception
    /// is propagated into the return value of then().
    ///
    /// \param func - function to be called when the future becomes available,
    ///               unless it has failed.
    /// \return a \c future representing the return value of \c func, applied
    ///         to the eventual value of this future.
    //template <typename Func, typename Result = std::result_of_t<Func(T)>>
    template <typename Func, typename Result = result_of_t<Func, T>>
    add_future_t<Result> then(Func&& func) noexcept
    {
        //std::cout << "Then from thread: " << std::this_thread::get_id() << '\n';
        //using futurator = futurize<std::result_of_t<Func(T&&)>>;
        using inner_type = remove_future_t<Result>;
        if (available()/* && !need_preempt()*/)
        {
            if (failed())
            {
                return make_exception_future<inner_type>(get_available_state().get_exception());
            }
            else
            {
                return detail::call_state<inner_type>(std::forward<Func>(func), get_available_state());
            }
        }
        promise<inner_type> pr;
        auto fut = pr.get_future();
        try
        {
            this->schedule([pr = std::move(pr), func = std::forward<Func>(func)](auto&& state) mutable {
                if (state.failed())
                {
                    pr.set_exception(std::move(state).get_exception());
                }
                else
                {
                    detail::call_state<inner_type>(std::forward<Func>(func), std::move(state)).forward_to(std::move(pr));
                }
            });
        }
        catch (...)
        {
            // catch possible std::bad_alloc in schedule() above
            // nothing can be done about it, we cannot break future chain by returning
            // ready future while 'this' future is not ready
            abort();
        }
        return fut;
    }


    /// \brief Schedule a block of code to run when the future is ready, allowing
    ///        for exception handling.
    ///
    /// Schedules a function (often a lambda) to run when the future becomes
    /// available.  The function is called with the this future as a parameter;
    /// it will be in an available state.  The return value of the function becomes
    /// the return value of then_wrapped(), itself as a future; this allows
    /// then_wrapped() calls to be chained.
    ///
    /// Unlike then(), the function will be called for both value and exceptional
    /// futures.
    ///
    /// \param func - function to be called when the future becomes available,
    /// \return a \c future representing the return value of \c func, applied
    ///         to the eventual value of this future.
    template <typename Func, typename Result = std::result_of_t<Func(future)>>
    add_future_t<Result> then_wrapped(Func&& func) noexcept
    {
        //using futurator = futurize<std::result_of_t<Func(future)>>;
        using inner_type = remove_future_t<Result>;
        if (available()/* && !need_preempt()*/)
        {
            return detail::call_future<inner_type>(std::forward<Func>(func), future(get_available_state()));
        }
        //typename futurator::promise_type pr;
        promise<inner_type> pr;
        auto fut = pr.get_future();
        try
        {
            //memory::disable_failure_guard dfg;
            this->schedule([pr = std::move(pr), func = std::forward<Func>(func)](auto&& state) mutable {
                detail::call_future<inner_type>(std::forward<Func>(func), future(std::move(state))).forward_to(std::move(pr));
            });
        }
        catch (...)
        {
            // catch possible std::bad_alloc in schedule() above
            // nothing can be done about it, we cannot break future chain by returning
            // ready future while 'this' future is not ready
            abort();
        }
        return fut;
    }

    /// \brief Satisfy some \ref promise object with this future as a result.
    ///
    /// Arranges so that when this future is resolve, it will be used to
    /// satisfy an unrelated promise.  This is similar to scheduling a
    /// continuation that moves the result of this future into the promise
    /// (using promise::set_value() or promise::set_exception(), except
    /// that it is more efficient.
    ///
    /// \param pr a promise that will be fulfilled with the results of this
    /// future.
    void forward_to(promise<T>&& pr) noexcept
    {
        if (state()->available())
        {
            state()->forward_to(pr);
        }
        else
        {
            _promise->_future = nullptr;
            *_promise = std::move(pr);
            _promise = nullptr;
        }
    }



    /**
     * Finally continuation for statements that require waiting for the result.
     * I.e. you need to "finally" call a function that returns a possibly
     * unavailable future. The returned future will be "waited for", any
     * exception generated will be propagated, but the return value is ignored.
     * I.e. the original return value (the future upon which you are making this
     * call) will be preserved.
     *
     * If the original return value or the callback return value is an
     * exceptional future it will be propagated.
     *
     * If both of them are exceptional - the std::nested_exception exception
     * with the callback exception on top and the original future exception
     * nested will be propagated.
     */
    template <typename Func>
    future<T> finally(Func&& func) noexcept
    {
        return then_wrapped(finally_body<Func, is_future<std::result_of_t<Func()>>::value>(std::forward<Func>(func)));
    }


    template <typename Func, bool FuncReturnsFuture>
    struct finally_body;

    template <typename Func>
    struct finally_body<Func, true>
    {
        Func _func;

        using inner_type = remove_future_t<std::result_of_t<Func(T)>>;

        finally_body(Func&& func) : _func(std::forward<Func>(func))
        {
        }

        future<T> operator()(future<T>&& result)
        {
            //using futurator = futurize<std::result_of_t<Func()>>;
            return detail::call_state<inner_type>(_func).then_wrapped([result = std::move(result)](auto f_res) mutable {
                if (!f_res.failed())
                {
                    return std::move(result);
                }
                else
                {
                    try
                    {
                        f_res.get();
                    }
                    catch (...)
                    {
                        return result.rethrow_with_nested();
                    }
                    //__builtin_unreachable();
                }
            });
        }
    };

    template <typename Func>
    struct finally_body<Func, false>
    {
        Func _func;

        finally_body(Func&& func) : _func(std::forward<Func>(func))
        {
        }

        future<T> operator()(future<T>&& result)
        {
            try
            {
                _func();
                return std::move(result);
            }
            catch (...)
            {
                return result.rethrow_with_nested();
            }
        };
    };

    /// \brief Terminate the program if this future fails.
    ///
    /// Terminates the entire program is this future resolves
    /// to an exception.  Use with caution.
//     future<void> or_terminate() noexcept
//     {
//         return then_wrapped([](auto&& f)
//         {
//             try
//             {
//                 f.get();
//             }
//             catch (...)
//             {
//                 //engine_exit(std::current_exception());
//             }
//         });
//     }

    /// \brief Discards the value carried by this future.
    ///
    /// Converts the future into a no-value \c future<>, by
    /// ignoring any result.  Exceptions are propagated unchanged.
//     future<void> discard_result() noexcept
//     {
//         return then([](T&&) {});
//     }

    /// \brief Handle the exception carried by this future.
    ///
    /// When the future resolves, if it resolves with an exception,
    /// handle_exception(func) replaces the exception with the value
    /// returned by func. The exception is passed (as a std::exception_ptr)
    /// as a parameter to func; func may return the replacement value
    /// immediately (T or std::tuple<T...>) or in the future (future<T...>)
    /// and is even allowed to return (or throw) its own exception.
    ///
    /// The idiom fut.discard_result().handle_exception(...) can be used
    /// to handle an exception (if there is one) without caring about the
    /// successful value; Because handle_exception() is used here on a
    /// future<>, the handler function does not need to return anything.
    //template <typename Func, typename Result = result_of_t<Func, T>>
    template <typename Func>
    /* Broken?
    GCC6_CONCEPT( requires ::seastar::ApplyReturns<Func, future<T...>, std::exception_ptr>
                    || (sizeof...(T) == 0 && ::seastar::ApplyReturns<Func, void, std::exception_ptr>)
                    || (sizeof...(T) == 1 && ::seastar::ApplyReturns<Func, T..., std::exception_ptr>)
    ) */
    future<T> handle_exception(Func&& func) noexcept
    {
        //using Result = result_of_t<Func, future>;
        //using inner_type = remove_future_t<Result>;
        //using func_ret = typename std::invoke_result_t<Func, std::exception_ptr>;
        using func_ret = std::result_of_t<Func(std::exception_ptr)>;
        return then_wrapped([func = std::forward<Func>(func)]
        (auto&& fut) mutable->future<T> {
            if (!fut.failed())
            {
                return make_ready_future<T>(fut.get());
            }
            else
            {
                //return futurize<func_ret>::apply(func, fut.get_exception());
                return detail::call_future<func_ret>(func, fut.get_exception());
            }
        });
    }

    /// \brief Handle the exception of a certain type carried by this future.
    ///
    /// When the future resolves, if it resolves with an exception of a type that
    /// provided callback receives as a parameter, handle_exception(func) replaces
    /// the exception with the value returned by func. The exception is passed (by
    /// reference) as a parameter to func; func may return the replacement value
    /// immediately (T or std::tuple<T...>) or in the future (future<T...>)
    /// and is even allowed to return (or throw) its own exception.
    /// If exception, that future holds, does not match func parameter type
    /// it is propagated as is.
//     template <typename Func>
//     future<T> handle_exception_type(Func&& func) noexcept
//     {
//         using trait = function_traits<Func>;
//         static_assert(trait::arity == 1, "func can take only one parameter");
//         using ex_type = typename trait::template arg<0>::type;
//         using func_ret = typename trait::return_type;
//         return then_wrapped([func = std::forward<Func>(func)]
//         (auto&& fut) mutable->future<T> {
//             try
//             {
//                 return make_ready_future<T>(fut.get());
//             }
//             catch (ex_type& ex)
//             {
//                 return futurize<func_ret>::apply(func, ex);
//             }
//         });
//     }

    /// \brief Ignore any result hold by this future
    ///
    /// Ignore any result (value or exception) hold by this future.
    /// Use with caution since usually ignoring exception is not what
    /// you want
    void ignore_ready_future() noexcept
    {
        state()->ignore();
    }

private:

    /// \cond internal
    template <typename U>
    friend class promise;
    template <typename U, typename... A>
    friend future<U> make_ready_future(A&&... value);
    template <typename U>
    friend future<U> make_exception_future(std::exception_ptr ex) noexcept;
    /// \endcond
};

// inline void future_state<void>::forward_to(promise<void>& pr) noexcept
// {
//     assert(_u.st != state::future && _u.st != state::invalid);
//     if (_u.st >= state::exception_min)
//     {
//         pr.set_urgent_exception(std::move(_u.ex));
//         _u.ex.~exception_ptr();
//     }
//     else
//     {
//         pr.set_urgent_value();
//     }
//     _u.st = state::invalid;
// }

template <typename T>
inline future<T> promise<T>::get_future() noexcept
{
    assert(!_future && _state && !_task);
    return future<T>(this);
}

template <typename T>
template<typename promise<T>::urgent Urgent>
inline void promise<T>::make_ready() noexcept
{
    if (_task)
    {
        _state = nullptr;
        if (Urgent == urgent::yes/* && !need_preempt()*/)
        {
            //::seastar::schedule_urgent(std::move(_task));
            _task->run();
        }
        else
        {
            //asio::post(*aegis::internal::_io_context, [_task = std::move(_task)]
            {
                _task->run();
            }//);
            //::seastar::schedule(std::move(_task));
        }
    }
}

template <typename T>
inline void promise<T>::migrated() noexcept
{
    if (_future)
    {
        _future->_promise = this;
    }
}

template <typename T>
inline void promise<T>::abandoned() noexcept
{
    if (_future)
    {
        assert(_state);
        assert(_state->available() || !_task);
        _future->_local_state = std::move(*_state);
        _future->_promise = nullptr;
    }
    else if (_state && _state->failed())
    {
        //report_failed_future(_state->get_exception());
    }
}

template <typename T, typename... A>
inline future<T> make_ready_future(A&&... value)
{
    return { ready_future_marker(), std::forward<A>(value)... };
}

template <typename T>
inline future<T> make_exception_future(std::exception_ptr ex) noexcept
{
    return { exception_future_marker(), std::move(ex) };
}


namespace detail
{
template<typename T>
inline add_future_t<T> to_future(T&& value)
{
    return make_ready_future<T>(std::forward<T>(value));
}

template<typename T>
inline future<T> to_future(future<T>&& fut)
{
    return std::move(fut);
}

template<typename T, typename Func, typename... A, typename Ret = std::result_of_t<Func(A&&...)>>
inline std::enable_if_t<is_future<Ret>::value, add_future_t<Ret>> call_function(std::true_type, Func&& func, A&&... args)
{
    try
    {
        return func(std::forward<A>(args)...);
    }
    catch (...)
    {
        return make_exception_future<T>(std::current_exception());
    }
}

template<typename T, typename Func, typename... A, typename Ret = std::result_of_t<Func(A&&...)>>
inline std::enable_if_t<!is_future<Ret>::value, add_future_t<T>> call_function(std::true_type, Func&& func, A&&... args)
{
    try
    {
        func(std::forward<A>(args)...);
        return make_ready_future<T>();
    }
    catch (...)
    {
        return make_exception_future<T>(std::current_exception());
    }
}

template<typename T, typename Func, typename... A>
inline add_future_t<T> call_function(std::false_type, Func&& func, A&&... args)
{
    try
    {
        return to_future(func(std::forward<A>(args)...));
    }
    catch (...)
    {
        return make_exception_future<T>(std::current_exception());
    }
}


// the tag in the function below denotes if typename State::type is void

template<typename T, typename Func, typename State>
inline add_future_t<T> call_from_state(std::true_type, Func&& func, State&&)
{
    return call_function<T>(std::is_void<T>{}, std::forward<Func>(func));
}

template<typename T, typename Func, typename State>
inline add_future_t<T> call_from_state(std::false_type, Func&& func, State&& state)
{
    return call_function<T>(std::is_void<T>{}, std::forward<Func>(func), std::forward<State>(state).get_value());
}

template<typename T, typename Func, typename State>
inline add_future_t<T> call_state(Func&& func, State&& state)
{
    return call_from_state<T>(std::is_void<typename State::type>{}, std::forward<Func>(func), std::forward<State>(state));
}

template<typename T, typename Func, typename Future>
inline add_future_t<T> call_future(Func&& func, Future&& fut) noexcept
{
    return call_function<T>(std::is_void<T>{}, std::forward<Func>(func), std::forward<Future>(fut));
}
} // detail

inline void future_state<void>::forward_to(promise<void>& pr) noexcept
{
    assert(available());
    if (_u.st == state::exception_min)
    {
        pr.set_urgent_exception(std::move(_u.ex));
    }
    else
    {
        pr.set_urgent_value();
    }
    _u.st = state::invalid;
}


template<typename Duration>
inline aegis::future<void> sleep(const Duration& dur)
{
    aegis::promise<void> pr;
    auto fut = pr.get_future();
    std::thread t1([pr = std::move(pr), dur = dur]() mutable {
        std::this_thread::sleep_for(dur);
        pr.set_value();
    });
    t1.detach();
    return fut;
}

template<typename T, typename V = std::result_of_t<T()>, typename = std::enable_if_t<!std::is_void<V>::value>>
aegis::future<V> async(T f)
{
    aegis::promise<V> pr;
    auto fut = pr.get_future();

    aegis::promise<void> pc;
    auto fut_c = pc.get_future();

    asio::post(*aegis::internal::_io_context, [pc = std::move(pc), pr = std::move(pr), f = std::move(f)]() mutable
    {
        pc.set_value();
        pr.set_value(f());
    });
    fut_c.get();
    return fut;
}

template<typename T, typename V = std::enable_if_t<std::is_void<std::result_of_t<T()>>::value>>
aegis::future<V> async(T f)
{
    aegis::promise<V> pr;
    auto fut = pr.get_future();

    aegis::promise<void> pc;
    auto fut_c = pc.get_future();

    asio::post(*aegis::internal::_io_context, [pc = std::move(pc), pr = std::move(pr), f = std::move(f)]() mutable
    {
        pc.set_value();
        f();
        pr.set_value();
    });
    fut_c.get();
    return fut;
}

/*
template<typename E, typename T, typename V = std::result_of_t<T()>>
aegis::future<V> async(E ex, T f)
{
    aegis::promise<V> pr;
    auto fut = pr.get_future();

    asio::post(*aegis::internal::_io_context, asio::bind_executor(*ex, [pr = std::move(pr), f = std::move(f)]() mutable
    {
        pr.set_value(f());
    }));
    return fut;
}

template<typename E, typename T>
aegis::future<void> async(E ex, T f)
{
    aegis::promise<void> pr;
    auto fut = pr.get_future();

    asio::post(*aegis::internal::_io_context, asio::bind_executor(*ex, [pr = std::move(pr), f = std::move(f)]() mutable
    {
        f();
        pr.set_value();
    }));
    return fut;
}*/

// template<typename V, typename E, typename T, typename std::enable_if_t<std::is_void<std::result_of_t<T()>>::value> = 0>
// aegis::future<V> async(E ex, T f)
// {
//     aegis::promise<V> pr;
//     auto fut = pr.get_future();
// 
//     asio::post(*aegis::internal::_io_context, asio::bind_executor(ex, [pr = std::move(pr), f = std::move(f)]() mutable
//     {
//         f();
//         pr.set_value();
//     }));
//     return fut;
// }

// template<typename E, typename T>
// aegis::future<void> async(E ex, T f)
// {
//     aegis::async_executor::promise<void> pr;
//     auto fut = pr.get_future();
// 
//     asio::post(*aegis::internal::_io_context, asio::bind_executor(ex, [pr = std::move(pr), f = std::move(f)]() mutable
//     {
//         f();
//         pr.set_value();
//     }));
//     return fut;
// }


}
