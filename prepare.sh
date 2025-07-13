#!/bin/bash

function create_symlink(){
    for file in `ls $1`
    do
        if [ -d $1"/"$file ]
        then
            if [ "$file" != "picocalc-overlay" ]
            then
                create_symlink $1"/"$file $2"/"$file
            fi
        else
            local path_src=$1"/"$file  
            local path_sdk=$2"/"$file
            if [ ! -d "$2" ]
            then
                mkdir -p $2
            fi
            ln -sfr $path_src $path_sdk
        fi
    done
}

CUR_PATH=$(cd "$(dirname $0)";pwd)
SRC_PATH=$CUR_PATH/src
SDK_PATH=$CUR_PATH/..
IFS=$'\n'

echo "SRC_PATH: $SRC_PATH"
echo "SDK_PATH: $SDK_PATH" 

# check SDK path
if [ -e $SRC_PATH ]
then
    for file in `ls $SRC_PATH`
    do
        if [ -d $SRC_PATH"/"$file ]
        then
            if [ ! -d $SDK_PATH"/"$file ]
            then
                echo "error: not a SDK path!"
                exit
            fi
        fi
    done
else
    echo "error: not a source path!"
    exit
fi

create_symlink $SRC_PATH $SDK_PATH

if [ -d "$SDK_PATH/buildroot/board/rockchip/rk3506/picocalc-overlay" ]
then
    rm -rf $SDK_PATH/buildroot/board/rockchip/rk3506/picocalc-overlay
fi
ln -sr $SRC_PATH/buildroot/board/rockchip/rk3506/picocalc-overlay $SDK_PATH/buildroot/board/rockchip/rk3506/picocalc-overlay

create_symlink $SRC_PATH/device/rockchip/.chips $SDK_PATH/device/rockchip/.chips

if [ -d "$SDK_PATH/device/rockchip/common/extra-parts/userdata/picocalc" ]
then
    rm -rf $SDK_PATH/device/rockchip/common/extra-parts/userdata/picocalc
fi
mkdir $SDK_PATH/device/rockchip/common/extra-parts/userdata/picocalc
