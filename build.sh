#!/bin/sh

chmod 777 fdk_aac/build.sh

B_PATH=$PWD

build_all(){
cd $B_PATH/fdk_aac
./build.sh all
}

build_clean(){
cd $B_PATH/fdk_aac
./build.sh clean
}

if test $# -eq 0
then
        echo 'install'
        build_all
else
	if [ $1 == 'clean' ]
	then
        	echo 'uninstall'
        	build_clean
	fi
fi

