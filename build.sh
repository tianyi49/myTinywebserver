#!/bin/sh

set -x

SOURCE_DIR=`pwd`
BUILD_DIR=${BUILD_DIR:-./build}
# BUILD_TYPE=${BUILD_TYPE:-Debug}
DEBUG_FLAG=${DEBUG_FLAG:-1}

mkdir -p $BUILD_DIR/ \
    && cd $BUILD_DIR/ \
    && cmake \
            -DCMAKE_DEBUG_FLAG=$DEBUG_FLAG \
            $SOURCE_DIR \
    && make $*