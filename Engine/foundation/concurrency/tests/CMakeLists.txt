cd_add_test(concurrency
  SOURCES test_concurrency.cpp
  DEPS    cd::concurrency cd::core
)

cd_add_test(threadpool
  SOURCES test_threadpool.cpp
  DEPS    cd::concurrency cd::core
)

cd_add_test(deterministic_executor
  SOURCES test_deterministic_executor.cpp
  DEPS    cd::concurrency cd::core
)

cd_add_test(work_stealing_deque
  SOURCES test_work_stealing_deque.cpp
  DEPS    cd::concurrency cd::core
)

cd_add_test(work_stealing_pool
  SOURCES test_work_stealing_pool.cpp
  DEPS    cd::concurrency cd::core
)

cd_add_test(hazard_ptr
  SOURCES test_hazard_ptr.cpp
  DEPS    cd::concurrency cd::core
)
