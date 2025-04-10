FRAMEWORK=doca
SIZE=big
CORE=5
if [ "$1" = "a" ]
then
    FRAMEWORK=astraea
fi
if [ "$2" = "s" ]
then
    SIZE=small
    CORE=6
fi

taskset -c $CORE ./build/src/example/$FRAMEWORK/ec_create_$FRAMEWORK -j config/ec_create_$SIZE.jsonc