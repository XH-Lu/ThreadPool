#include "threadpool.h"


int sum1(int a, int b)
{
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    return (a + b);
}
int sum2(int a, int b, int c)
{
    //std::this_thread::sleep_for(std::chrono::seconds(2));
    return (a + b + c);
}

int main()
{

    {
        ThreadPool pool;
        pool.start(2);

        std::future<int> r1 = pool.submitTask(sum1, 1, 2);
        std::future<int> r2 = pool.submitTask(sum2, 1, 2, 3);
        std::cout << r1.get() << std::endl;
        std::cout << r2.get() << std::endl;
    }

    std::getchar();
    return 0;
}