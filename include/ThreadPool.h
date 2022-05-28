#pragma once

#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

#include "SafeQueue.h"

/*--
线程什么时候开始工作？
只在线程池初始化的时候线程才开始从任务队列中拿任务，那线程任务完成后去哪里拿？
线程满是怎么判断的？
--*/


class ThreadPool {
private:
 
  class ThreadWorker {
   /*-----ThreadWorker类-------
     类型：仿函数
     功能：完成线程对队列中任务的获取
     方式：ThreadWorker tw(参数); tw();
  */
  private:
    int m_id;   //m_id是线程的编号id，[0,n_threads-1]
    ThreadPool * m_pool; //线程所属的线程池
  public:
    ThreadWorker(ThreadPool * pool, const int id)
      : m_pool(pool), m_id(id) {
    }
    /*-----operator()()--
    */
    void operator()() {
      std::function<void()> func;
      bool dequeued;
      while (!m_pool->m_shutdown) {
          /*
          !!!!此处是一个循环，某个线程一旦接收这个伪函数运行，就会一直循环在这里从任务队列中取任务函数并运行。
          */
        {
          std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);  //这里是对共享的任务队列的访问，同时任务队列的空满也是条件变量堵塞线程的判断条件
          if (m_pool->m_queue.empty()) {
            m_pool->m_conditional_lock.wait(lock);
          }
          dequeued = m_pool->m_queue.dequeue(func);  
        }
        if (dequeued) {
          func();
        }
      }
    }
  };

  bool m_shutdown;
  SafeQueue<std::function<void()>> m_queue;
  std::vector<std::thread> m_threads;    
  std::mutex m_conditional_mutex;         
  std::condition_variable m_conditional_lock;
public:
  ThreadPool(const int n_threads)
    : m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false) {
  }

  ThreadPool(const ThreadPool &) = delete;  //禁用传入ThreadPool指针的构造函数，wgl_wt:此处的禁用是为了什么？是为了防止线程池复制
  ThreadPool(ThreadPool &&) = delete;

  ThreadPool & operator=(const ThreadPool &) = delete;
  ThreadPool & operator=(ThreadPool &&) = delete;

  // Inits thread pool
  void init() {
    for (int i = 0; i < m_threads.size(); ++i) {
      m_threads[i] = std::thread(ThreadWorker(this, i)); //这里实际上等价于ThreadWorker tw=ThreadWorker(this,i); thread(tw);
    }
  }

  // Waits until threads finish their current task and shutdowns the pool
  void shutdown() {
    m_shutdown = true;
    m_conditional_lock.notify_all();
    
    for (int i = 0; i < m_threads.size(); ++i) {
      if(m_threads[i].joinable()) {
        m_threads[i].join();
      }
    }
  }

  // Submit a function to be executed asynchronously by the pool
  /*------submit函数-----
  参数：
    F&& f ，要提交的任务函数；
    Args&&... args，任务函数的传入参数，可变
    
  返回值：
    返回一个能获取提交的任务函数执行结果的future对象
    
  作用：
    接收一个任务函数，将任务函数放入到任务队列中，并返回一个能获取该任务函数返回值的future对象
    
  注意点：
    1. 由于任务函数的参数和返回值都是不固定的，在设计任务队列时，是不太好定义队列的函数类型的，因此实现部分对任务函数进行包装，统一成<void()>类型
    
  -----------------------------------------
  
  */
  template<typename F, typename...Args>
 
  auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))> {
    
    // Create a function with bounded parameters ready to execute
    //这一步将有参任务函数f,bind()成无参任务函数func
    std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    
    // Encapsulate it into a shared ptr in order to be able to copy construct / assign 
    auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);  //wgl_wt:这里很好奇为什么要传入func，func和所声明的共享指针类型也不同啊？
    
    //这里如果不用共享指针，可以如下来写：
    /*
     std::packaged_task<declytpe(f(args...))()> mytask(func);
     std::function<void()> wrapper_func=[mytask](){
       mytask();
     }
     后面类似，最后
     return mytask.get_future();
    */
   
    

    // Wrap packaged task into void function
    std::function<void()> wrapper_func = [task_ptr]() {
      (*task_ptr)();   //在这个里面实现任务函数
    };

    // Enqueue generic wrapper function
    m_queue.enqueue(wrapper_func);

    // Wake up one thread if its waiting
    m_conditional_lock.notify_one();

    // Return future from promise
    return task_ptr->get_future();   
  }
};
