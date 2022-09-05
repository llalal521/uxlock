#!/bin/bash

LOCAL_DIR=$(dirname $(readlink -f "$0"))
LITL_DIR=$LOCAL_DIR/..
LITLLIB_DIR=$LITL_DIR/lib
export LD_LIBRARY_PATH=$LITLLIB_DIR:$LD_LIBRARY_PATH


$LITL_DIR/libutablocking_spin_then_park.sh  $LITL_DIR/bin/uta_check 

# $LITL_DIR/bin/check 