(taskset -c 1 ../build/src/erasure_coding/ec_recover -o outp1 -t 128 -r 32 -n 128 > out1) &
(taskset -c 0 ../build/src/erasure_coding/ec_recover -o outp2 -t 2 -r 2 -n 128 > out2) &
wait