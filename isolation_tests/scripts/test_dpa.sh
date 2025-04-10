#!/bin/zsh

# 确保脚本以用户期望的方式执行，添加必要的权限检查或其他验证步骤

# 启动第一个命令并将其输出重定向到out1，使用子shell（...）与&后台执行符
(sudo ../build/src/kv_aggregation/host/kv_aggregation mlx5_0 85 120 > out1) &

# 启动第二个命令并将其输出重定向到out2，同样使用子shell（...）与&后台执行符
(sudo ../build/src/check_header/host/check_header mlx5_1 85 120 > out2) &

# 使用wait命令等待所有后台任务完成
wait

echo "Both tasks have been completed."
