#pragma once
#include "const.h"
#include "Singleton.h"


class ThreadPool final :public Singleton<ThreadPool>
{
    friend class Singleton<ThreadPool>;
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ~ThreadPool()
    {
        stop();
    }

    /*
     * 第一次调用时创建线程池。
     *
     * 注意：
     * 第一次传入的 threadCount 有效；
     * 后面再次调用 instance(其他数量)，不会重新创建线程池。
     *
     * 建议程序启动时先读取 config.ini：
     *
     * ThreadPool::instance(threadCount);
     *
     * 后续业务中直接：
     *
     * ThreadPool::instance().commit(...);
     */

    template <typename F, typename... Args>
    auto commit(F&& func, Args&&... args)
        -> std::future<
        std::invoke_result_t<// 识别成员函数和仿函数
        std::decay_t<F>,
        std::decay_t<Args>...>>
    {
        using FunctionType = std::decay_t<F>;

        using ArgumentsType =
            std::tuple<std::decay_t<Args>...>;

        using ReturnType =
            std::invoke_result_t<
            FunctionType,
            std::decay_t<Args>...>;

        /*
         * 把函数和参数保存起来。
         *
         * 任务真正被工作线程执行时，
         * 参数才会传递给原函数。
         */
        auto packagedTask =
            std::make_shared<
            std::packaged_task<ReturnType()>>(
                [
                    function =
                        FunctionType(
                            std::forward<F>(func)),

                        arguments =
                        ArgumentsType(
                            std::forward<Args>(args)...)
                ]() mutable -> ReturnType
                {
                    return std::apply(
                        [&function](
                            auto&&... unpacked)
                        mutable -> ReturnType
                        {
                            return std::invoke(
                                std::move(function),

                                std::forward<
                                decltype(unpacked)>(
                                    unpacked)...
                            );
                        },

                        std::move(arguments)
                    );
                }
                        );

        std::future<ReturnType> result =
            packagedTask->get_future();

        {
            /*
             * 停止检查和任务入队，
             * 必须由同一把互斥锁保护。
             *
             * 这样 stop() 与 commit()
             * 就不会发生竞争。
             */
            std::lock_guard<std::mutex> lock(
                mutex_
            );

            if (stopping_)
            {
                throw std::runtime_error(
                    "cannot commit task to a stopped ThreadPool"
                );
            }

            tasks_.emplace(
                [packagedTask]() mutable
                {
                    (*packagedTask)();
                }
            );
        }

        condition_.notify_one();

        return result;
    }

    /*
     * 停止线程池。
     *
     * 已经进入队列的任务会继续执行，
     * 队列清空后工作线程才退出。
     *
     * 不要在线程池自己的任务中调用 stop()，
     * 应该由 main 线程或服务器控制线程调用。
     */
    void stop()
    {
        {
            std::lock_guard<std::mutex> lock(
                mutex_
            );

            if (stopping_)
            {
                return;
            }

            stopping_ = true;
        }

        condition_.notify_all();

        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }

        workers_.clear();
    }

    [[nodiscard]]
    std::size_t threadCount() const noexcept
    {
        return thread_count_;
    }

private:
    using Task = std::function<void()>;

    ThreadPool()
        : ThreadPool(DefaultThreadCount())
    {
    }

    explicit ThreadPool(std::size_t threadCount)
        : stopping_(false)
        , thread_count_(
            threadCount == 0
            ? 1
            : threadCount)
    {
        Start();
    }

    /*
     * hardware_concurrency() 允许返回 0。
     *
     * 返回 0 时使用 4 个线程作为默认值。
     */
    static std::size_t DefaultThreadCount() noexcept
    {
        const unsigned int count =
            std::thread::hardware_concurrency();

        return count == 0
            ? 4U
            : static_cast<std::size_t>(
                count);
    }

    void Start()
    {
        workers_.reserve(thread_count_);

        try
        {
            for (
                std::size_t i = 0;
                i < thread_count_;
                ++i)
            {
                workers_.emplace_back(
                    [this]
                    {
                        WorkerLoop();
                    }
                );
            }
        }
        catch (...)
        {
            /*
             * 某个线程创建失败时，
             * 停止并回收已经创建成功的线程。
             */
            {
                std::lock_guard<std::mutex> lock(
                    mutex_
                );

                stopping_ = true;
            }

            condition_.notify_all();

            for (std::thread& worker : workers_)
            {
                if (worker.joinable())
                {
                    worker.join();
                }
            }

            workers_.clear();

            throw;
        }
    }

    void WorkerLoop()
    {
        for (;;)
        {
            Task task;

            {
                std::unique_lock<std::mutex> lock(
                    mutex_
                );

                condition_.wait(
                    lock,
                    [this]
                    {
                        return stopping_
                            || !tasks_.empty();
                    }
                );

                /*
                 * stop() 后不立刻退出。
                 *
                 * 只有：
                 *
                 * 1. 已经要求停止；
                 * 2. 队列已经为空；
                 *
                 * 才真正结束工作线程。
                 */
                if (stopping_ && tasks_.empty())
                {
                    return;
                }

                task = std::move(
                    tasks_.front()
                );

                tasks_.pop();
            }

            /*
             * 业务函数抛出的异常会被 packaged_task
             * 保存到 future 中，不会直接丢失。
             */
            task();
        }
    }

private:
    /*
     * stopping_ 的所有访问都由 mutex_ 保护，
     * 所以这里不需要 atomic。
     */
    bool stopping_;

    /*
     * 线程数初始化后不会变化，
     * 所以也不需要 atomic。
     */
    const std::size_t thread_count_;

    std::mutex mutex_;
    std::condition_variable condition_;

    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
};