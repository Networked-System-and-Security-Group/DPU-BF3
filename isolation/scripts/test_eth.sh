#!/bin/zsh

# 确保脚本以用户期望的方式执行，添加必要的权限检查或其他验证步骤

# 启动第一个命令并将其绑定到逻辑核心 0，同时使用 perf 测试缓存命中率
# (sudo taskset -c 0 perf stat -e L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads ./build/kvs/doca_kvs -j kvs/eth_l2_fwd_params.json > out1 2> perf_out1) &
(sudo taskset -c 0 ../build/src/kvs/doca_kvs -j ../src/kvs/eth_l2_fwd_params.json > out1) &

# 启动第二个命令并将其绑定到逻辑核心 1，同时使用 perf 测试缓存命中率
(sudo taskset -c 1 ../build/src/counter/doca_counter -j ../src/counter/eth_l2_fwd_params.json > out2) &

# 使用 wait 命令等待所有后台任务完成
wait

echo "Both tasks have been completed."
