#ifndef THREADPOOL_H
#define THREADPOOL_H
#include<iostream>
//#include<vector>
#include<unordered_map>
#include<queue>
#include<atomic>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include<future>

#define THREAD_INIT_ID 0
#define TASK_MAX_THRESHHOLD INT32_MAX
#define THREADS_DEFAULT_NUM_ 10
#define THREADS_MAX_NUM_ 1024
#define THREAD_MAX_IDLE_TIME 60


///////////////Thread��///////////////
class Thread
{
public:
	using ThreadFunc = std::function<void(int)>;
	Thread() = default;

	Thread(ThreadFunc func)
	{
		thread_func_ = func;
		th_id_ = th_total_id_++;
	}

	~Thread() = default;

	void start()
	{
		std::thread th1(thread_func_, th_id_);
		th1.detach();
	}

	int getID()
	{
		return th_id_;
	}

private:

	ThreadFunc thread_func_;
	static int th_total_id_;
	int th_id_;
};

int Thread::th_total_id_ = THREAD_INIT_ID;


///////////////ThreadPool��///////////////
enum class ThreadPoolMode
{
	FIXED,
	CACHED,
};

class ThreadPool
{
private:
	//�̳߳ع�����Ҫ�ı���
	//std::vector<std::shared_ptr<Thread>> ths_pool_;
	std::unordered_map<int, std::shared_ptr<Thread>> ths_pool_;
	int th_max_threshold;
	std::atomic_int th_default_num_;
	std::atomic_int th_cur_num_;
	std::atomic_int th_idle_num_;
	ThreadPoolMode pool_mode_;
	bool is_running_;

	//������й�����Ҫ�ı���
	using Task = std::function<void()>;
	std::queue<Task> tasks_que_;
	int task_max_threshold_;
	std::atomic_int task_num_;

	//�߳�ͨ����Ҫ��������������
	std::mutex mtx_;
	std::condition_variable not_full_;
	std::condition_variable not_empty_;
	std::condition_variable is_end_;

public:

	ThreadPool()
		: th_default_num_(THREADS_DEFAULT_NUM_),
		pool_mode_(ThreadPoolMode::FIXED),
		is_running_(true),
		th_max_threshold(THREADS_MAX_NUM_),
		th_cur_num_(0),
		th_idle_num_(0),
		task_max_threshold_(TASK_MAX_THRESHHOLD),
		task_num_(0)
	{}

	~ThreadPool()
	{
		std::unique_lock<std::mutex> lock(mtx_);
		is_running_ = false;
		not_empty_.notify_all();

		is_end_.wait(lock, [&]()->bool {return ths_pool_.size() == 0; });
	}

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	template<typename Func, typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
	{
		using RetType = decltype(func(args...));
		auto task = std::make_shared<std::packaged_task<RetType()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));
		std::future<RetType> result = task->get_future();

		std::unique_lock<std::mutex> lock(mtx_);
		//not_full_.wait(lock, [&]()->bool {return tasks_que_.size() < task_max_threadhold_;  });
		if (!not_full_.wait_for(lock, std::chrono::seconds(1), [&]()->bool {return tasks_que_.size() < task_max_threshold_;  }))
		{
			std::cout << "�������ʧ��" << std::endl;
			auto task = std::make_shared<std::packaged_task<RetType()>>([]()->RetType { return RetType(); });
			(*task)();
			return task->get_future();
		}

		tasks_que_.emplace([task]() {(*task)(); });
		task_num_++;
		not_empty_.notify_all();

		if (pool_mode_ == ThreadPoolMode::CACHED && task_num_ > th_idle_num_ && th_cur_num_ < th_max_threshold)
		{
			auto tk = std::make_shared<Thread>(std::bind(&ThreadPool::doTask, this, std::placeholders::_1));
			ths_pool_.emplace(tk->getID(), tk);
			ths_pool_[tk->getID()]->start();
			th_cur_num_++;
			th_idle_num_++;
			std::cout << "��չID:" << tk->getID() << "����" << std::endl;
		}
		return result;
	}

	void start(int thn = THREADS_DEFAULT_NUM_, ThreadPoolMode pm = ThreadPoolMode::FIXED, int thm = THREADS_MAX_NUM_, int tm = TASK_MAX_THRESHHOLD)
	{
		th_default_num_ = thn;
		pool_mode_ = pm;
		th_max_threshold = thm;
		task_max_threshold_ = tm;
		th_cur_num_ = 0;
		th_idle_num_ = 0;
		task_num_ = 0;
		is_running_ = true;


		for (int i = 0; i < th_default_num_; ++i)
		{
			auto tk = std::make_shared<Thread>(std::bind(&ThreadPool::doTask, this, std::placeholders::_1));
			ths_pool_.emplace(tk->getID(), tk);
			th_cur_num_++;
			std::cout << "ID:" << tk->getID() << "����" << std::endl;
		}

		for (int i = THREAD_INIT_ID; i < THREAD_INIT_ID + th_default_num_; ++i)
		{
			ths_pool_[i]->start();
			th_idle_num_++;
		}
	}

	void doTask(int id)
	{
		auto lastTime = std::chrono::high_resolution_clock().now();
		while (1)
		{
			Task task;
			{
				std::unique_lock<std::mutex> lock(mtx_);
				std::cout << "�߳�ID:" << id << "�ȴ���������" << std::endl;

				//����״̬
				while (task_num_ == 0)
				{
					if (!is_running_)
					{
						ths_pool_.erase(id);
						th_cur_num_--;
						th_idle_num_--;
						std::cout << "ID:" << id << "�ͷ�" << std::endl;
						is_end_.notify_all();
						return;
					}
					if (pool_mode_ == ThreadPoolMode::CACHED)
					{
						//�ж���չ���߳̿���ʱ���Ƿ񳬹�Ԥ��ֵ������������ͷ���չ�߳�
						if (std::cv_status::timeout == not_empty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
							if (dur.count() >= THREAD_MAX_IDLE_TIME && th_cur_num_ > th_default_num_)
							{
								ths_pool_.erase(id);
								th_cur_num_--;
								th_idle_num_--;
								std::cout << "��չID:" << id << "�ͷ�" << std::endl;
								is_end_.notify_all();
								return;
							}
						}
					}
					else
					{
						not_empty_.wait(lock);
					}

				}

				//��������
				th_idle_num_--;
				task = tasks_que_.front();
				tasks_que_.pop();
				task_num_--;
				not_full_.notify_all();
				if (tasks_que_.size() > 0)
					not_empty_.notify_all();
			}
			//ִ������
			std::cout << "�߳�ID:" << id << "�ɹ��ӵ�����" << std::endl;
			task();
			th_idle_num_++;
		}

	}

};


#endif